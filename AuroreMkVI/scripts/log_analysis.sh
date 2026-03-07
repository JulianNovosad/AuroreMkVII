#!/bin/bash
# Log Analysis Script for Safety Audit
# Analyzes logs for ballistics track sanity, bounding box validity, and 2σ confidence

LOG_DIR="logs"
ANALYSIS_FILE="log_analysis_report.txt"

echo "========================================"
echo "Log Analysis for Safety Audit"
echo "========================================"
echo ""

# Find latest log files
LATEST_LOG=$(ls -t "$LOG_DIR"/*.log 2>/dev/null | head -1)

if [ -z "$LATEST_LOG" ]; then
    echo "No log files found in $LOG_DIR"
    echo "Run the system first to generate logs"
    exit 1
fi

echo "Analyzing: $LATEST_LOG"
echo ""

# Track ID analysis
echo "=== Track ID Analysis ==="
TRACK_IDS=$(grep -oE 'track_id=[0-9]+' "$LATEST_LOG" | cut -d= -f2 | sort -n | uniq -c | sort -rn | head -10)
if [ -n "$TRACK_IDS" ]; then
    echo "Most frequent track IDs:"
    echo "$TRACK_IDS"
    echo ""
fi

# Bounding box analysis
echo "=== Bounding Box Coordinate Analysis ==="
BBOX_ISSUES=$(grep -c "xmin.*xmax\|ymin.*ymax\|NaN\|Inf" "$LATEST_LOG" 2>/dev/null || echo "0")
echo "Potential bbox issues: $BBOX_ISSUES"

# Check for extreme values
EXTREME_VALUES=$(grep -E "distance.*1e[5-9]|1327\s+\*|10\^131" "$LATEST_LOG" 2>/dev/null | wc -l)
echo "Extreme value anomalies: $EXTREME_VALUES"

# TPU latency analysis
echo ""
echo "=== TPU Latency Analysis ==="
TPU_LATENCY=$(grep -oE "tpu_inference_ms=[0-9.]+" "$LATEST_LOG" | cut -d= -f2)
if [ -n "$TPU_LATENCY" ]; then
    TPU_AVG=$(echo "$TPU_LATENCY" | awk '{sum+=$1; count++} END {if(count>0) printf "%.2f", sum/count}')
    TPU_MAX=$(echo "$TPU_LATENCY" | sort -n | tail -1)
    TPU_P95=$(echo "$TPU_LATENCY" | sort -n | awk '{count++} END {if(count>0) print $0}')
    echo "TPU Latency (ms):"
    echo "  Average: $TPU_AVG"
    echo "  Max: $TPU_MAX"
    echo ""
fi

# End-to-end latency analysis
echo "=== End-to-End Latency Analysis ==="
E2E_LATENCY=$(grep -oE "latency=[0-9.]+ms" "$LATEST_LOG" | cut -d= -f1 | tr -d ' ')
if [ -n "$E2E_LATENCY" ]; then
    E2E_AVG=$(echo "$E2E_LATENCY" | awk '{sum+=$1; count++} END {if(count>0) printf "%.2f", sum/count}')
    E2E_MAX=$(echo "$E2E_LATENCY" | sort -n | tail -1)
    E2E_P95=$(echo "$E2E_LATENCY" | sort -n | awk 'BEGIN {c=0} {a[++c]=$0} END {if(c>0) print a[int(c*0.95)]}')
    echo "End-to-End Latency (ms):"
    echo "  Average: $E2E_AVG"
    echo "  Max: $E2E_MAX"
    echo "  P95: $E2E_P95"
    echo ""
fi

# Queue health analysis
echo "=== Queue Health ==="
QUEUE_DROPS=$(grep -oE "dropped=[0-9]+" "$LATEST_LOG" | cut -d= -f2 | awk '{sum+=$1} END {print sum}')
echo "Total queue drops: $QUEUE_DROPS"

# Statistical confidence analysis
echo ""
echo "=== Statistical Confidence ==="
CONFIDENCE=$(grep -oE "confidence=0\.[0-9]+" "$LATEST_LOG" | cut -d= -f2)
if [ -n "$CONFIDENCE" ]; then
    CONF_AVG=$(echo "$CONFIDENCE" | awk '{sum+=$1; count++} END {if(count>0) printf "%.3f", sum/count}')
    CONF_STD=$(echo "$CONFIDENCE" | awk '{sum+=$1; sumsq+=$1*$1} END {if(count>0) printf "%.3f", sqrt(sumsq/count - (sum/count)^2)}')
    echo "Average confidence: $CONF_AVG"
    echo "Std deviation: $CONF_STD"
    echo "2σ confidence interval: $(echo "$CONF_AVG $CONF_STD" | awk '{low=$1-2*$2; high=$1+2*$2; if(low<0) low=0; if(high>1) high=1; printf "[%.3f, %.3f]", low, high}')"
    echo ""
fi

# Safety checks
echo "=== Safety Check Results ==="
FIRE_BLOCKED=$(grep -c "fire_allowed=0\|FIRE_BLOCKED\|Orange Zone" "$LATEST_LOG" 2>/dev/null || echo "0")
FIRE_ALLOWED=$(grep -c "fire_allowed=1\|ACTUATION_START" "$LATEST_LOG" 2>/dev/null || echo "0")
echo "Fire blocked: $FIRE_BLOCKED"
echo "Fire allowed: $FIRE_ALLOWED"

# Generate report
cat > "$ANALYSIS_FILE" << EOF
LOG ANALYSIS REPORT
Generated: $(date)
Analyzed: $LATEST_LOG

SUMMARY
-------
Track IDs: $(echo "$TRACK_IDS" | wc -l) unique IDs
BBox issues: $BBOX_ISSUES
Extreme values: $EXTREME_VALUES
TPU latency (avg): ${TPU_AVG:-N/A} ms
E2E latency (avg): ${E2E_AVG:-N/A} ms
Queue drops: $QUEUE_DROPS
Avg confidence: ${CONF_AVG:-N/A}
Fire blocked: $FIRE_BLOCKED
Fire allowed: $FIRE_ALLOWED

RISK ASSESSMENT
---------------
$(if [ "$EXTREME_VALUES" -gt 0 ] || [ "$TPU_MAX" -gt 20 ] || [ "$E2E_MAX" -gt 30 ]; then
    echo "RISK: ELEVATED"
    echo "  - Extreme values detected: $EXTREME_VALUES"
    echo "  - TPU latency max: ${TPU_MAX:-N/A}ms (>20ms threshold)"
    echo "  - E2E latency max: ${E2E_MAX:-N/A}ms (>30ms threshold)"
else
    echo "RISK: ACCEPTABLE"
    echo "  - No extreme values"
    echo "  - Latency within bounds"
fi)

RECOMMENDATIONS
---------------
1. Monitor TPU latency trends
2. Investigate any extreme values
3. Verify bounding box validity
4. Track queue drop rates
EOF

echo ""
echo "Report saved to: $ANALYSIS_FILE"
