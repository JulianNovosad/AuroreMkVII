# LRF Calibration Issue Report

## Summary
The M01 laser rangefinder is returning inconsistent readings. The calibration factor of 2.25 does not work across all distances.

## Test Data

| Actual Distance | Raw Value | Calculated (÷2.25) | Reading |
|-----------------|------------|---------------------|---------|
| 0.73 m | ~1651 | 734 mm | 0.734m ✓ |
| 3.0 m | ~6752 | 3001 mm | 3.001m ✓ |
| 3.94 m (wall) | ~8932 | 3970 mm | 3.974m ✓ |
| 3.2 m (actual) | ~8932 | 3970 mm | 3.974m ✗ |

## Analysis
- At 0.73m: Works correctly
- At 3.0m: Works correctly  
- At 3.2m: Reads ~3.97m instead of 3.2m (error: +0.77m)

The raw value is approximately the same for 3.94m wall and 3.2m target, suggesting:
1. The raw value may not be changing with target distance, OR
2. The conversion formula is non-linear, OR
3. Wrong offset being used for distance data

## Code Location
`src/drivers/laser_rangefinder.cpp` - `reader_loop_m01()` function

## Current Implementation
```cpp
const uint32_t raw = (static_cast<uint32_t>(buf[kM01DistOffset]) << 8) |
                     buf[kM01DistOffset + 1];
const uint32_t mm = static_cast<uint32_t>(static_cast<float>(raw) / 2.25f);
```

## Notes
- Need to investigate M01 protocol documentation or test different offsets
