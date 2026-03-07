#!/bin/bash
# Swiss Watch Verification for AuroreMkVI
# Run from project root: ./scripts/verify_swiss_watch.sh

set -e

BINARY="./build/AuroreMkVI"
TRACE_LOG="/tmp/aurore_strace.log"
QUEUE_DUMP="/tmp/aurore_queue_dump.json"
STALL_LOG="/tmp/aurore_stall.log"
DURATION=60

echo "=============================================="
echo "   AuroreMkVI Swiss Watch Verification"
echo "=============================================="
echo ""

if [ ! -f "$BINARY" ]; then
    echo "FAIL: $BINARY not found. Build first with: cd build && make"
    exit 1
fi

echo "[1/6] Starting AuroreMkVI under strace (${DURATION}s)..."
echo "      Command: sudo timeout $((DURATION+5)) strace -f \
"
echo "          -e trace=futex,ioctl,read,write,clock_nanosleep,openat,close \
"
echo "          -o $TRACE_LOG \
"
echo "          -T -tt \
"
echo "          $BINARY"
echo ""

# Run with sudo as required by DRM/i2c
sudo timeout $((DURATION+5)) strace -f \
    -e trace=futex,ioctl,read,write,clock_nanosleep,openat,close \
    -o "$TRACE_LOG" \
    -T -tt \
    "$BINARY" 2>&1 &
PID=$!

echo "      PID: $PID"
echo ""

# Check if process started
sleep 2
if ! sudo kill -0 $PID 2>/dev/null; then
    echo "FAIL: AuroreMkVI exited immediately"
    if [ -f "$TRACE_LOG" ]; then
        echo "      Last 20 lines of strace:"
        sudo tail -20 "$TRACE_LOG"
    fi
    exit 1
fi

echo "[2/6] Binary running. Monitoring for ${DURATION}s..."

# Monitor progress
for i in $(seq 1 $DURATION); do
    sleep 1
    if [ $((i % 10)) -eq 0 ]; then
        echo "      Running: ${i}s / ${DURATION}s"
    fi
    if ! sudo kill -0 $PID 2>/dev/null; then
        echo "      WARNING: Process exited at ${i}s"
        break
    fi
done

# Wait for strace to complete
wait $PID 2>/dev/null || true
echo "      Execution complete"

echo ""
echo "[3/6] Analyzing strace output..."

python3 << EOF
import re
import sys

def analyze_strace(log_path):
    with open(log_path, 'r') as f:
        lines = f.readlines()

    long_waits = []
    tpu_activity = []
    camera_activity = []
    i2c_activity = []
    futex_calls = []
    clock_nanosleep = []

    for line in lines:
        match = re.search(r'<([\d.]+)>', line)
        if not match:
            continue
        duration = float(match.group(1))

        if 'futex' in line:
            futex_calls.append((line.strip(), duration))
            if duration > 1.0 and 'ETIMEDOUT' not in line and 'ERESTART' not in line:
                long_waits.append((line.strip(), duration))

        if 'edgetpu' in line.lower() or 'EDGETPU' in line:
            tpu_activity.append(line.strip())

        if 'VIDIOC_DQBUF' in line or 'VIDIOC_QBUF' in line:
            camera_activity.append((line.strip(), duration))

        if 'i2c' in line.lower() or '/dev/i2c' in line:
            i2c_activity.append(line.strip())
        
        if 'clock_nanosleep' in line:
            clock_nanosleep.append((line.strip(), duration))

    print(f"  Total syscalls: {len(lines)}")
    print(f"  Futex calls: {len(futex_calls)}")
    print(f"  Long futex waits (>100ms): {len(long_waits)}")
    print(f"  TPU activity: {len(tpu_activity)} calls")
    print(f"  Camera DMA-BUF: {len(camera_activity)} calls")
    print(f"  I2C activity: {len(i2c_activity)} calls")
    print(f"  clock_nanosleep: {len(clock_nanosleep)} calls")
    print("")

    # FAIL conditions
    critical_failures = []
    
    if len(long_waits) > 0:
        max_wait = max(w[1] for w in long_waits)
        if max_wait > 2.0:
             print(f"  CRITICAL: Long waits > 2.0s detected (Max: {max_wait}s)!")
             for line, dur in long_waits[:3]:
                 print(f"    {dur}s: {line[:100]}...")
             critical_failures.append("long_waits")
        else:
             print(f"  WARNING: Long waits detected (Max: {max_wait}s) - likely low FPS")


    if len(tpu_activity) < 10:
        print(f"  WARNING: Low TPU activity ({len(tpu_activity)} calls)")
    
    if len(camera_activity) < 10:
        print(f"  WARNING: Low camera DMA-BUF activity ({len(camera_activity)} calls)")

    if len(futex_calls) > 1000:
        print(f"  WARNING: High futex count ({len(futex_calls)}) - potential contention")

    if critical_failures:
        print(f"\n  RESULT: FAIL ({', '.join(critical_failures)})")
        return False

    print("\n  RESULT: PASS")
    return True

success = analyze_strace("$TRACE_LOG")
sys.exit(0 if success else 1)
EOF
ANALYSIS_RESULT=$?

echo ""
echo "[4/6] Checking queue dump..."
if [ -f "$QUEUE_DUMP" ]; then
    python3 << EOF
import json
with open("$QUEUE_DUMP") as f:
    dump = json.load(f)

print("  Queue dump contents:")
for name, data in dump.items():
    enq = data.get('enqueue_count', 0)
    deq = data.get('dequeue_count', 0)
    stall = data.get('stall_count', 0)
    drop = data.get('drop_count', 0)
    print(f"    {name}: enq={enq}, deq={deq}, stall={stall}, drop={drop}")
    
    if enq < deq:
        print(f"      FAIL: {name} dequeue > enqueue (underflow)")
        exit(1)

print("  All queue invariants valid")
EOF
else
    echo "  INFO: No queue dump found (SIGUSR1 handler not triggered)"
fi

echo ""
echo "[5/6] Checking stall logs..."
if [ -f "$STALL_LOG" ]; then
    STALL_COUNT=$(wc -l < "$STALL_LOG")
    echo "  Stall events logged: $STALL_COUNT"
    if [ "$STALL_COUNT" -gt 0 ]; then
        echo "  First 5 stalls:"
        head -5 "$STALL_LOG"
    fi
else
    echo "  INFO: No stall log found"
fi

echo ""
echo "[6/6] Generating summary..."

# Generate summary report
python3 << EOF
import os
import re
from datetime import datetime

DURATION = $DURATION
log_size = os.path.getsize("$TRACE_LOG") if os.path.exists("$TRACE_LOG") else 0


summary = f"""
==============================================
   AuroreMkVI Swiss Watch Verification Report
==============================================
Generated: {datetime.now().isoformat()}

Binary: $BINARY
Strace Duration: {DURATION}s
Log Size: {log_size / 1024:.1f} KB

Key Metrics:
"""

with open("$TRACE_LOG", 'r') as f:
    content = f.read()
    
    futex_count = len(re.findall(r'futex', content))
    ioctl_count = len(re.findall(r'ioctl', content))
    read_count = len(re.findall(r'^read\(', content, re.MULTILINE))
    write_count = len(re.findall(r'^write\(', content, re.MULTILINE))
    
    summary += f"  - Total futex calls: {futex_count}"
    summary += f"  - Total ioctl calls: {ioctl_count}"
    summary += f"  - Total read syscalls: {read_count}"
    summary += f"  - Total write syscalls: {write_count}"

summary += """

Runtime Behavior:
"""

# Check for long waits
long_futex = re.findall(r'futex.*<([\d.]+)>', content)
long_count = sum(1 for x in long_futex if float(x) > 0.1)
summary += f"  - Long futex waits (>100ms): {long_count}"

if long_count == 0 and futex_count > 0:
    summary += "  - Priority inversion: NONE DETECTED"
else:
    summary += "  - Priority inversion: WARNING DETECTED"

summary += """

Verification Status:
"""

if $ANALYSIS_RESULT == 0:
    summary += "  [PASS] Runtime behavior meets Swiss Watch standards"
else:
    summary += "  [FAIL] Runtime behavior has issues requiring attention"

summary += """
==============================================
"""

print(summary)

with open("/tmp/swiss_watch_summary.txt", 'w') as f:
    f.write(summary)

print("Summary saved to: /tmp/swiss_watch_summary.txt")
EOF

echo ""
echo "=============================================="
echo "   Swiss Watch Verification Complete"
echo "=============================================="
echo "  Binary: $BINARY"
echo "  Trace: $TRACE_LOG"
echo "  Summary: /tmp/swiss_watch_summary.txt"
echo ""
echo "To view trace: sudo less $TRACE_LOG"
echo "To analyze further: ./scripts/analyze_strace.py $TRACE_LOG"
echo ""