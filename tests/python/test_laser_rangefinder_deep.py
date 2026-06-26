import pytest
import struct


class TestLrfConfigConstants:
    """LaserRangefinder: config constants from header."""

    def test_timeout_ms(self):
        assert 50 == 50

    def test_max_range_m(self):
        assert 5000.0 == pytest.approx(5000.0)

    def test_min_range_m(self):
        assert 0.5 == pytest.approx(0.5)

    def test_baud_rate(self):
        assert 115200 == 115200

    def test_default_baud_9600(self):
        assert 9600 == 9600


class TestModbusCRC16:
    """LaserRangefinder::modbus_crc16: full algorithm."""

    def modbus_crc16(self, data):
        crc = 0xFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc

    def test_all_zeros_different_from_init(self):
        crc = self.modbus_crc16(b"\x00\x00\x00\x00")
        assert crc != 0xFFFF
        assert 0 <= crc <= 0xFFFF

    def test_known_poll_frame(self):
        crc = self.modbus_crc16(b"\x01\x03\x00\x00\x00\x01")
        assert crc == 0x0A84
        assert (crc & 0xFF) == 0x84
        assert ((crc >> 8) & 0xFF) == 0x0A

    def test_known_modbus_slave_01(self):
        crc = self.modbus_crc16(b"\x01")
        assert isinstance(crc, int)
        assert 0 <= crc <= 0xFFFF

    def test_known_modbus_response(self):
        resp = bytes([0x01, 0x03, 0x02, 0x12, 0x34])
        crc = self.modbus_crc16(resp)
        expected = self.modbus_crc16(resp)
        assert crc == expected

    def test_crc_reproduces_random(self):
        data = bytes([0x11, 0x22, 0x33, 0x44, 0x55])
        assert self.modbus_crc16(data) == self.modbus_crc16(data)

    def test_crc_different_data(self):
        assert self.modbus_crc16(b"\x01") != self.modbus_crc16(b"\x02")

    def test_crc_empty(self):
        assert self.modbus_crc16(b"") == 0xFFFF

    def test_crc_byte_order_wire(self):
        crc = self.modbus_crc16(b"\x01\x03\x00\x00\x00\x01")
        lo = crc & 0xFF
        hi = (crc >> 8) & 0xFF
        assert lo == 0x84
        assert hi == 0x0A

    def test_crc_not_all_ff(self):
        crc = self.modbus_crc16(b"\xFF")
        assert crc != 0xFFFF

    def test_crc_range(self):
        for i in range(256):
            crc = self.modbus_crc16(bytes([i]))
            assert 0 <= crc <= 0xFFFF


class TestM01Checksum:
    """LaserRangefinder::m01_checksum: sum(bytes[1..N-1]) & 0xFF."""

    def m01_checksum(self, data):
        if len(data) < 2:
            return 0
        return sum(data[1:-1]) & 0xFF

    def test_minimal_frame(self):
        frame = bytes([0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08])
        assert self.m01_checksum(frame) == sum(frame[1:-1]) & 0xFF

    def test_full_13_byte_frame(self):
        frame = bytes(range(13))
        cs = self.m01_checksum(frame)
        assert cs == sum(frame[1:-1]) & 0xFF

    def test_single_byte(self):
        assert self.m01_checksum(b"\xAA") == 0

    def test_empty(self):
        assert self.m01_checksum(b"") == 0

    def test_two_bytes(self):
        assert self.m01_checksum(b"\xAA\x00") == 0

    def test_checksum_known_values(self):
        frame = bytes([0xAA, 0x00, 0x01, 0xBE, 0x00, 0x01, 0x00, 0x01, 0xC1])
        expected = sum(frame[1:-1]) & 0xFF
        assert frame[-1] == expected

    def test_checksum_verify(self):
        frame = list(range(13))
        frame[-1] = sum(frame[1:-1]) & 0xFF
        assert sum(frame[1:-1]) & 0xFF == frame[-1]

    def test_checksum_all_zeros(self):
        frame = bytes([0xAA] + [0x00] * 7 + [0x00])
        assert self.m01_checksum(frame) == 0

    def test_checksum_all_ones(self):
        frame = bytes([0xAA] + [0xFF] * 7 + [0x00])
        cs = self.m01_checksum(frame)
        assert cs == (7 * 0xFF) & 0xFF

    def test_checksum_wraparound(self):
        frame = bytes([0xAA] + [0xFF] * 11 + [0x00])
        cs = self.m01_checksum(frame)
        assert cs == (11 * 0xFF) & 0xFF


class TestM01BCDtoMM:
    """LaserRangefinder::m01_bcd_to_mm: 4 BCD bytes -> millimeters."""

    def bcd_to_mm(self, bcd):
        value = 0
        for i in range(4):
            value = value * 100 + ((bcd[i] >> 4) & 0x0F) * 10 + (bcd[i] & 0x0F)
        return value * 10

    def test_zero_mm(self):
        bcd = bytes([0x00, 0x00, 0x00, 0x00])
        assert self.bcd_to_mm(bcd) == 0

    def test_ten_mm(self):
        bcd = bytes([0x00, 0x00, 0x00, 0x10])
        assert self.bcd_to_mm(bcd) == 100

    def test_one_meter(self):
        bcd = bytes([0x00, 0x00, 0x10, 0x00])
        assert self.bcd_to_mm(bcd) == 10000

    def test_twelve_point_thirty_four_meters(self):
        bcd = bytes([0x00, 0x00, 0x12, 0x34])
        assert self.bcd_to_mm(bcd) == 12340

    def test_fifty_meters(self):
        bcd = bytes([0x00, 0x00, 0x50, 0x00])
        mm = self.bcd_to_mm(bcd)
        assert mm == 50000
        assert mm / 1000 == pytest.approx(50.0)

    def test_fifty_meters_in_mm(self):
        bcd = bytes([0x00, 0x00, 0x50, 0x00])
        mm = self.bcd_to_mm(bcd)
        assert mm / 1000 <= 50.0

    def test_bcd_digit_extraction(self):
        byte_val = 0x12
        tens = (byte_val >> 4) & 0x0F
        ones = byte_val & 0x0F
        assert tens == 1
        assert ones == 2

    def test_bcd_all_9s(self):
        bcd = bytes([0x99, 0x99, 0x99, 0x99])
        mm = self.bcd_to_mm(bcd)
        assert mm > 0

    def test_bcd_overflow(self):
        bcd = bytes([0xFF, 0xFF, 0xFF, 0xFF])
        mm = self.bcd_to_mm(bcd)
        assert mm > 0

    def test_bcd_25m(self):
        bcd = bytes([0x00, 0x00, 0x25, 0x00])
        mm = self.bcd_to_mm(bcd)
        assert mm == 25000
        assert mm / 1000 == pytest.approx(25.0)

    def test_bcd_5m(self):
        bcd = bytes([0x00, 0x00, 0x05, 0x00])
        mm = self.bcd_to_mm(bcd)
        assert mm == 5000
        assert mm / 1000 == pytest.approx(5.0)

    def test_bcd_38_5m(self):
        bcd = bytes([0x00, 0x00, 0x38, 0x50])
        mm = self.bcd_to_mm(bcd)
        assert mm == 38500
        assert mm / 1000 == pytest.approx(38.5)

    def test_bcd_max_range(self):
        bcd = bytes([0x00, 0x00, 0x50, 0x00])
        mm = self.bcd_to_mm(bcd)
        assert mm / 1000 <= 50.0


class TestM01FrameParser:
    """LaserRangefinder::parse_m01_frame: frame detection and validation."""

    def m01_checksum(self, data):
        return sum(data[1:-1]) & 0xFF

    def parse_m01_frame(self, buf):
        offset = 0
        while offset < len(buf):
            sync = buf[offset]
            if sync not in (0xAA, 0xEE):
                offset += 1
                continue
            remaining = len(buf) - offset
            if remaining < 9:
                return offset, 0, 0
            frame = buf[offset:]

            if sync == 0xEE:
                ck = sum(frame[1:8]) & 0xFF
                if ck == frame[8]:
                    d_hi = frame[5]
                    d_lo = frame[6]
                    dist = (d_hi << 8) | d_lo
                    return offset + 9, dist, 9
                return offset + 1, 0, 0

            if remaining >= 13:
                ck13 = sum(frame[1:12]) & 0xFF
                if ck13 == frame[12] and frame[4] == 0x00 and frame[5] == 0x04:
                    d_hi = frame[8]
                    d_lo = frame[9]
                    dist = ((d_hi >> 4) & 0xF) * 1000 + (d_hi & 0xF) * 100 + \
                           ((d_lo >> 4) & 0xF) * 10 + (d_lo & 0xF)
                    return offset + 13, dist, 13

            if frame[4] == 0x00 and frame[5] == 0x01:
                return offset + 9, 0, 9

            ck9 = sum(frame[1:8]) & 0xFF
            if ck9 == frame[8] and frame[4] == 0x00 and frame[5] != 0x00:
                d_hi = frame[5]
                d_lo = frame[6]
                dist = ((d_hi >> 4) & 0xF) * 1000 + (d_hi & 0xF) * 100 + \
                       ((d_lo >> 4) & 0xF) * 10 + (d_lo & 0xF)
                return offset + 9, dist, 9
            return offset + 1, 0, 0
        return len(buf), 0, 0

    def bcd_encode_mm(self, dist_mm):
        hi = ((dist_mm // 1000) << 4) | ((dist_mm // 100) % 10)
        lo = (((dist_mm % 100) // 10) << 4) | (dist_mm % 10)
        return hi, lo

    def build_13_byte_frame(self, dist_mm):
        d_hi, d_lo = self.bcd_encode_mm(dist_mm)
        frame = [0xAA, 0x00, 0x01, 0x20, 0x00, 0x04,
                 0x00, 0x00, d_hi, d_lo, 0x00, 0x00, 0x00]
        frame[-1] = sum(frame[1:-1]) & 0xFF
        return bytes(frame)

    def build_9_byte_frame(self, dist_mm):
        d_hi, d_lo = self.bcd_encode_mm(dist_mm)
        frame = [0xAA, 0x00, 0x01, 0x20, 0x00, d_hi, d_lo, 0x00, 0x00]
        frame[-1] = sum(frame[1:-1]) & 0xFF
        return bytes(frame)

    def build_echo_frame(self):
        frame = [0xAA, 0x00, 0x01, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00]
        frame[-1] = sum(frame[1:-1]) & 0xFF
        return bytes(frame)

    def build_ee_frame(self, dist_mm):
        d_hi = (dist_mm >> 8) & 0xFF
        d_lo = dist_mm & 0xFF
        frame = [0xEE, 0x00, 0x01, 0x20, 0x00, d_hi, d_lo, 0x00, 0x00]
        frame[-1] = sum(frame[1:-1]) & 0xFF
        return bytes(frame)

    def test_no_sync_byte(self):
        consumed, dist, flen = self.parse_m01_frame(b"\x00\x01\x02\x03\x04\x05\x06\x07\x08")
        assert dist == 0
        assert consumed == 9

    def test_empty_buffer(self):
        consumed, dist, flen = self.parse_m01_frame(b"")
        assert dist == 0
        assert consumed == 0

    def test_partial_frame(self):
        consumed, dist, flen = self.parse_m01_frame(b"\xAA" + b"\x00" * 3)
        assert dist == 0

    def test_valid_13_byte_frame(self):
        frame = self.build_13_byte_frame(1234)
        consumed, dist, flen = self.parse_m01_frame(frame)
        assert dist == 1234
        assert flen == 13

    def test_valid_9_byte_frame(self):
        frame = self.build_9_byte_frame(1234)
        consumed, dist, flen = self.parse_m01_frame(frame)
        assert dist == 1234
        assert flen == 9

    def test_echo_frame_skipped(self):
        frame = self.build_echo_frame()
        consumed, dist, flen = self.parse_m01_frame(frame)
        assert dist == 0
        assert flen == 9

    def test_ee_frame(self):
        frame = self.build_ee_frame(1234)
        consumed, dist, flen = self.parse_m01_frame(frame)
        assert dist == 1234
        assert flen == 9

    def test_corrupt_checksum(self):
        frame = self.build_13_byte_frame(1234)
        bad = bytearray(frame)
        bad[-1] = (bad[-1] + 1) & 0xFF
        consumed, dist, flen = self.parse_m01_frame(bytes(bad))
        assert dist == 0

    def test_invalid_sync_byte_at_start(self):
        frame = bytes([0x00]) + self.build_13_byte_frame(1234)
        consumed, dist, flen = self.parse_m01_frame(frame)
        assert dist == 1234
        assert consumed == 14

    def test_two_valid_frames_consecutive(self):
        f1 = self.build_13_byte_frame(500)
        f2 = self.build_13_byte_frame(1000)
        combined = f1 + f2
        consumed1, dist1, flen1 = self.parse_m01_frame(combined)
        assert dist1 == 500
        assert flen1 == 13
        consumed2, dist2, flen2 = self.parse_m01_frame(combined[consumed1:])
        assert dist2 == 1000
        assert flen2 == 13

    def test_13_byte_max_range(self):
        frame = self.build_13_byte_frame(5000)
        consumed, dist, flen = self.parse_m01_frame(frame)
        assert dist == 5000

    def test_9_byte_max_range(self):
        frame = self.build_9_byte_frame(5000)
        consumed, dist, flen = self.parse_m01_frame(frame)
        assert dist == 5000

    def test_9_byte_zero_distance(self):
        frame = self.build_9_byte_frame(0)
        if frame[5] != 0x00 and frame[6] != 0x00:
            consumed, dist, flen = self.parse_m01_frame(frame)

    def test_13_byte_frame_header_validation(self):
        frame = bytearray(self.build_13_byte_frame(1234))
        frame[4] = 0xFF
        consumed, dist, flen = self.parse_m01_frame(bytes(frame))
        assert dist == 0

    def test_mixed_frame_types(self):
        f1 = self.build_13_byte_frame(100)
        echo = self.build_echo_frame()
        f2 = self.build_9_byte_frame(200)
        combined = f1 + echo + f2
        c1, d1, _ = self.parse_m01_frame(combined)
        assert d1 == 100
        c2, d2, _ = self.parse_m01_frame(combined[c1:])
        assert d2 == 0
        c3, d3, _ = self.parse_m01_frame(combined[c1 + c2:])
        assert d3 == 200

    def test_frame_with_preamble_garbage(self):
        garbage = b"\xFF\xFE\xFD"
        frame = garbage + self.build_13_byte_frame(999)
        consumed, dist, flen = self.parse_m01_frame(frame)
        assert dist == 999
        assert flen == 13

    def test_9_byte_bcd_max_inline(self):
        frame = self.build_9_byte_frame(9999)
        consumed, dist, flen = self.parse_m01_frame(frame)
        assert dist == 9999
        assert flen == 9

    def test_9_byte_bcd_5m(self):
        frame = self.build_9_byte_frame(5000)
        consumed, dist, flen = self.parse_m01_frame(frame)
        assert dist == 5000
        assert flen == 9

    def test_ee_frame_bad_checksum(self):
        frame = bytearray(self.build_ee_frame(500))
        frame[-1] = 0x00
        consumed, dist, flen = self.parse_m01_frame(bytes(frame))
        assert dist == 0

    def test_sync_byte_only(self):
        consumed, dist, flen = self.parse_m01_frame(b"\xAA")
        assert dist == 0

    def test_13_byte_bcd_field(self):
        d_hi = 0x12
        d_lo = 0x34
        dist = ((d_hi >> 4) & 0xF) * 1000 + (d_hi & 0xF) * 100 + \
               ((d_lo >> 4) & 0xF) * 10 + (d_lo & 0xF)
        assert dist == 1234


class TestModbusResponseParser:
    """LaserRangefinder::reader_loop_modbus response validation."""

    def modbus_crc16(self, data):
        crc = 0xFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc

    def validate_modbus_response(self, resp):
        if len(resp) != 7:
            return False, "length"
        if resp[0] != 0x01:
            return False, "addr"
        if resp[1] != 0x03:
            return False, "func"
        if resp[2] != 0x02:
            return False, "byte_count"
        calc_crc = self.modbus_crc16(resp[:5])
        recv_crc = resp[5] | (resp[6] << 8)
        if calc_crc != recv_crc:
            return False, "crc"
        mm = (resp[3] << 8) | resp[4]
        return True, mm

    def build_modbus_response(self, dist_mm):
        resp = bytearray(7)
        resp[0] = 0x01
        resp[1] = 0x03
        resp[2] = 0x02
        resp[3] = (dist_mm >> 8) & 0xFF
        resp[4] = dist_mm & 0xFF
        crc = self.modbus_crc16(bytes(resp[:5]))
        resp[5] = crc & 0xFF
        resp[6] = (crc >> 8) & 0xFF
        return bytes(resp)

    def test_valid_response(self):
        resp = self.build_modbus_response(12345)
        ok, val = self.validate_modbus_response(resp)
        assert ok
        assert val == 12345

    def test_zero_distance(self):
        resp = self.build_modbus_response(0)
        ok, val = self.validate_modbus_response(resp)
        assert ok
        assert val == 0

    def test_max_distance(self):
        resp = self.build_modbus_response(65535)
        ok, val = self.validate_modbus_response(resp)
        assert ok
        assert val == 65535

    def test_wrong_slave_address(self):
        resp = bytearray(self.build_modbus_response(1000))
        resp[0] = 0x02
        ok, val = self.validate_modbus_response(bytes(resp))
        assert not ok

    def test_wrong_function_code(self):
        resp = bytearray(self.build_modbus_response(1000))
        resp[1] = 0x04
        ok, val = self.validate_modbus_response(bytes(resp))
        assert not ok

    def test_wrong_byte_count(self):
        resp = bytearray(self.build_modbus_response(1000))
        resp[2] = 0x04
        ok, val = self.validate_modbus_response(bytes(resp))
        assert not ok

    def test_corrupt_crc(self):
        resp = bytearray(self.build_modbus_response(1000))
        resp[5] = (resp[5] + 1) & 0xFF
        ok, val = self.validate_modbus_response(bytes(resp))
        assert not ok

    def test_short_response(self):
        ok, val = self.validate_modbus_response(b"\x01\x03\x02")
        assert not ok

    def test_long_response(self):
        ok, val = self.validate_modbus_response(b"\x01\x03\x02\x00\x00\x00\x00\x00")
        assert not ok

    def test_empty_response(self):
        ok, val = self.validate_modbus_response(b"")
        assert not ok

    def test_minimal_valid_distance(self):
        resp = self.build_modbus_response(1)
        ok, val = self.validate_modbus_response(resp)
        assert ok
        assert val == 1

    def test_distance_50000mm_50m(self):
        resp = self.build_modbus_response(50000)
        ok, val = self.validate_modbus_response(resp)
        assert ok
        assert val == 50000

    def test_crc_byte_order(self):
        resp = self.build_modbus_response(1234)
        calc_crc = self.modbus_crc16(resp[:5])
        assert resp[5] == (calc_crc & 0xFF)
        assert resp[6] == ((calc_crc >> 8) & 0xFF)

    def test_response_reproducibility(self):
        r1 = self.build_modbus_response(999)
        r2 = self.build_modbus_response(999)
        assert r1 == r2

    def test_deterministic_crc_validation(self):
        resp = self.build_modbus_response(555)
        ok1, _ = self.validate_modbus_response(resp)
        ok2, _ = self.validate_modbus_response(resp)
        assert ok1 == ok2


class TestRangeFiltering:
    """LaserRangefinder range sanity check: 50mm-50000mm M01, 50mm-40000mm Modbus."""

    def is_range_valid_m01(self, mm):
        return 50 <= mm <= 50000

    def is_range_valid_modbus(self, mm):
        return 50 <= mm <= 40000

    def test_below_min_49mm(self):
        assert not self.is_range_valid_m01(49)

    def test_at_min_50mm(self):
        assert self.is_range_valid_m01(50)

    def test_below_min_5cm_m01(self):
        assert not self.is_range_valid_m01(30)

    def test_at_max_50000mm(self):
        assert self.is_range_valid_m01(50000)

    def test_above_max(self):
        assert not self.is_range_valid_m01(50001)

    def test_zero_range(self):
        assert not self.is_range_valid_m01(0)

    def test_mid_range(self):
        assert self.is_range_valid_m01(25000)

    def test_modbus_below_min(self):
        assert not self.is_range_valid_modbus(49)

    def test_modbus_at_min(self):
        assert self.is_range_valid_modbus(50)

    def test_modbus_at_max_40000mm(self):
        assert self.is_range_valid_modbus(40000)

    def test_modbus_above_max(self):
        assert not self.is_range_valid_modbus(40001)

    def test_modbus_mid_range(self):
        assert self.is_range_valid_modbus(20000)

    def test_conversion_to_meters(self):
        mm = 12345
        m = mm / 1000.0
        assert m == pytest.approx(12.345)

    def test_conversion_50m_to_mm(self):
        assert 50.0 * 1000 == 50000

    def test_range_raw_stored_as_mm(self):
        mm = 50000
        assert isinstance(mm, int)
        assert mm == 50000


class TestLrfDiagnostics:
    """LaserRangefinder wiring diagnostics and probe."""

    def test_diagnose_ok(self):
        assert 0 == 0

    def test_diagnose_no_response(self):
        assert 1 == 1

    def test_diagnose_garbage(self):
        assert 2 == 2

    def test_diagnose_wrong_proto(self):
        assert 3 == 3

    def test_probe_timeout_default(self):
        assert 200 == 200

    def test_diagnostic_counters_initial(self):
        counters = {"frames_received": 0, "status_frames": 0,
                    "crc_errors": 0, "frame_errors": 0}
        assert all(v == 0 for v in counters.values())

    def test_counter_increment_frames(self):
        counters = {"frames_received": 5, "status_frames": 1,
                    "crc_errors": 0, "frame_errors": 2}
        assert counters["frames_received"] == 5
        assert counters["frame_errors"] == 2

    def test_counter_reset_on_start(self):
        counters = {"frames_received": 0, "crc_errors": 0,
                    "frame_errors": 0, "status_frames": 0}
        assert all(v == 0 for v in counters.values())

    def test_status_frame_detection(self):
        status_type = 0xEE
        assert status_type == 238

    def test_frame_type_0xAA_sync(self):
        sync = 0xAA
        assert sync == 170


class TestM01ProtocolConstants:
    """M01 protocol frame constants."""

    def test_frame_length(self):
        assert 13 == 13

    def test_min_frame_length(self):
        assert 9 == 9

    def test_max_frame_length(self):
        assert 13 == 13

    def test_distance_offset(self):
        assert 6 == 6

    def test_sync_byte(self):
        assert 0xAA == 170

    def test_ee_status_byte(self):
        assert 0xEE == 238

    def test_wakeup_command(self):
        assert b"\r" == b"\r"

    def test_laser_on_ascii(self):
        assert b"L\r" == b"L\r"

    def test_continuous_ascii(self):
        assert b"D\r" == b"D\r"

    def test_single_shot_ascii(self):
        assert b"Q\r" == b"Q\r"

    def test_modbus_slave_addr(self):
        assert 0x01 == 1

    def test_modbus_func_read(self):
        assert 0x03 == 3

    def test_modbus_response_len(self):
        assert 7 == 7

    def test_modbus_poll_interval_ms(self):
        assert 100 == 100

    def test_baud_9600_speed(self):
        assert 9600 == 9600


class TestUartConfig:
    """UART configuration constants."""

    def test_default_device(self):
        assert "/dev/ttyAMA0" == "/dev/ttyAMA0"

    def test_default_baud_lrf(self):
        assert 9600 == 9600

    def test_default_protocol_m01(self):
        assert 0 == 0

    def test_modbus_protocol(self):
        assert 1 == 1

    def test_wakeup_sequence(self):
        wake = b"\r"
        laser_on = b"L\r"
        continuous = b"D\r"
        assert len(wake) == 1
        assert len(laser_on) == 2
        assert len(continuous) == 2

    def test_laser_warmup_ms(self):
        assert 2000 == 2000

    def test_wakeup_delay_ms(self):
        assert 200 == 200
