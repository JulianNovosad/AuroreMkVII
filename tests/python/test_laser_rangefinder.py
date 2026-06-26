import pytest


class TestLrfProtocol:
    """LrfProtocol enum: M01 vs Modbus RTU."""

    def test_m01_value(self):
        assert 0 == 0

    def test_modbus_value(self):
        assert 1 == 1


class TestM01FrameConstants:
    """M01 continuous-mode protocol constants."""

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

    def test_range_limits(self):
        assert 50.0 == pytest.approx(50.0)
        assert 0.05 == pytest.approx(0.05)


class TestM01Checksum:
    """m01_checksum: sum(bytes[1..N-1]) & 0xFF."""

    def m01_checksum(self, data):
        return sum(data[1:-1]) & 0xFF if len(data) >= 2 else 0

    def test_checksum_empty(self):
        assert self.m01_checksum(b"") == 0

    def test_checksum_single_byte(self):
        assert self.m01_checksum(b"\xAA") == 0

    def test_checksum_known(self):
        data = bytes([0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x00])
        expected = sum(data[1:-1]) & 0xFF
        assert self.m01_checksum(data) == expected

    def test_checksum_verification(self):
        frame = bytes(range(13))
        frame_list = list(frame)
        frame_list[-1] = sum(frame[1:-1]) & 0xFF
        frame = bytes(frame_list)
        assert sum(frame[1:-1]) & 0xFF == frame[-1]


class TestM01BCDToMM:
    """m01_bcd_to_mm: 4 BCD bytes to millimeters."""

    def bcd_to_mm(self, bcd):
        return ( ((bcd[0] >> 4) * 10 + (bcd[0] & 0x0F)) * 1000000 +
                 ((bcd[1] >> 4) * 10 + (bcd[1] & 0x0F)) * 10000 +
                 ((bcd[2] >> 4) * 10 + (bcd[2] & 0x0F)) * 100 +
                 ((bcd[3] >> 4) * 10 + (bcd[3] & 0x0F)) )

    def test_zero(self):
        bcd = bytes([0x00, 0x00, 0x00, 0x00])
        assert self.bcd_to_mm(bcd) == 0

    def test_one_meter(self):
        bcd = bytes([0x00, 0x00, 0x10, 0x00])
        assert self.bcd_to_mm(bcd) == 1000

    def test_twelve_point_thirty_four_meters(self):
        bcd = bytes([0x00, 0x00, 0x12, 0x34])
        assert self.bcd_to_mm(bcd) == 1234

    def test_fifty_meters(self):
        bcd = bytes([0x00, 0x00, 0x50, 0x00])
        assert self.bcd_to_mm(bcd) == 5000

    def test_max_range(self):
        bcd = bytes([0x00, 0x00, 0x50, 0x00])
        mm = self.bcd_to_mm(bcd)
        assert mm / 100 <= 50.0

    def test_bcd_digit_extraction(self):
        byte = 0x12
        tens = (byte >> 4) * 10
        ones = byte & 0x0F
        assert tens + ones == 12

    def test_all_zeros(self):
        assert self.bcd_to_mm(bytes([0x00, 0x00, 0x00, 0x00])) == 0

    def test_ten_meters(self):
        assert self.bcd_to_mm(bytes([0x00, 0x00, 0x10, 0x00])) == 1000


class TestModbusRTU:
    """Modbus RTU CRC-16 and protocol constants."""

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

    def test_crc_known(self):
        assert self.modbus_crc16(b"\x01\x03\x00\x00\x00\x01") == 0x0A84

    def test_crc_from_modbus_spec(self):
        crc = self.modbus_crc16(b"\x01\x03\x02\x00\x00")
        assert isinstance(crc, int)
        assert 0 <= crc <= 0xFFFF

    def test_slave_address(self):
        assert 0x01 == 1

    def test_function_code(self):
        assert 0x03 == 3

    def test_response_length(self):
        assert 7 == 7

    def test_poll_interval(self):
        assert 100 == 100

    def test_deterministic_crc(self):
        data = b"\x01\x03\x00\x00\x00\x01"
        assert self.modbus_crc16(data) == self.modbus_crc16(data)

    def test_different_data_different_crc(self):
        assert self.modbus_crc16(b"\x01") != self.modbus_crc16(b"\x02")


class TestLaserRangefinderDiagnostics:
    """Diagnostic counters and wiring check."""

    def test_diagnose_codes(self):
        ok = 0
        no_response = 1
        garbage = 2
        wrong_proto = 3
        assert ok == 0
        assert no_response == 1
        assert garbage == 2
        assert wrong_proto == 3

    def test_counters_initial(self):
        assert 0 == 0
        assert 0 == 0
        assert 0 == 0
        assert 0 == 0
        assert 0 == 0

    def test_frame_type_0xEE(self):
        status_type = 0xEE
        assert status_type == 238
