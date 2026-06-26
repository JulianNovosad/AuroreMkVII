#!/bin/bash
# measure_latency.sh - Measure end-to-end latency of Aurore MkVII
#
# Measures latency across all pipeline stages:
#   Camera Capture → Vision Pipeline → Track Compute → Actuation Output
#
# Usage: ./scripts/measure_latency.sh [--samples=N] [--output=DIR]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build-release"
SAMPLES=100000
OUTPUT_DIR="./latency_results"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --samples=*)
            SAMPLES="${1#*=}"
            shift
            ;;
        --output=*)
            OUTPUT_DIR="${1#*=}"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "=== Aurore MkVII End-to-End Latency Measurement ==="
echo "Samples: $SAMPLES"
echo "Output:  $OUTPUT_DIR"
echo ""

# Check if build exists, build if needed
if [[ ! -f "$BUILD_DIR/aurore_latency_measurement" ]]; then
    echo "Building latency measurement tool..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . --target aurore_latency_measurement -j$(nproc)
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Run measurement
echo ""
echo "Running latency measurement (this may take a while)..."
"$BUILD_DIR/aurore_latency_measurement" \
    --samples=$SAMPLES \
    --output="$OUTPUT_DIR/latency_samples.csv" \
    --verbose

# Generate analysis report
echo ""
echo "Generating latency analysis report..."

if command -v python3 &> /dev/null; then
    cat > "$OUTPUT_DIR/analyze_latency.py" << 'PYTHON_SCRIPT'
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy import stats
import sys

# Load data
csv_file = sys.argv[1] if len(sys.argv) > 1 else 'latency_samples.csv'
df = pd.read_csv(csv_file)

print(f"\n=== End-to-End Latency Analysis ===")
print(f"Samples: {len(df):,}")

# Define stages
stages = ['camera_capture', 'vision_done', 'track_done', 'actuation_done']
stage_labels = ['Camera Capture', 'Vision Pipeline', 'Track Compute', 'Actuation Output']

# Calculate stage-to-stage latencies
for i in range(len(stages) - 1):
    col_name = f'{stages[i+1]}_us'
    if col_name in df.columns:
        latency = df[col_name] / 1000.0  # Convert to microseconds
        print(f"\n--- {stage_labels[i]} → {stage_labels[i+1]} ---")
        print(f"  Min:     {latency.min():.1f} µs")
        print(f"  Max:     {latency.max():.1f} µs")
        print(f"  Mean:    {latency.mean():.1f} µs")
        print(f"  Median:  {latency.median():.1f} µs")
        print(f"  Std Dev: {latency.std():.1f} µs")
        print(f"  P50:     {np.percentile(latency, 50):.1f} µs")
        print(f"  P90:     {np.percentile(latency, 90):.1f} µs")
        print(f"  P99:     {np.percentile(latency, 99):.1f} µs")
        print(f"  P99.9:   {np.percentile(latency, 99.9):.1f} µs")

# End-to-end latency
if 'end_to_end_us' in df.columns:
    e2e = df['end_to_end_us'] / 1000.0  # Convert to microseconds
    print(f"\n=== END-TO-END LATENCY (Camera → Actuation) ===")
    print(f"  Min:     {e2e.min():.1f} µs ({e2e.min()/1000:.2f} ms)")
    print(f"  Max:     {e2e.max():.1f} µs ({e2e.max()/1000:.2f} ms)")
    print(f"  Mean:    {e2e.mean():.1f} µs ({e2e.mean()/1000:.2f} ms)")
    print(f"  Median:  {e2e.median():.1f} µs ({e2e.median()/1000:.2f} ms)")
    print(f"  Std Dev: {e2e.std():.1f} µs")
    print(f"  P50:     {np.percentile(e2e, 50):.1f} µs")
    print(f"  P90:     {np.percentile(e2e, 90):.1f} µs")
    print(f"  P95:     {np.percentile(e2e, 95):.1f} µs")
    print(f"  P99:     {np.percentile(e2e, 99):.1f} µs")
    print(f"  P99.9:   {np.percentile(e2e, 99.9):.1f} µs")

    # Spec check
    spec_ms = 5.0  # WCET spec is ≤5ms per AGENTS.md
    p99_ms = np.percentile(e2e, 99) / 1000.0
    print(f"\n  Spec (≤{spec_ms}ms): {'PASS' if p99_ms <= spec_ms else 'FAIL'}")
    print(f"  P99: {p99_ms:.2f} ms")

# Jitter analysis
if 'end_to_end_us' in df.columns:
    e2e = df['end_to_end_us'] / 1000.0
    jitter = e2e.diff().dropna()
    print(f"\n=== JITTER ANALYSIS (Cycle-to-Cycle) ===")
    print(f"  Min:     {jitter.min():.1f} µs")
    print(f"  Max:     {jitter.max():.1f} µs")
    print(f"  Mean:    {jitter.mean():.1f} µs")
    print(f"  Std Dev: {jitter.std():.1f} µs")
    print(f"  Max Abs: {jitter.abs().max():.1f} µs")

# Generate plots
print(f"\nGenerating plots...")
plt.figure(figsize=(16, 10))

# Plot 1: End-to-end latency histogram
plt.subplot(2, 3, 1)
plt.hist(e2e, bins=100, log=True, alpha=0.7, color='blue')
plt.xlabel('End-to-End Latency (µs)')
plt.ylabel('Frequency (log scale)')
plt.title('End-to-End Latency Distribution')
plt.grid(True, alpha=0.3)
plt.axvline(e2e.mean(), color='red', linestyle='--', label=f'Mean: {e2e.mean():.0f}µs')
plt.axvline(np.percentile(e2e, 99), color='orange', linestyle='--', label=f'P99: {np.percentile(e2e, 99):.0f}µs')
plt.legend()

# Plot 2: End-to-end CDF
plt.subplot(2, 3, 2)
sorted_e2e = np.sort(e2e)
percentiles = np.linspace(0, 100, len(sorted_e2e))
plt.plot(sorted_e2e, percentiles, color='blue')
plt.xlabel('End-to-End Latency (µs)')
plt.ylabel('Percentile')
plt.title('End-to-End CDF')
plt.grid(True, alpha=0.3)
plt.xlim(sorted_e2e[int(len(sorted_e2e)*0.9):])

# Plot 3: Stage latencies over time
plt.subplot(2, 3, 3)
for stage in stages[1:]:
    col_name = f'{stage}_us'
    if col_name in df.columns:
        plt.plot(df.index, df[col_name] / 1000.0, label=stage.replace('_', ' ').title(), alpha=0.7)
plt.xlabel('Sample Number')
plt.ylabel('Latency (µs)')
plt.title('Stage Latencies Over Time')
plt.legend()
plt.grid(True, alpha=0.3)

# Plot 4: Jitter histogram
plt.subplot(2, 3, 4)
plt.hist(jitter, bins=100, alpha=0.7, color='green')
plt.xlabel('Jitter (µs)')
plt.ylabel('Frequency')
plt.title('Cycle-to-Cycle Jitter')
plt.grid(True, alpha=0.3)

# Plot 5: End-to-end over time
plt.subplot(2, 3, 5)
plt.plot(df.index, e2e, alpha=0.7, color='blue')
plt.xlabel('Sample Number')
plt.ylabel('End-to-End Latency (µs)')
plt.title('End-to-End Latency Over Time')
plt.grid(True, alpha=0.3)

# Plot 6: Box plot of all stages
plt.subplot(2, 3, 6)
stage_data = []
stage_labels_short = []
for stage in stages[1:]:
    col_name = f'{stage}_us'
    if col_name in df.columns:
        stage_data.append(df[col_name] / 1000.0)
        stage_labels_short.append(stage.replace('_', ' ').title())
if stage_data:
    plt.boxplot(stage_data, labels=stage_labels_short)
    plt.ylabel('Latency (µs)')
    plt.title('Stage Latency Comparison')
    plt.xticks(rotation=45)
    plt.grid(True, alpha=0.3)

plt.tight_layout()
plt.savefig(f'{OUTPUT_DIR}/latency_analysis.png', dpi=150)
print(f"Plot saved to: {OUTPUT_DIR}/latency_analysis.png")

PYTHON_SCRIPT

    python3 "$OUTPUT_DIR/analyze_latency.py" "$OUTPUT_DIR/latency_samples.csv"
else
    echo "Python3 not available - skipping detailed analysis"
    echo "Raw data saved to: $OUTPUT_DIR/latency_samples.csv"
fi

echo ""
echo "=== Measurement Complete ==="
echo "Results saved to: $OUTPUT_DIR/"
echo "  - latency_samples.csv: Raw measurement data"
echo "  - latency_analysis.png: Visualization plots"
echo ""
echo "To view the CSV:"
echo "  head -20 $OUTPUT_DIR/latency_samples.csv"
