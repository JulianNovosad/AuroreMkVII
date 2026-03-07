#!/usr/bin/env python3
"""
Pipeline Trace Visualization Tool
Analyzes trace data from aurore::trace::PipelineTracer and generates timing visualizations.
"""

import argparse
import csv
import json
import os
import sys
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import matplotlib
matplotlib.use('Agg')  # Non-interactive backend
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

STAGE_NAMES = {
    0: "MIPI_INTERRUPT",
    1: "LIBCAMERA_CAPTURE",
    2: "IMAGE_PROCESSOR",
    3: "TPU_INFERENCE",
    4: "DETECTION_PARSING",
    5: "BALLISTICS_CALC",
    6: "FIRE_AUTHORIZATION",
    7: "SERVO_ACTUATION"
}

STAGE_COLORS = {
    0: "#FF6B6B",  # Red
    1: "#4ECDC4",  # Teal
    2: "#45B7D1",  # Blue
    3: "#96CEB4",  # Green
    4: "#FFEAA7",  # Yellow
    5: "#DDA0DD",  # Plum
    6: "#98D8C8",  # Mint
    7: "#F7DC6F"   # Gold
}


@dataclass
class TraceEntry:
    frame_id: int
    stage: int
    timestamp_ns: int
    event_type: str  # "enter" or "exit"
    latency_ns: Optional[int] = None


class TraceAnalyzer:
    def __init__(self, trace_file: str):
        self.trace_file = trace_file
        self.entries: List[TraceEntry] = []
        self.frame_stages: Dict[int, Dict[int, TraceEntry]] = defaultdict(dict)
        
    def load_trace(self) -> bool:
        """Load trace data from CSV file."""
        if not os.path.exists(self.trace_file):
            print(f"Error: Trace file not found: {self.trace_file}")
            return False
            
        try:
            with open(self.trace_file, 'r') as f:
                reader = csv.reader(f)
                header = next(reader)  # Skip header
                
                for row in reader:
                    if len(row) < 4:
                        continue
                        
                    frame_id = int(row[0])
                    stage = int(row[1])
                    timestamp_ns = int(row[2])
                    event_type = row[3]
                    
                    entry = TraceEntry(
                        frame_id=frame_id,
                        stage=stage,
                        timestamp_ns=timestamp_ns,
                        event_type=event_type
                    )
                    
                    # Parse latency from exit events
                    if event_type == "exit" and len(row) > 4:
                        try:
                            entry.latency_ns = int(row[4])
                        except ValueError:
                            pass
                    
                    self.entries.append(entry)
                    
                    # Store enter/exit pairs for each frame
                    if event_type == "enter":
                        self.frame_stages[frame_id][stage] = entry
                    elif event_type == "exit" and stage in self.frame_stages[frame_id]:
                        self.frame_stages[frame_id][stage].latency_ns = entry.latency_ns
                        
            print(f"Loaded {len(self.entries)} trace entries from {self.trace_file}")
            return True
        except Exception as e:
            print(f"Error loading trace file: {e}")
            return False
    
    def compute_frame_latencies(self) -> List[Dict]:
        """Compute end-to-end latency for each frame."""
        frame_latencies = []
        
        for frame_id in sorted(self.frame_stages.keys()):
            stages = self.frame_stages[frame_id]
            
            # Find MIPI interrupt (entry) and SERVO actuation (exit)
            if 0 not in stages or 7 not in stages:
                continue
                
            mipi_entry = stages[0]
            servo_exit_latency = None
            servo_exit = None
            
            # Find servo exit (latest exit before next MIPI)
            for stage_id, entry in stages.items():
                if entry.event_type == "exit" and (servo_exit is None or entry.timestamp_ns > servo_exit.timestamp_ns):
                    servo_exit = entry
                    
            if servo_exit:
                end_to_end_latency = servo_exit.timestamp_ns - mipi_entry.timestamp_ns
                
                frame_data = {
                    "frame_id": frame_id,
                    "start_ns": mipi_entry.timestamp_ns,
                    "end_ns": servo_exit.timestamp_ns,
                    "latency_ns": end_to_end_latency,
                    "latency_ms": end_to_end_latency / 1_000_000.0,
                    "stages": {}
                }
                
                for stage_id, entry in stages.items():
                    if entry.event_type == "exit":
                        frame_data["stages"][stage_id] = {
                            "name": STAGE_NAMES.get(stage_id, f"STAGE_{stage_id}"),
                            "latency_ns": entry.latency_ns,
                            "latency_ms": (entry.latency_ns / 1_000_000.0) if entry.latency_ns else 0
                        }
                
                frame_latencies.append(frame_data)
                
        return frame_latencies
    
    def compute_stage_statistics(self) -> Dict:
        """Compute statistics for each stage."""
        stage_stats = {}
        
        for stage_id, name in STAGE_NAMES.items():
            latencies = []
            for frame_id, stages in self.frame_stages.items():
                if stage_id in stages and stages[stage_id].latency_ns:
                    latencies.append(stages[stage_id].latency_ns)
            
            if latencies:
                stage_stats[stage_id] = {
                    "name": name,
                    "count": len(latencies),
                    "min_ns": min(latencies),
                    "max_ns": max(latencies),
                    "mean_ns": np.mean(latencies),
                    "std_ns": np.std(latencies),
                    "p95_ns": np.percentile(latencies, 95),
                    "p99_ns": np.percentile(latencies, 99),
                    "min_ms": min(latencies) / 1_000_000.0,
                    "max_ms": max(latencies) / 1_000_000.0,
                    "mean_ms": np.mean(latencies) / 1_000_000.0,
                    "p95_ms": np.percentile(latencies, 95) / 1_000_000.0
                }
            else:
                stage_stats[stage_id] = {
                    "name": name,
                    "count": 0
                }
                
        return stage_stats
    
    def generate_timeline_plot(self, output_path: str, max_frames: int = 100):
        """Generate a Gantt-style timeline of pipeline stages."""
        frame_latencies = self.compute_frame_latencies()[:max_frames]
        
        if not frame_latencies:
            print("No frame data to plot")
            return
            
        fig, ax = plt.subplots(figsize=(16, 10))
        
        num_frames = len(frame_latencies)
        y_positions = np.arange(num_frames)
        
        bar_height = 0.8
        
        for i, frame in enumerate(frame_latencies):
            start_rel = frame["start_ns"] - frame_latencies[0]["start_ns"]
            
            for stage_id in sorted(frame["stages"].keys()):
                stage_data = frame["stages"][stage_id]
                if stage_data["latency_ns"]:
                    duration = stage_data["latency_ns"]
                    x_pos = start_rel / 1_000_000.0  # Convert to ms
                    width = duration / 1_000_000.0
                    
                    color = STAGE_COLORS.get(stage_id, "#CCCCCC")
                    ax.barh(y_positions[i], width, left=x_pos, height=bar_height,
                           color=color, edgecolor='black', linewidth=0.5)
        
        ax.set_xlabel('Time (ms)', fontsize=12)
        ax.set_ylabel('Frame', fontsize=12)
        ax.set_title(f'Pipeline Trace Timeline (First {num_frames} Frames)', fontsize=14)
        ax.set_yticks(y_positions)
        ax.set_yticklabels([f'F{f["frame_id"]}' for f in frame_latencies])
        ax.invert_yaxis()
        
        # Legend
        patches = [mpatches.Patch(color=STAGE_COLORS[i], label=STAGE_NAMES[i]) 
                   for i in STAGE_COLORS if i in STAGE_NAMES]
        ax.legend(handles=patches, loc='upper right', fontsize=8)
        
        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        
        print(f"Timeline saved to: {output_path}")
    
    def generate_latency_boxplot(self, output_path: str):
        """Generate boxplot of latency distributions per stage."""
        stage_stats = self.compute_stage_statistics()
        
        valid_stages = [(sid, data) for sid, data in stage_stats.items() if data["count"] > 0]
        
        if not valid_stages:
            print("No valid stage data for boxplot")
            return
        
        fig, ax = plt.subplots(figsize=(14, 8))
        
        # Collect all latencies per stage
        stage_latencies = defaultdict(list)
        for frame_id, stages in self.frame_stages.items():
            for stage_id, entry in stages.items():
                if entry.latency_ns:
                    stage_latencies[stage_id].append(entry.latency_ns / 1_000_000.0)  # ms
        
        # Prepare data for boxplot
        box_data = []
        labels = []
        colors = []
        
        for stage_id, name in STAGE_NAMES.items():
            if stage_id in stage_latencies and stage_latencies[stage_id]:
                box_data.append(stage_latencies[stage_id])
                labels.append(name.replace("_", "\n"))
                colors.append(STAGE_COLORS.get(stage_id, "#CCCCCC"))
        
        if box_data:
            bp = ax.boxplot(box_data, labels=labels, patch_artist=True)
            
            for patch, color in zip(bp['boxes'], colors):
                patch.set_facecolor(color)
                patch.set_alpha(0.7)
                
        ax.set_ylabel('Latency (ms)', fontsize=12)
        ax.set_title('Pipeline Stage Latency Distribution', fontsize=14)
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        
        print(f"Boxplot saved to: {output_path}")
    
    def generate_summary_report(self, output_path: str):
        """Generate a text summary report."""
        frame_latencies = self.compute_frame_latencies()
        stage_stats = self.compute_stage_statistics()
        
        lines = []
        lines.append("=" * 80)
        lines.append("PIPELINE TRACE ANALYSIS REPORT")
        lines.append(f"Generated: {datetime.now().isoformat()}")
        lines.append(f"Trace File: {self.trace_file}")
        lines.append("=" * 80)
        lines.append("")
        
        # Frame statistics
        if frame_latencies:
            latencies_ms = [f["latency_ms"] for f in frame_latencies]
            lines.append("END-TO-END LATENCY (MIPI to Servo)")
            lines.append("-" * 40)
            lines.append(f"  Frames analyzed: {len(frame_latencies)}")
            lines.append(f"  Mean:   {np.mean(latencies_ms):.3f} ms")
            lines.append(f"  Min:    {min(latencies_ms):.3f} ms")
            lines.append(f"  Max:    {max(latencies_ms):.3f} ms")
            lines.append(f"  Std:    {np.std(latencies_ms):.3f} ms")
            lines.append(f"  P95:    {np.percentile(latencies_ms, 95):.3f} ms")
            lines.append(f"  P99:    {np.percentile(latencies_ms, 99):.3f} ms")
            lines.append("")
        
        # Stage statistics
        lines.append("STAGE-BY-STAGE LATENCY")
        lines.append("-" * 40)
        
        for stage_id, name in STAGE_NAMES.items():
            stats = stage_stats.get(stage_id, {})
            if stats.get("count", 0) > 0:
                lines.append(f"\n  {name}")
                lines.append(f"    Count:  {stats['count']}")
                lines.append(f"    Mean:   {stats['mean_ms']:.4f} ms")
                lines.append(f"    Min:    {stats['min_ms']:.4f} ms")
                lines.append(f"    Max:    {stats['max_ms']:.4f} ms")
                lines.append(f"    P95:    {stats['p95_ms']:.4f} ms")
                
                # Check against budget (16ms for 60fps)
                if stats['p95_ms'] > 16.0:
                    lines.append(f"    WARNING: P95 exceeds 16ms budget!")
            else:
                lines.append(f"\n  {name}: No data")
        
        lines.append("")
        lines.append("=" * 80)
        
        report = "\n".join(lines)
        
        with open(output_path, 'w') as f:
            f.write(report)
            
        print(f"Summary report saved to: {output_path}")
        print(report)
    
    def export_json(self, output_path: str):
        """Export analysis results as JSON."""
        frame_latencies = self.compute_frame_latencies()
        stage_stats = self.compute_stage_statistics()
        
        results = {
            "metadata": {
                "trace_file": self.trace_file,
                "generated_at": datetime.now().isoformat(),
                "total_entries": len(self.entries)
            },
            "frame_latencies": frame_latencies,
            "stage_statistics": stage_stats
        }
        
        with open(output_path, 'w') as f:
            json.dump(results, f, indent=2)
            
        print(f"JSON export saved to: {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Aurore Pipeline Trace Analyzer"
    )
    parser.add_argument(
        "trace_file",
        help="Path to trace CSV file"
    )
    parser.add_argument(
        "-o", "--output-dir",
        default=".",
        help="Output directory for generated files"
    )
    parser.add_argument(
        "--max-frames",
        type=int,
        default=100,
        help="Maximum frames to visualize in timeline"
    )
    parser.add_argument(
        "--timeline",
        action="store_true",
        help="Generate timeline plot"
    )
    parser.add_argument(
        "--boxplot",
        action="store_true",
        help="Generate latency boxplot"
    )
    parser.add_argument(
        "--report",
        action="store_true",
        help="Generate text summary report"
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Export results as JSON"
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Generate all outputs"
    )
    
    args = parser.parse_args()
    
    if not os.path.exists(args.output_dir):
        os.makedirs(args.output_dir)
    
    analyzer = TraceAnalyzer(args.trace_file)
    
    if not analyzer.load_trace():
        sys.exit(1)
    
    base_name = os.path.splitext(os.path.basename(args.trace_file))[0]
    
    if args.all or args.timeline:
        analyzer.generate_timeline_plot(
            os.path.join(args.output_dir, f"{base_name}_timeline.png"),
            args.max_frames
        )
    
    if args.all or args.boxplot:
        analyzer.generate_latency_boxplot(
            os.path.join(args.output_dir, f"{base_name}_boxplot.png")
        )
    
    if args.all or args.report:
        analyzer.generate_summary_report(
            os.path.join(args.output_dir, f"{base_name}_report.txt")
        )
    
    if args.all or args.json:
        analyzer.export_json(
            os.path.join(args.output_dir, f"{base_name}_analysis.json")
        )
    
    # Default behavior: generate all if no options specified
    if not any([args.timeline, args.boxplot, args.report, args.json, args.all]):
        analyzer.generate_timeline_plot(
            os.path.join(args.output_dir, f"{base_name}_timeline.png"),
            args.max_frames
        )
        analyzer.generate_latency_boxplot(
            os.path.join(args.output_dir, f"{base_name}_boxplot.png")
        )
        analyzer.generate_summary_report(
            os.path.join(args.output_dir, f"{base_name}_report.txt")
        )


if __name__ == "__main__":
    main()
