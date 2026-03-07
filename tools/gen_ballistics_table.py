#!/usr/bin/env python3
# tools/gen_ballistics_table.py
# Run: python3 tools/gen_ballistics_table.py --output data/ballistics.bin
"""
Generates the precomputed 4D ballistics lookup table (spec section 7.4).
Not part of the C++ build -- run offline to produce the binary data file.
"""
import argparse, struct, math
from pathlib import Path

G = 9.81
RANGE_BINS   = 20
ELEV_BINS    = 18
VEL_BINS     = 71
DENSITY_BINS = 5

def solve_kinetic(range_m, height_m, vel_m_s):
    if vel_m_s <= 0: return 0, 0, 0
    t = range_m / vel_m_s
    drop = 0.5 * G * t * t + height_m
    el = math.atan2(-drop, range_m) * 180 / math.pi
    az = 0.0
    conf = min(1.0, 0.95 - abs(drop) * 10)
    return int(az * 1000), int(el * 1000), int(max(0, conf) * 255)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="data/ballistics.bin")
    args = parser.parse_args()

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)

    with open(args.output, 'wb') as f:
        for ri in range(RANGE_BINS):
            range_m = 0.1 + ri * 0.1
            for ei in range(ELEV_BINS):
                el_deg = -90 + ei * 10
                for vi in range(VEL_BINS):
                    vel = vi * 10
                    for di in range(DENSITY_BINS):
                        if vel == 0:
                            az, el, conf = 0, 0, 0
                        else:
                            height_m = math.tan(math.radians(el_deg)) * range_m
                            az, el, conf = solve_kinetic(range_m, height_m, vel)
                        f.write(struct.pack('<hhB', az, el, conf))

    size_kb = Path(args.output).stat().st_size / 1024
    print(f"Generated {args.output} ({size_kb:.0f} KB)")

if __name__ == "__main__":
    main()
