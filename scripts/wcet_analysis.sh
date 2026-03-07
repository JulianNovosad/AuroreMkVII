#!/bin/bash
# wcet_analysis.sh - WCET measurement and analysis script
#
# Usage: ./scripts/wcet_analysis.sh [options]
#
# Options:
#   --samples=N    Number of samples (default: 1000000)
#   --output=DIR   Output directory (default: ./wcet_results)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
OUTPUT_DIR="./wcet_results"
SAMPLES=1000000

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

echo "=== Aurore MkVII WCET Analysis ==="
echo "Samples: $SAMPLES"
echo "Output:  $OUTPUT_DIR"
echo ""

# Check if build exists
if [[ ! -f "$BUILD_DIR/aurore_wcet_measurement" ]]; then
    echo "Building WCET measurement tool..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . --target aurore_wcet_measurement -j$(nproc)
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Run WCET measurement
echo ""
echo "Running WCET measurement..."
"$BUILD_DIR/aurore_wcet_measurement" \
    --samples=$SAMPLES \
    --output="$OUTPUT_DIR/wcet_samples.csv" \
    --verbose

# Generate report
echo ""
echo "Generating analysis report..."

# Use Python if available for plotting
if command -v python3 &> /dev/null; then
    cat > "$OUTPUT_DIR/analyze.py" << 'PYTHON_SCRIPT'
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy import stats
import sys

# Load data
df = pd.read_csv(sys.argv[1] if len(sys.argv) > 1 else 'wcet_samples.csv')
samples = df['execution_time_ns'].values

# Basic statistics
print(f"\n=== Statistical Analysis ===")
print(f"Samples: {len(samples):,}")
print(f"Min:     {samples.min():,} ns")
print(f"Max:     {samples.max():,} ns")
print(f"Mean:    {samples.mean():,.0f} ns")
print(f"Median:  {np.median(samples):,} ns")
print(f"Std Dev: {samples.std():,.0f} ns")

# Percentiles
print(f"\n=== Percentiles ===")
print(f"P50:     {np.percentile(samples, 50):,} ns")
print(f"P90:     {np.percentile(samples, 90):,} ns")
print(f"P99:     {np.percentile(samples, 99):,} ns")
print(f"P99.9:   {np.percentile(samples, 99.9):,} ns")
print(f"P99.99:  {np.percentile(samples, 99.99):,} ns")

# WCET estimate (P99.99 + 10% margin)
wcet = np.percentile(samples, 99.99) * 1.1
print(f"\n=== WCET Estimate ===")
print(f"P99.99 + 10% margin: {wcet:,.0f} ns ({wcet/1e6:.2f} ms)")
print(f"Requirement (≤5ms):  {'PASS' if wcet <= 5e6 else 'FAIL'}")

# Histogram
plt.figure(figsize=(12, 8))

plt.subplot(2, 2, 1)
plt.hist(samples, bins=100, log=True, alpha=0.7)
plt.xlabel('Execution Time (ns)')
plt.ylabel('Frequency (log scale)')
plt.title('Execution Time Distribution')
plt.grid(True, alpha=0.3)

plt.subplot(2, 2, 2)
sorted_samples = np.sort(samples)
percentiles = np.linspace(0, 100, len(samples))
plt.plot(sorted_samples, percentiles)
plt.xlabel('Execution Time (ns)')
plt.ylabel('Percentile')
plt.title('CDF')
plt.grid(True, alpha=0.3)
plt.xlim(sorted_samples[int(len(samples)*0.9):])

plt.subplot(2, 2, 3)
tail = samples[samples > np.percentile(samples, 99)]
plt.hist(tail, bins=100, log=True, alpha=0.7)
plt.xlabel('Execution Time (ns)')
plt.ylabel('Frequency (log scale)')
plt.title('Tail Distribution (>P99)')
plt.grid(True, alpha=0.3)

plt.subplot(2, 2, 4)
jitter = np.diff(samples)
plt.hist(jitter, bins=100, alpha=0.7)
plt.xlabel('Jitter (ns)')
plt.ylabel('Frequency')
plt.title('Cycle-to-Cycle Jitter')
plt.grid(True, alpha=0.3)

plt.tight_layout()
plt.savefig('wcet_analysis.png', dpi=150)
print(f"\nPlot saved to: wcet_analysis.png")

# Extreme value analysis
print(f"\n=== Extreme Value Analysis ===")
# Fit Generalized Extreme Value distribution
block_size = 1000
block_maxima = [samples[i:i+block_size].max() for i in range(0, len(samples), block_size)]
shape, loc, scale = stats.genextreme.fit(block_maxima)
print(f"GEV shape parameter: {shape:.4f}")
print(f"GEV location: {loc:.0f} ns")
print(f"GEV scale: {scale:.0f} ns")

# Return level plot
return_periods = [10, 100, 1000, 10000]
print(f"\n=== Return Levels ===")
for rp in return_periods:
    level = stats.genextreme.ppf(1 - 1/rp, shape, loc, scale)
    print(f"{rp}-sample return level: {level:,.0f} ns")
PYTHON_SCRIPT

    python3 "$OUTPUT_DIR/analyze.py" "$OUTPUT_DIR/wcet_samples.csv"
else
    echo "Python3 not available - skipping detailed analysis"
fi

echo ""
echo "=== Analysis Complete ==="
echo "Results saved to: $OUTPUT_DIR/"
