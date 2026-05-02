# M01 Laser Rangefinder - Complete Guide
# ============================================================================

# QUICK START (TL;DR)
# ============================================================================
# Hardware: Liancheng Electronics M01 50m laser rangefinder
# Connect: UART to /dev/ttyAMA0 (RPi GPIO 14/15) at 9600 baud, 8N1
# Commands: "L\r" (laser on), "D\r" (continuous mode), "K\r" (laser off)
# Read distance: Combine bytes[5:6] as big-endian uint16 → MILLIMETERS

# IMPORTANT: HOW TO DECODE DISTANCE
# ============================================================================

# THE PROBLEM (what caused the bug):
# The original code used BCD (Binary Coded Decimal) decoding which gave
# WRONG results. The correct method is simple uint16-BE in millimeters.

# THE FIX (super simple, for a kindergarten kid):
# 1. Read two bytes: byte[5] and byte[6]
# 2. Put them together: (byte[5] << 8) | byte[6]
# 3. That's the distance in MILLIMETERS!
# 4. Divide by 1000 to get meters.

# EXAMPLE (what you actually see):
# Frame bytes: EE 00 00 00 00 01 00 02 03
#                        ^    ^
#                        |    +-- byte[6] = 0x00
#                        +------- byte[5] = 0x01
# Calculation: (0x01 << 8) | 0x00 = 256
# Result: 256mm = 25.6cm = 0.256m ✓ (matches physical measurement of ~23cm!)

# WRONG WAY (what the code used to do - BCD decoding):
# byte[5]=0x01, byte[6]=0x00
# BCD: "0100" = 100cm = 1.0m ✗ (WAY off!)

# RIGHT WAY (uint16-BE mm):
# byte[5]=0x01, byte[6]=0x00
# uint16: (0x01 << 8) | 0x00 = 256mm = 0.256m ✓

# ============================================================================
# FRAME FORMATS
# ============================================================================

# 0xEE Status Frame (9 bytes) - most common:
# +------+------+------+------+------+------+------+----+------+
# | 0xEE | 0x00 | 0x00 | 0x00 | 0x00 | hi   | lo   | ?? | CHK  |
# +------+------+------+------+------+------+------+----+------+
# Byte:   0     1      2      3      4      5     6     7    8
#                                                          ^
#                                                   distance here! (bytes 5-6)
# Distance: (byte[5] << 8) | byte[6] → millimeters

# 0xAA Data Frame (13 bytes):
# +------+------+------+------+------+------+------+------+
# | 0xAA | 0x00 | 0x00 | func | 0x00 | 0x04 | hi   | lo   | ...
# +------+------+------+------+------+------+------+------+
# Byte:    0     1      2      3     4      5     6      7     8
#                                                        ^
#                                              distance here! (bytes 7-8)

# ============================================================================
# COMMUNICATION
# ============================================================================

# ASCII Commands (most reliable):
# 1. Send "L\r" → turns on the laser
# 2. Send "D\r" → starts continuous distance mode
# 3. LRF then sends frames automatically every ~100ms
# 4. Send "K\r" → turns off the laser

# Binary Commands (some modules support):
# 0xAA 0x00 0x01 0xBE 0x00 0x01 0x00 0x01 0xC1 → Laser ON
# 0xAA 0x00 0x00 0x21 0x00 0x01 0x00 0x00 0x22 → Continuous mode

# ============================================================================
# CHECKSUM
# ============================================================================

# M01 checksum: sum of all bytes from index 1 to N-1, then & 0xFF
# Should match the last byte of the frame

# ============================================================================
# WIRING (M01 6-pin module)
# ============================================================================

# Pinout (top to bottom):
#   MIN (3.3V) - red wire
#   ENA - enable (active HIGH, can tie to 3.3V)
#   GND - black wire (common ground!)
#   RXD - green wire (module RX, connect to MCU TX)
#   TXD - yellow wire (module TX, connect to MCU RX)
#   NC - not connected

# On Raspberry Pi 5:
#   /dev/ttyAMA0 on GPIO 14 (TX) and GPIO 15 (RX)
#   NOTE: This is NOT the mini-UART (/dev/ttyS0)!

# ============================================================================
# TROUBLESHOOTING
# ============================================================================

# Q: No data received?
# A: Check wiring (TX→RX cross!), check ground connection, verify 3.3V power

# Q: Readings are exactly 1.000m or 10.000m?
# A: Code is using BCD decoding - fix to uint16-BE mm (see THE FIX above)

# Q: Readings are 0.256m but target is ~23cm?
# A: That's actually correct! 0x01 0x00 = 256mm = 25.6cm (within ±3cm of 23cm)

# Q: Readings fluctuate wildly?
# A: Check target reflectivity (use matte white target), ensure stable power

# Q: Module doesn't respond to ASCII commands?
# A: Some modules need binary commands instead. Try 0xAA 0x00 0x01 0xBE...

# ============================================================================
# TESTING
# ============================================================================

# Run the LRF test:
#   cd build-debug && ./lrf_20_samples_test

# Expected output (for ~23cm target):
#   Mean: 0.256m  (or similar, within ±5cm of physical measurement)

# ============================================================================
# REFERENCE
# ============================================================================

# Original Arduino/ESP32 reference: github.com/Andres-ros/laser-m01-esp32
# Module: Liancheng Electronics M01 50m laser rangefinder (AliExpress)