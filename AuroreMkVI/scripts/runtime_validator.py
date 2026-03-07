#!/usr/bin/env python3
"""
Runtime Validator for AuroreMkVI
Analyzes logs after a runtime execution to detect issues and validate metrics.
"""

import re
import sys
import os
import json
from datetime import datetime
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple
from collections import defaultdict
from pathlib import Path


@dataclass
class ValidationMetrics:
    """Collected metrics from runtime execution."""
    duration_seconds: float = 0.0
    errors: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)
    frames_processed: int = 0
    inferences_completed: int = 0
    fps_values: List[float] = field(default_factory=list)
    queue_drop_count: int = 0
    buffer_starvation_count: int = 0
    memory_warnings: List[str] = field(default_factory=list)
    thread_safety_issues: List[str] = field(default_factory=list)
    blocking_issues: List[str] = field(default_factory=list)
    bounding_box_counts: List[int] = field(default_factory=list)
    detection_confidences: List[float] = field(default_factory=list)
    ballistic_impacts: int = 0
    safety_violations: int = 0
    hit_streaks: List[int] = field(default_factory=list)
    overlay_queue_drops: int = 0
    tpu_temp_readings: List[float] = field(default_factory=list)
    inference_times_us: List[int] = field(default_factory=list)
    capture_times_us: List[int] = field(default_factory=list)
    preprocess_times_us: List[int] = field(default_factory=list)


@dataclass
class ValidationResult:
    """Final validation result with pass/fail status."""
    status: str  # "PASS", "FAIL", "WARN"
    metrics: ValidationMetrics
    findings: List[str]
    suggestions: List[str]


class RuntimeValidator:
    """Analyzes AuroreMkVI runtime logs for issues and metrics."""

    # Regex patterns for parsing logs
    PATTERNS = {
        'timestamp': re.compile(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})'),
        'error': re.compile(r'APP_LOG_ERROR\[.*?\]: (.+)'),
        'warning': re.compile(r'APP_LOG_WARN\[.*?\]: (.+)'),
        'info': re.compile(r'APP_LOG_INFO\[.*?\]: (.+)'),
        'frames_processed': re.compile(r'Processed (\d+) frames in last interval'),
        'fps': re.compile(r'FPS: (\d+\.?\d*)'),
        'queue_drop': re.compile(r'Queue drop count: (\d+)'),
        'buffer_starvation': re.compile(r'Failed to acquire buffer from pool \(Starvation\)'),
        'output_queue_full': re.compile(r'Output queue push failed \(Full\)'),
        'buffer_acquisition_failed': re.compile(r'Failed to acquire ImageData from pool'),
        'detection_count': re.compile(r'detections=(\d+)'),
        'confidence': re.compile(r'Score: (\d+\.?\d*)'),
        'ballistic_point': re.compile(r'Ballistic point: x=(\d+\.?\d*), y=(\d+\.?\d*)'),
        'safety_violation': re.compile(r'Safety violation detected'),
        'hit_streak': re.compile(r'Hit streak: (\d+)'),
        'overlay_audit': re.compile(r'GPU_OVERLAY_AUDIT: frame_id=\d+, detections=(\d+), ballistic=(\d)'),
        'tpu_temperature': re.compile(r'TPU Temperature: (\d+\.?\d*)'),
        'thread_stall': re.compile(r'Thread stall detected'),
        'memory_growth': re.compile(r'Memory growth: \+(\d+\.?\d+)MB'),
        'allocation_failure': re.compile(r'Failed to allocate'),
        'mmap_failed': re.compile(r'mmap failed'),
        'dma_sync_failed': re.compile(r'DMA_BUF_SYNC.*failed'),
        'fstat_failed': re.compile(r'fstat failed'),
        'dup_failed': re.compile(r'dup failed'),
        'tensor_alloc_failed': re.compile(r'Failed to allocate tensors'),
        'interpreter_failed': re.compile(r'Failed to create interpreter'),
        'egl_error': re.compile(r'EGL.*error: (\d+)'),
        'gl_error': re.compile(r'OpenGL error'),
        'framebuffer_incomplete': re.compile(r'Framebuffer is not complete'),
        # TPU hardware errors
        'tpu_device_failed': re.compile(r'Failed to open device.*apex.*:'),
        'tpu_timeout': re.compile(r'Connection timed out'),
        'tpu_no_device': re.compile(r'No (matching )?device(s)? (found|available)'),
        'tpu_permission_denied': re.compile(r'Permission denied'),
        'tpu_driver_error': re.compile(r'\[ERROR\].*TPU'),
        'tpu_init_failed': re.compile(r'Failed to (initialize|create|open).*TPU'),
        'pcie_error': re.compile(r'PCIe.*(timeout|error|failed)'),
        'module_init_failed': re.compile(r'Failed to initialize modules'),
        # Monitor output patterns
        'tpu_throughput': re.compile(r'TPU Throughput: (\d+) IPS'),
        'inference_time': re.compile(r'Inference: (\d+) us'),
        'capture_time': re.compile(r'Capture: (\d+) us'),
        'preprocess_time': re.compile(r'Pre-processing: (\d+) us'),
        'overlay_queue_drops': re.compile(r'Overlay Queue Drops: (\d+)'),
        'main_stream_drops': re.compile(r'Main Stream Drops: (\d+)'),
        'tpu_stream_drops': re.compile(r'TPU Stream Drops: (\d+)'),
        'logic_queue_drops': re.compile(r'Logic Queue Drops: (\d+)'),
    }

    THRESHOLDS = {
        'min_fps': 55.0,
        'max_queue_drop_rate': 0.01,  # 1%
        'max_memory_growth_mb': 10.0,
        'min_detection_confidence': 0.7,
        'max_thread_stall_ms': 100.0,
        'max_buffer_starvation': 5,
        'max_errors': 0,
    }

    def __init__(self, log_content: str):
        self.log_content = log_content
        self.metrics = ValidationMetrics()
        self.findings: List[str] = []
        self.suggestions: List[str] = []

    def parse(self) -> ValidationMetrics:
        """Parse all log entries and collect metrics."""
        lines = self.log_content.split('\n')

        for line in lines:
            self._parse_line(line)

        return self.metrics

    def _parse_line(self, line: str):
        """Parse a single log line."""
        # Errors
        error_match = self.PATTERNS['error'].search(line)
        if error_match:
            error_msg = error_match.group(1).strip()
            self.metrics.errors.append(error_msg)
            self._analyze_error(error_msg)

        # Warnings
        warning_match = self.PATTERNS['warning'].search(line)
        if warning_match:
            self.metrics.warnings.append(warning_match.group(1).strip())

        # Frames processed
        match = self.PATTERNS['frames_processed'].search(line)
        if match:
            self.metrics.frames_processed = max(
                self.metrics.frames_processed,
                int(match.group(1))
            )

        # FPS values
        match = self.PATTERNS['fps'].search(line)
        if match:
            self.metrics.fps_values.append(float(match.group(1)))

        # Queue drops
        match = self.PATTERNS['queue_drop'].search(line)
        if match:
            self.metrics.queue_drop_count = int(match.group(1))

        # Buffer starvation
        if self.PATTERNS['buffer_starvation'].search(line):
            self.metrics.buffer_starvation_count += 1
            self.metrics.blocking_issues.append("Buffer pool starvation detected")

        # Output queue full
        if self.PATTERNS['output_queue_full'].search(line):
            self.metrics.blocking_issues.append("Output queue full - downstream bottleneck")

        # Buffer acquisition failed
        if self.PATTERNS['buffer_acquisition_failed'].search(line):
            self.metrics.buffer_starvation_count += 1

        # Detection count (from overlay audit)
        match = self.PATTERNS['overlay_audit'].search(line)
        if match:
            det_count = int(match.group(1))
            self.metrics.bounding_box_counts.append(det_count)

        # Confidence scores
        match = self.PATTERNS['confidence'].search(line)
        if match:
            self.metrics.detection_confidences.append(float(match.group(1)))

        # Hit streak
        match = self.PATTERNS['hit_streak'].search(line)
        if match:
            self.metrics.hit_streaks.append(int(match.group(1)))

        # TPU temperature
        match = self.PATTERNS['tpu_temperature'].search(line)
        if match:
            self.metrics.tpu_temp_readings.append(float(match.group(1)))

        # Memory warnings
        if self.PATTERNS['memory_growth'].search(line):
            match = self.PATTERNS['memory_growth'].search(line)
            if match:
                self.metrics.memory_warnings.append(
                    f"Memory growth: +{match.group(1)}MB"
                )

        # Thread safety issues
        if self.PATTERNS['thread_stall'].search(line):
            self.metrics.thread_safety_issues.append("Thread stall detected")

        # Allocation failures
        if self.PATTERNS['allocation_failure'].search(line):
            self.metrics.memory_warnings.append("Memory allocation failure")

        # TPU throughput (from monitor)
        match = self.PATTERNS['tpu_throughput'].search(line)
        if match:
            tpu_ips = float(match.group(1))
            if tpu_ips > 0:
                # Calculate approximate FPS from TPU throughput
                self.metrics.fps_values.append(tpu_ips)

        # Inference time
        match = self.PATTERNS['inference_time'].search(line)
        if match:
            inf_time = int(match.group(1))
            self.metrics.inference_times_us.append(inf_time)

        # Capture time
        match = self.PATTERNS['capture_time'].search(line)
        if match:
            self.metrics.capture_times_us.append(int(match.group(1)))

        # Preprocessing time
        match = self.PATTERNS['preprocess_time'].search(line)
        if match:
            self.metrics.preprocess_times_us.append(int(match.group(1)))

        # Queue drop counters
        match = self.PATTERNS['overlay_queue_drops'].search(line)
        if match:
            self.metrics.overlay_queue_drops = int(match.group(1))

        match = self.PATTERNS['main_stream_drops'].search(line)
        if match:
            # Add to total queue drops
            pass

        match = self.PATTERNS['tpu_stream_drops'].search(line)
        if match:
            pass

        match = self.PATTERNS['logic_queue_drops'].search(line)
        if match:
            pass

        # MMAP failures
        if self.PATTERNS['mmap_failed'].search(line):
            self.metrics.blocking_issues.append("MMAP failed - DMA buffer issue")

        # DMA sync failures
        if self.PATTERNS['dma_sync_failed'].search(line):
            self.metrics.thread_safety_issues.append("DMA sync failure - potential race")

        # FSTAT failures
        if self.PATTERNS['fstat_failed'].search(line):
            self.metrics.blocking_issues.append("FSTAT failed - file descriptor issue")

        # Tensor allocation failures
        if self.PATTERNS['tensor_alloc_failed'].search(line):
            self.metrics.memory_warnings.append("TPU tensor allocation failed")

        # Interpreter failures
        if self.PATTERNS['interpreter_failed'].search(line):
            self.metrics.errors.append("TPU interpreter creation failed")

        # EGL/GL errors
        if self.PATTERNS['egl_error'].search(line):
            self.metrics.errors.append("EGL error occurred")
        if self.PATTERNS['gl_error'].search(line):
            self.metrics.errors.append("OpenGL error occurred")
        if self.PATTERNS['framebuffer_incomplete'].search(line):
            self.metrics.errors.append("Framebuffer incomplete - GPU FBO setup failed")

        # TPU hardware errors
        if self.PATTERNS['tpu_device_failed'].search(line):
            self.metrics.errors.append("TPU device open failed")
        if self.PATTERNS['tpu_timeout'].search(line):
            self.metrics.errors.append("TPU connection timeout")
        if self.PATTERNS['tpu_no_device'].search(line):
            self.metrics.errors.append("No TPU device available")
        if self.PATTERNS['tpu_permission_denied'].search(line):
            self.metrics.errors.append("TPU permission denied")
        if self.PATTERNS['tpu_driver_error'].search(line):
            self.metrics.errors.append("TPU driver error")
        if self.PATTERNS['tpu_init_failed'].search(line):
            self.metrics.errors.append("TPU initialization failed")
        if self.PATTERNS['pcie_error'].search(line):
            self.metrics.errors.append("PCIe error with TPU")
        if self.PATTERNS['module_init_failed'].search(line):
            self.metrics.errors.append("Module initialization failed")

    def _analyze_error(self, error_msg: str):
        """Categorize and analyze errors."""
        error_lower = error_msg.lower()

        if 'failed' in error_lower and 'allocate' in error_lower:
            self.metrics.memory_warnings.append(f"Allocation error: {error_msg}")
        elif 'timeout' in error_lower or 'stall' in error_lower:
            self.metrics.blocking_issues.append(f"Timeout/stall: {error_msg}")
        elif 'queue' in error_lower or 'full' in error_lower:
            self.metrics.blocking_issues.append(f"Queue issue: {error_msg}")
        elif 'tpu' in error_lower or 'coral' in error_lower:
            self.findings.append(f"TPU error: {error_msg}")
        elif 'memory' in error_lower or 'mmap' in error_lower:
            self.metrics.memory_warnings.append(f"Memory issue: {error_msg}")
        elif 'thread' in error_lower or 'mutex' in error_lower or 'lock' in error_lower:
            self.metrics.thread_safety_issues.append(f"Threading issue: {error_msg}")

    def validate(self) -> ValidationResult:
        """Validate metrics against thresholds and generate findings."""
        findings = []
        suggestions = []

        # Check FPS
        if self.metrics.fps_values:
            avg_fps = sum(self.metrics.fps_values) / len(self.metrics.fps_values)
            max_fps = max(self.metrics.fps_values)
            min_fps = min(self.metrics.fps_values)

            if avg_fps < self.THRESHOLDS['min_fps']:
                findings.append(f"Low average FPS: {avg_fps:.1f} (threshold: {self.THRESHOLDS['min_fps']})")
                suggestions.append("Consider reducing overlay complexity or optimizing rendering")

            if min_fps < self.THRESHOLDS['min_fps'] * 0.8:
                findings.append(f"Minimum FPS dropped to {min_fps:.1f} - possible frame drop")
                suggestions.append("Check for blocking operations in the pipeline")
        else:
            findings.append("No FPS metrics found in logs")
            suggestions.append("Ensure FPS logging is enabled in the application")

        # Check queue drops
        if self.metrics.queue_drop_count > 0:
            drop_rate = self.metrics.queue_drop_count / max(self.metrics.frames_processed, 1)
            if drop_rate > self.THRESHOLDS['max_queue_drop_rate']:
                findings.append(
                    f"High queue drop rate: {drop_rate*100:.1f}% "
                    f"({self.metrics.queue_drop_count} drops)"
                )
                suggestions.append("Increase queue capacity or reduce upstream production rate")

        # Check buffer starvation
        if self.metrics.buffer_starvation_count > self.THRESHOLDS['max_buffer_starvation']:
            findings.append(
                f"Buffer starvation: {self.metrics.buffer_starvation_count} occurrences"
            )
            suggestions.append("Increase buffer pool size or reduce frame rate")

        # Check detection confidence
        if self.metrics.detection_confidences:
            avg_conf = sum(self.metrics.detection_confidences) / len(self.metrics.detection_confidences)
            if avg_conf < self.THRESHOLDS['min_detection_confidence']:
                findings.append(
                    f"Low average detection confidence: {avg_conf:.2f} "
                    f"(threshold: {self.THRESHOLDS['min_detection_confidence']})"
                )
                suggestions.append("Review detection threshold or model confidence calibration")

        # Check bounding box counts
        if self.metrics.bounding_box_counts:
            max_dets = max(self.metrics.bounding_box_counts)
            if max_dets > 3:
                findings.append(f"More than 3 detections in a frame: {max_dets}")
                suggestions.append("Review NMS settings or detection limit enforcement")

        # Check memory warnings
        if len(self.metrics.memory_warnings) > 3:
            findings.append(
                f"Multiple memory warnings: {len(self.metrics.memory_warnings)} occurrences"
            )
            suggestions.append("Review memory allocation patterns and buffer pool sizing")

        # Check thread safety issues
        if self.metrics.thread_safety_issues:
            findings.append(
                f"Thread safety issues detected: {len(self.metrics.thread_safety_issues)}"
            )
            for issue in self.metrics.thread_safety_issues[:3]:  # Limit to first 3
                findings.append(f"  - {issue}")
            suggestions.append("Review mutex usage and lock ordering")

        # Check blocking issues
        if self.metrics.blocking_issues:
            findings.append(
                f"Blocking issues detected: {len(self.metrics.blocking_issues)}"
            )
            for issue in self.metrics.blocking_issues[:3]:
                findings.append(f"  - {issue}")
            suggestions.append("Review queue capacities and consumer thread responsiveness")

        # Check TPU temperature
        if self.metrics.tpu_temp_readings:
            max_temp = max(self.metrics.tpu_temp_readings)
            if max_temp > 80:
                findings.append(f"High TPU temperature: {max_temp:.1f}°C")
                suggestions.append("Ensure adequate cooling for Coral Edge TPU")

        # Determine overall status - check for critical errors
        critical_errors = ['TPU', 'initialization', 'timeout', 'PCIe', 'device open']
        has_critical_errors = any(
            any(crit in err.lower() for crit in critical_errors)
            for err in self.metrics.errors
        )

        critical_issues = len([f for f in findings if 'Low FPS' in f or 'TPU' in f or 'initialization' in f])

        if has_critical_errors or critical_issues > 0:
            status = "FAIL"
        elif len(findings) > 0:
            status = "WARN"
        else:
            status = "PASS"

        # Add critical errors to findings
        if self.metrics.errors:
            findings.append(f"Critical errors detected: {len(self.metrics.errors)}")
            for err in self.metrics.errors[:5]:
                findings.append(f"  - {err}")

        # Add positive findings
        if not findings:
            findings.append("All metrics within acceptable ranges")

        return ValidationResult(
            status=status,
            metrics=self.metrics,
            findings=findings,
            suggestions=suggestions
        )


def generate_report(result: ValidationResult, duration: float) -> str:
    """Generate a formatted validation report."""
    lines = []

    lines.append("=" * 60)
    lines.append("RUNTIME VALIDATION REPORT")
    lines.append("=" * 60)
    lines.append(f"Generated: {datetime.now().isoformat()}")
    lines.append(f"Duration: {duration:.1f}s")
    lines.append("")

    # Status
    status_color = {
        "PASS": "\033[92m",  # Green
        "WARN": "\033[93m",  # Yellow
        "FAIL": "\033[91m",  # Red
    }.get(result.status, "")
    reset = "\033[0m"
    lines.append(f"Status: {status_color}{result.status}{reset}")
    lines.append("")

    # Throughput
    lines.append("-" * 40)
    lines.append("THROUGHPUT METRICS")
    lines.append("-" * 40)
    if result.metrics.fps_values:
        lines.append(f"  FPS: min={min(result.metrics.fps_values):.1f}, "
                     f"avg={sum(result.metrics.fps_values)/len(result.metrics.fps_values):.1f}, "
                     f"max={max(result.metrics.fps_values):.1f}")
    else:
        lines.append("  FPS: No data")
    lines.append(f"  Frames processed: {result.metrics.frames_processed}")

    # Pipeline timing
    if result.metrics.capture_times_us:
        avg_capture = sum(result.metrics.capture_times_us) / len(result.metrics.capture_times_us)
        lines.append(f"  Capture time: {avg_capture:.0f} us (avg)")

    if result.metrics.preprocess_times_us:
        avg_preprocess = sum(result.metrics.preprocess_times_us) / len(result.metrics.preprocess_times_us)
        lines.append(f"  Preprocessing: {avg_preprocess:.0f} us (avg)")

    if result.metrics.inference_times_us:
        avg_inference = sum(result.metrics.inference_times_us) / len(result.metrics.inference_times_us)
        lines.append(f"  Inference: {avg_inference:.0f} us ({avg_inference/1000:.1f} ms) (avg)")

    lines.append("")

    # Queue Health
    lines.append("-" * 40)
    lines.append("QUEUE HEALTH")
    lines.append("-" * 40)
    lines.append(f"  Queue drops: {result.metrics.queue_drop_count}")
    lines.append(f"  Buffer starvation: {result.metrics.buffer_starvation_count}")
    lines.append(f"  Overlay queue drops: {result.metrics.overlay_queue_drops}")
    lines.append("")

    # Detection Metrics
    lines.append("-" * 40)
    lines.append("DETECTION METRICS")
    lines.append("-" * 40)
    if result.metrics.bounding_box_counts:
        lines.append(f"  Max detections/frame: {max(result.metrics.bounding_box_counts)}")
        lines.append(f"  Avg detections/frame: "
                     f"{sum(result.metrics.bounding_box_counts)/len(result.metrics.bounding_box_counts):.1f}")
    if result.metrics.detection_confidences:
        lines.append(f"  Confidence: min={min(result.metrics.detection_confidences):.2f}, "
                     f"avg={sum(result.metrics.detection_confidences)/len(result.metrics.detection_confidences):.2f}, "
                     f"max={max(result.metrics.detection_confidences):.2f}")
    lines.append(f"  Ballistic impacts: {result.metrics.ballistic_impacts}")
    lines.append(f"  Safety violations: {result.metrics.safety_violations}")
    if result.metrics.hit_streaks:
        lines.append(f"  Avg hit streak: {sum(result.metrics.hit_streaks)/len(result.metrics.hit_streaks):.1f}")
    lines.append("")

    # Threading
    lines.append("-" * 40)
    lines.append("THREADING HEALTH")
    lines.append("-" * 40)
    lines.append(f"  Thread safety issues: {len(result.metrics.thread_safety_issues)}")
    lines.append(f"  Blocking issues: {len(result.metrics.blocking_issues)}")
    lines.append("")

    # Memory
    lines.append("-" * 40)
    lines.append("MEMORY HEALTH")
    lines.append("-" * 40)
    lines.append(f"  Memory warnings: {len(result.metrics.memory_warnings)}")
    if result.metrics.tpu_temp_readings:
        lines.append(f"  TPU temp: min={min(result.metrics.tpu_temp_readings):.1f}°C, "
                     f"max={max(result.metrics.tpu_temp_readings):.1f}°C")
    lines.append("")

    # Errors
    lines.append("-" * 40)
    lines.append("ERRORS")
    lines.append("-" * 40)
    if result.metrics.errors:
        lines.append(f"  Total errors: {len(result.metrics.errors)}")
        for err in result.metrics.errors[:5]:  # Show first 5
            lines.append(f"    - {err[:80]}")
        if len(result.metrics.errors) > 5:
            lines.append(f"    ... and {len(result.metrics.errors) - 5} more")
    else:
        lines.append("  No errors detected")
    lines.append("")

    # Findings
    lines.append("-" * 40)
    lines.append("FINDINGS")
    lines.append("-" * 40)
    for finding in result.findings:
        lines.append(f"  - {finding}")
    lines.append("")

    # Suggestions
    if result.suggestions:
        lines.append("-" * 40)
        lines.append("SUGGESTIONS")
        lines.append("-" * 40)
        for suggestion in result.suggestions:
            lines.append(f"  - {suggestion}")
        lines.append("")

    lines.append("=" * 60)
    lines.append(f"FINAL STATUS: {result.status}")
    lines.append("=" * 60)

    return '\n'.join(lines)


def find_latest_log(log_dir: str = "/home/pi/Aurore/logs") -> Optional[str]:
    """Find the most recent log file."""
    log_path = Path(log_dir)
    if not log_path.exists():
        return None

    log_files = list(log_path.glob("*.log"))
    if not log_files:
        return None

    latest = max(log_files, key=lambda f: f.stat().st_mtime)
    return str(latest)


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Validate AuroreMkVI runtime execution"
    )
    parser.add_argument(
        "--log-file", "-l",
        help="Path to log file (default: auto-detect latest)"
    )
    parser.add_argument(
        "--duration", "-d",
        type=float,
        default=60.0,
        help="Runtime duration in seconds (default: 60)"
    )
    parser.add_argument(
        "--json", "-j",
        action="store_true",
        help="Output results as JSON"
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Show detailed output"
    )

    args = parser.parse_args()

    # Find log file
    log_file = args.log_file
    if not log_file:
        log_file = find_latest_log()
        if log_file and args.verbose:
            print(f"Auto-detected latest log: {log_file}")

    if not log_file:
        print("Error: No log file found. Specify with --log-file or ensure logs exist in /home/pi/Aurore/logs/")
        sys.exit(1)

    # Read log file
    try:
        with open(log_file, 'r') as f:
            log_content = f.read()
    except Exception as e:
        print(f"Error reading log file: {e}")
        sys.exit(1)

    if not log_content.strip():
        print("Error: Log file is empty")
        sys.exit(1)

    # Parse and validate
    validator = RuntimeValidator(log_content)
    metrics = validator.parse()
    result = validator.validate()

    # Set duration
    metrics.duration_seconds = args.duration

    # Generate report
    report = generate_report(result, args.duration)

    if args.json:
        # Output as JSON
        output = {
            "status": result.status,
            "duration_seconds": args.duration,
            "metrics": {
                "fps_values": metrics.fps_values,
                "frames_processed": metrics.frames_processed,
                "queue_drop_count": metrics.queue_drop_count,
                "buffer_starvation_count": metrics.buffer_starvation_count,
                "bounding_box_counts": metrics.bounding_box_counts,
                "detection_confidences": metrics.detection_confidences,
                "errors_count": len(metrics.errors),
                "warnings_count": len(metrics.warnings),
                "memory_warnings_count": len(metrics.memory_warnings),
                "thread_safety_issues_count": len(metrics.thread_safety_issues),
                "blocking_issues_count": len(metrics.blocking_issues),
            },
            "findings": result.findings,
            "suggestions": result.suggestions
        }
        print(json.dumps(output, indent=2))
    else:
        print(report)

    # Exit with appropriate code
    if result.status == "FAIL":
        sys.exit(1)
    elif result.status == "WARN":
        sys.exit(2)  # Warning status
    else:
        sys.exit(0)


if __name__ == "__main__":
    main()
