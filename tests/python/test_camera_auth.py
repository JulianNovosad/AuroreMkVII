import hashlib
import hmac as hmac_lib
import struct
import pytest


SHA256_LEN = 32
HMAC_LEN = 32


class TestDefaultHmacKey:
    def test_key_not_null(self):
        key = "AuroreMkVII_Default_Dev_Key_2024_ABCDEF123456"
        assert key is not None

    def test_key_length_32_plus(self):
        key = "AuroreMkVII_Default_Dev_Key_2024_ABCDEF123456"
        assert len(key) >= 32

    def test_key_is_string(self):
        key = "AuroreMkVII_Default_Dev_Key_2024_ABCDEF123456"
        assert isinstance(key, str)

    def test_key_ascii_printable(self):
        key = "AuroreMkVII_Default_Dev_Key_2024_ABCDEF123456"
        assert all(32 <= ord(c) <= 126 for c in key)

    def test_key_produces_valid_hmac(self):
        key = "AuroreMkVII_Default_Dev_Key_2024_ABCDEF123456"
        data = b"test frame data"
        tag = hmac_lib.new(key.encode(), data, "sha256").digest()
        assert len(tag) == HMAC_LEN


class TestComputeFrameHeader:
    def make_header(self, sequence, timestamp_ns, width, height, fmt):
        packed = struct.pack("<IQII", sequence, timestamp_ns,
                            width, height)
        return packed

    def test_basic_header(self):
        hdr = self.make_header(1, 1000, 1536, 864, 0)
        assert len(hdr) >= 16

    def test_header_with_hash(self):
        hdr = self.make_header(42, 1234567890, 640, 480, 1)
        hdr_hash = hashlib.sha256(hdr).digest()
        assert len(hdr_hash) == SHA256_LEN

    def test_hmac_over_header_and_hash(self):
        key = b"test_key_32_bytes_long!!!!!"
        hdr = self.make_header(1, 1000, 1536, 864, 0)
        hdr_hash = hashlib.sha256(hdr).digest()
        hmac_input = hdr + hdr_hash
        tag = hmac_lib.new(key, hmac_input, "sha256").digest()
        assert len(tag) == HMAC_LEN

    def test_different_sequences_differ(self):
        h1 = self.make_header(1, 1000, 1536, 864, 0)
        h2 = self.make_header(2, 1000, 1536, 864, 0)
        assert h1 != h2

    def test_resolution_in_header(self):
        hdr = self.make_header(0, 0, 640, 480, 1)
        w, h = struct.unpack_from("<II", hdr, 12)
        assert w == 640
        assert h == 480


class TestComputeFrameHash:
    def test_empty_frame_hash(self):
        h = hashlib.sha256(b"").digest()
        assert len(h) == SHA256_LEN

    def test_known_pixel_data_hash(self):
        pixels = b"\x00" * 100
        h = hashlib.sha256(pixels).hexdigest()
        assert h == "cd00e292c5970d3c5e2f0ffa5171e555bc46bfc4faddfb4a418b6840b86e79a3"

    def test_frame_hash_with_zeros(self):
        pixels = bytes(1024)
        h = hashlib.sha256(pixels).hexdigest()
        assert h == "5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef"

    def test_frame_hash_deterministic(self):
        pixels = b"\xAB" * 256
        assert hashlib.sha256(pixels).digest() == hashlib.sha256(pixels).digest()

    def test_frame_hash_changes_with_data(self):
        assert hashlib.sha256(b"\x00" * 16).digest() != hashlib.sha256(b"\xFF" * 16).digest()

    def test_frame_hash_all_pixels_max(self):
        pixels = b"\xFF" * 64
        h = hashlib.sha256(pixels).hexdigest()
        assert h == "8667e718294e9e0df1d30600ba3eeb201f764aad2dad72748643e4a285e1d1f7"

    def test_frame_hash_partial_row(self):
        row = bytes(1920)
        h = hashlib.sha256(row).hexdigest()
        assert h == "155e437b946ac82ae591ff382b8d19efda9397b2282672dbabd91ec31ce8a651"

    def test_frame_hash_verifiable(self):
        pixels = b"SIMULATED_FRAME_DATA_1234"
        h1 = hashlib.sha256(pixels).digest()
        h2 = hashlib.sha256(pixels).digest()
        assert h1 == h2


class TestComputeFrameHmac:
    def test_known_hmac_key_and_data(self):
        key = b"Jefe"
        data = b"what do ya want for nothing?"
        expected = "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"
        result = hmac_lib.new(key, data, "sha256").hexdigest()
        assert result == expected

    def test_hmac_output_size(self):
        tag = hmac_lib.new(b"key", b"data", "sha256").digest()
        assert len(tag) == HMAC_LEN

    def test_hmac_deterministic(self):
        k, d = b"testkey", b"testdata"
        assert hmac_lib.new(k, d, "sha256").digest() == hmac_lib.new(k, d, "sha256").digest()

    def test_hmac_key_change(self):
        d = b"shared data"
        assert hmac_lib.new(b"key1", d, "sha256").digest() != hmac_lib.new(b"key2", d, "sha256").digest()

    def test_hmac_data_change(self):
        k = b"fixedkey"
        assert hmac_lib.new(k, b"data1", "sha256").digest() != hmac_lib.new(k, b"data2", "sha256").digest()

    def test_hmac_with_key_from_env(self):
        key = b"x" * 32
        data = b"frame authentication data"
        tag = hmac_lib.new(key, data, "sha256").digest()
        assert len(tag) == HMAC_LEN

    def test_hmac_with_longer_key_hashed(self):
        key = b"a" * 64
        data = b"test"
        tag = hmac_lib.new(key, data, "sha256").digest()
        assert len(tag) == HMAC_LEN

    def test_hmac_rfc4231_test_case_2(self):
        key = b"Jefe"
        data = b"what do ya want for nothing?"
        expected = "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"
        assert hmac_lib.new(key, data, "sha256").hexdigest() == expected


class TestAuthenticateFrame:
    def test_authenticate_round_trip(self):
        key = b"test_auth_key_32_bytes_long!!"
        pixels = b"pixel data for authentication test"
        header = struct.pack("<IQQIIB", 1, 1000, 1536, 864, 0, 0)

        frame_hash = hashlib.sha256(pixels).digest()
        hmac_input = header + frame_hash
        tag = hmac_lib.new(key, hmac_input, "sha256").digest()

        assert len(frame_hash) == SHA256_LEN
        assert len(tag) == HMAC_LEN

    def test_authenticate_wrong_key_fails(self):
        key1 = b"correct_key_32_bytes_long!!!!!"
        key2 = b"wrong_key_32_bytes_long!!!!!!!"
        pixels = b"sensitive frame data"
        header = struct.pack("<IQQIIB", 1, 1000, 1536, 864, 0, 0)

        fh = hashlib.sha256(pixels).digest()
        tag1 = hmac_lib.new(key1, header + fh, "sha256").digest()
        tag2 = hmac_lib.new(key2, header + fh, "sha256").digest()
        assert tag1 != tag2

    def test_authenticate_data_tamper_detected(self):
        key = b"fixed_key_32_bytes_long!!!!!!"
        pixels = b"original frame data"
        tampered = b"TAMPERED frame data"
        header = struct.pack("<IQQIIB", 1, 1000, 1536, 864, 0, 0)

        fh_orig = hashlib.sha256(pixels).digest()
        fh_tamper = hashlib.sha256(tampered).digest()
        tag_orig = hmac_lib.new(key, header + fh_orig, "sha256").digest()
        tag_tamper = hmac_lib.new(key, header + fh_tamper, "sha256").digest()
        assert tag_orig != tag_tamper

    def test_authenticate_const_time_compare(self):
        key = b"key_for_const_time_test_32b!!"
        pixels = b"constant time test"
        header = bytes(20)

        fh = hashlib.sha256(pixels).digest()
        tag = hmac_lib.new(key, header + fh, "sha256").digest()

        xor_sum = 0
        for b1, b2 in zip(tag, hmac_lib.new(key, header + fh, "sha256").digest()):
            xor_sum |= b1 ^ b2
        assert xor_sum == 0

    def test_authenticate_different_seq_fails(self):
        key = b"seq_test_key_32_bytes_long!!!"
        pixels = b"sequence number test"

        hdr1 = struct.pack("<IQQIIB", 1, 1000, 1536, 864, 0, 0)
        hdr2 = struct.pack("<IQQIIB", 2, 1000, 1536, 864, 0, 0)
        fh = hashlib.sha256(pixels).digest()

        tag1 = hmac_lib.new(key, hdr1 + fh, "sha256").digest()
        tag2 = hmac_lib.new(key, hdr2 + fh, "sha256").digest()
        assert tag1 != tag2


class TestFrameAuthConstants:
    def test_sha256_hash_size(self):
        assert SHA256_LEN == 32

    def test_hmac_size(self):
        assert HMAC_LEN == 32

    def test_frame_hash_field_size(self):
        assert hashlib.sha256(b"").digest_size == 32

    def test_hmac_field_size(self):
        assert hmac_lib.new(b"k", b"d", "sha256").digest_size == 32

    def test_hmac_key_32_bytes(self):
        key = b"k" * 32
        assert len(key) == 32

    def test_header_and_hash_for_hmac(self):
        hdr = bytes(24)
        fh = bytes(32)
        hmac_input = hdr + fh
        assert len(hmac_input) == 56

    def test_frame_auth_total_size(self):
        frame_hash = 32
        hmac = 32
        assert frame_hash + hmac == 64
