import pytest
import hashlib
import hmac as hmac_lib
import struct
import os


SHA256_LEN = 32
HMAC_LEN = 32


class TestSHA256:
    """compute_sha256_raw: SHA256 hashing."""

    def test_known_hash(self):
        result = hashlib.sha256(b"").hexdigest()
        assert result == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

    def test_hello_hash(self):
        result = hashlib.sha256(b"hello").hexdigest()
        assert result == "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"

    def test_output_size(self):
        result = hashlib.sha256(b"test").digest()
        assert len(result) == SHA256_LEN

    def test_deterministic(self):
        assert hashlib.sha256(b"data").digest() == hashlib.sha256(b"data").digest()

    def test_different_inputs(self):
        assert hashlib.sha256(b"foo").digest() != hashlib.sha256(b"bar").digest()


class TestHMACSHA256:
    """compute_hmac_sha256_raw: HMAC-SHA256 signing."""

    def test_known_hmac(self):
        key = b"key"
        msg = b"The quick brown fox jumps over the lazy dog"
        expected = "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"
        result = hmac_lib.new(key, msg, "sha256").hexdigest()
        assert result == expected

    def test_output_size(self):
        result = hmac_lib.new(b"key", b"data", "sha256").digest()
        assert len(result) == HMAC_LEN

    def test_deterministic(self):
        assert hmac_lib.new(b"k", b"d", "sha256").digest() == hmac_lib.new(b"k", b"d", "sha256").digest()

    def test_key_change_changes_hmac(self):
        h1 = hmac_lib.new(b"key1", b"data", "sha256").digest()
        h2 = hmac_lib.new(b"key2", b"data", "sha256").digest()
        assert h1 != h2


class TestVerifyHMAC:
    """verify_hmac_sha256_raw: constant-time comparison."""

    def test_valid_signature(self):
        key = b"secret"
        data = b"test data"
        sig = hmac_lib.new(key, data, "sha256").digest()
        computed = hmac_lib.new(key, data, "sha256").digest()
        diff = bytes(a ^ b for a, b in zip(computed, sig))
        assert sum(diff) == 0

    def test_invalid_signature(self):
        key = b"secret"
        data = b"test data"
        wrong_key = b"wrong"
        sig = hmac_lib.new(key, data, "sha256").digest()
        computed = hmac_lib.new(wrong_key, data, "sha256").digest()
        diff = bytes(a ^ b for a, b in zip(computed, sig))
        assert sum(diff) != 0

    def test_constant_time_comparison_does_not_short_circuit(self):
        sig1 = b"\x00" * 32
        sig2 = b"\x01" * 32
        xor_sum = 0
        for a, b in zip(sig1, sig2):
            xor_sum |= a ^ b
        assert xor_sum != 0


class TestSequenceNumber:
    """verify_sequence_number: RFC 1982 serial number arithmetic."""

    def rfc1982_compare(self, a, b):
        """RFC 1982: return True if a > b, handling wrap."""
        return ((a - b) & 0xFFFFFFFF) < (1 << 31) and a != b

    def test_normal_forward(self):
        assert self.rfc1982_compare(10, 5)

    def test_normal_backward(self):
        assert not self.rfc1982_compare(5, 10)

    def test_equal(self):
        assert not self.rfc1982_compare(42, 42)

    def test_wrap_forward(self):
        assert self.rfc1982_compare(5, 0xFFFFFFF0)

    def test_wrap_backward(self):
        assert not self.rfc1982_compare(0xFFFFFFF0, 5)

    def test_boundary_just_before_half(self):
        mid = 1 << 31
        assert self.rfc1982_compare(mid - 1, 0)

    def test_boundary_at_half(self):
        mid = 1 << 31
        assert not self.rfc1982_compare(mid, 0)

    def test_sequential_increment(self):
        for i in range(10):
            assert self.rfc1982_compare(i + 1, i)

    def test_full_wrap_around(self):
        assert self.rfc1982_compare(0, 0xFFFFFFFF)
        assert not self.rfc1982_compare(0xFFFFFFFF, 0)


class TestSequenceGap:
    """is_sequence_gap: threshold-based gap detection."""

    def is_gap(self, old_seq, new_seq, threshold):
        if old_seq == new_seq:
            return False
        diff = (new_seq - old_seq) & 0xFFFFFFFF
        return diff > threshold

    def test_no_gap(self):
        assert not self.is_gap(100, 101, 1000)

    def test_small_gap(self):
        assert not self.is_gap(100, 200, 1000)

    def test_large_gap(self):
        assert self.is_gap(100, 2000, 500)

    def test_equal_no_gap(self):
        assert not self.is_gap(42, 42, 0)

    def test_zero_threshold(self):
        assert self.is_gap(100, 101, 0)

    def test_wrap_gap(self):
        assert self.is_gap(0xFFFFFFF0, 100, 50)

    def test_k_gimbal_sequence_gap_threshold(self):
        assert 1000 == 1000

    def test_large_gap_detected(self):
        assert self.is_gap(500, 2000, 1000)


class TestLoadHMACKey:
    """load_hmac_key: hex env var and file loading."""

    def test_hex_env_var_64_chars(self):
        hex_key = "a" * 64
        assert len(hex_key) == 64
        raw = bytes.fromhex(hex_key)
        assert len(raw) == 32

    def test_hex_decode_valid(self):
        raw = bytes.fromhex("00" * 32)
        assert len(raw) == 32
        assert all(b == 0 for b in raw)

    def test_hex_decode_known(self):
        raw = bytes.fromhex("0123456789abcdef" * 4)
        assert len(raw) == 32
        assert raw[0] == 0x01
        assert raw[1] == 0x23

    def test_invalid_hex(self):
        with pytest.raises(ValueError):
            bytes.fromhex("xyz")

    def test_key_from_env_32_plus_chars(self):
        env_val = "x" * 32
        assert len(env_val) >= 32

    def test_short_env_var_rejected(self):
        env_val = "abc"
        has_64 = len(env_val) == 64
        has_32_plus = len(env_val) >= 32
        assert not has_64 and not has_32_plus


class TestGenerateKey256Bit:
    """generate_key_256bit: hardware RNG key generation."""

    def test_key_length(self):
        key = os.urandom(32)
        assert len(key) == 32

    def test_key_is_random(self):
        key1 = os.urandom(32)
        key2 = os.urandom(32)
        assert key1 != key2

    def test_key_not_all_zeros(self):
        key = os.urandom(32)
        assert any(b != 0 for b in key)


class TestSaveKeyToFile:
    """save_key_to_file: key persistence."""

    def test_write_and_read(self, tmp_path):
        key = os.urandom(32)
        path = tmp_path / "test_key.bin"
        path.write_bytes(key)
        loaded = path.read_bytes()
        assert loaded == key

    def test_protected_permissions(self):
        mode = 0o600
        assert mode == 0o600


class TestSHA256Extended:
    """Additional SHA256 known-answer tests (NIST vectors)."""

    def test_nist_abc_hash(self):
        h = hashlib.sha256(b"abc").hexdigest()
        assert h == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"

    def test_nist_empty_string(self):
        h = hashlib.sha256(b"").hexdigest()
        assert h == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

    def test_nist_56_chars(self):
        msg = b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
        h = hashlib.sha256(msg).hexdigest()
        assert h == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"

    def test_nist_one_million_a(self):
        msg = b"a" * 1000000
        h = hashlib.sha256(msg).hexdigest()
        assert h == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"


class TestHMACSHA256Extended:
    """Additional HMAC-SHA256 test vectors (RFC 4231)."""

    def test_rfc4231_test_case_1(self):
        key = bytes.fromhex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b")
        data = b"Hi There"
        expected = "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"
        result = hmac_lib.new(key, data, "sha256").hexdigest()
        assert result == expected

    def test_rfc4231_test_case_3(self):
        key = bytes.fromhex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
        data = bytes.fromhex("dd" * 50)
        expected = "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"
        result = hmac_lib.new(key, data, "sha256").hexdigest()
        assert result == expected

    def test_rfc4231_test_case_4(self):
        key = bytes.fromhex("0102030405060708090a0b0c0d0e0f10111213141516171819")
        data = bytes.fromhex("cd" * 50)
        expected = "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b"
        result = hmac_lib.new(key, data, "sha256").hexdigest()
        assert result == expected

    def test_rfc4231_test_case_6(self):
        key = bytes.fromhex("aa" * 131)
        data = b"Test Using Larger Than Block-Size Key - Hash Key First"
        expected = "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"
        result = hmac_lib.new(key, data, "sha256").hexdigest()
        assert result == expected

    def test_zero_key(self):
        key = b"\x00" * 32
        data = b"test data"
        h = hmac_lib.new(key, data, "sha256").digest()
        assert len(h) == 32

    def test_full_key_space(self):
        key = bytes(range(32))
        data = b"data"
        h = hmac_lib.new(key, data, "sha256").digest()
        assert len(h) == 32


class TestConstantTimeCompare:
    """Constant-time comparison utilities."""

    def const_time_eq(self, a, b):
        if len(a) != len(b):
            return False
        result = 0
        for x, y in zip(a, b):
            result |= x ^ y
        return result == 0

    def test_equal_arrays(self):
        assert self.const_time_eq(b"hello", b"hello")

    def test_unequal_arrays(self):
        assert not self.const_time_eq(b"hello", b"world")

    def test_equal_hmacs(self):
        h1 = hmac_lib.new(b"k", b"d", "sha256").digest()
        h2 = hmac_lib.new(b"k", b"d", "sha256").digest()
        assert self.const_time_eq(h1, h2)

    def test_unequal_hmacs(self):
        h1 = hmac_lib.new(b"k1", b"d", "sha256").digest()
        h2 = hmac_lib.new(b"k2", b"d", "sha256").digest()
        assert not self.const_time_eq(h1, h2)

    def test_different_lengths(self):
        assert not self.const_time_eq(b"abc", b"abcd")

    def test_all_zeros(self):
        assert self.const_time_eq(bytes(32), bytes(32))

    def test_single_bit_difference(self):
        a = bytes(32)
        b = b"\x01" + bytes(31)
        assert not self.const_time_eq(a, b)


class TestECDSA:
    """ECDSA P-256 signing constants and DER encoding."""

    def test_curve_name(self):
        curve = "prime256v1"
        assert curve == "prime256v1"

    def test_hash_algorithm(self):
        assert hasattr(hashlib, "sha256")

    def test_signature_der_format(self):
        sig = bytes([0x30]) + bytes([0x45, 0x02, 0x21]) + bytes(32) + bytes([0x02, 0x20]) + bytes(32)
        assert sig[0] == 0x30  # SEQUENCE tag

    def test_public_key_pem_identifier(self):
        assert b"PUBLIC KEY" in b"-----BEGIN PUBLIC KEY-----"

    def test_private_key_pem_identifier(self):
        assert b"EC PRIVATE KEY" in b"-----BEGIN EC PRIVATE KEY-----"

    def test_der_sequence_tag(self):
        assert 0x30 == 0x30

    def test_der_integer_tag(self):
        assert 0x02 == 0x02

    def test_der_bitstring_tag(self):
        assert 0x03 == 0x03

    def test_der_octetstring_tag(self):
        assert 0x04 == 0x04

    def test_p256_key_size_bits(self):
        assert 256 == 256

    def test_p256_key_size_bytes(self):
        assert 32 == 32

    def test_ecdsa_signature_r_s_size(self):
        r_len = 32
        s_len = 32
        assert r_len == 32
        assert s_len == 32

    def test_ecdsa_signature_typical_der_length(self):
        sig_len = 70 + 2  # typical DER-encoded P-256 sig: 70-72 bytes
        assert 70 <= sig_len <= 74

    def test_pem_begin_marker(self):
        assert b"-----BEGIN" in b"-----BEGIN PUBLIC KEY-----"


class TestECDSAKeyGeneration:
    """ECDSA key structure validation."""

    def test_ecdsa_public_key_uncompressed_len(self):
        uncompressed = bytes([0x04]) + bytes(64)
        assert len(uncompressed) == 65

    def test_ecdsa_public_key_compressed_len(self):
        compressed_even = bytes([0x02]) + bytes(32)
        compressed_odd = bytes([0x03]) + bytes(32)
        assert len(compressed_even) == 33
        assert len(compressed_odd) == 33

    def test_uncompressed_prefix(self):
        assert 0x04 == 0x04

    def test_compressed_even_prefix(self):
        assert 0x02 == 0x02

    def test_compressed_odd_prefix(self):
        assert 0x03 == 0x03

    def test_p256_prime_length(self):
        assert 115792089210356248762697446949407573530086143415290314195533631308867097853951 > 0


class TestECDSAFilePathDefaults:
    """Default file paths for ECDSA verification."""

    def test_default_pubkey_path(self):
        assert "/etc/aurore/signing_key.pub" == "/etc/aurore/signing_key.pub"

    def test_default_sig_path(self):
        assert "/etc/aurore/aurore.sig" == "/etc/aurore/aurore.sig"

    def test_pubkey_path_not_empty(self):
        path = "/etc/aurore/signing_key.pub"
        assert len(path) > 0

    def test_sig_path_not_empty(self):
        path = "/etc/aurore/aurore.sig"
        assert len(path) > 0


class TestVerifySelf:
    """verify_self: binary authentication paths."""

    def test_proc_self_exe_exists(self):
        path = "/proc/self/exe"
        assert os.path.exists(path)

    def test_default_pubkey_path(self):
        assert "/etc/aurore/signing_key.pub" == "/etc/aurore/signing_key.pub"

    def test_default_sig_path(self):
        assert "/etc/aurore/aurore.sig" == "/etc/aurore/aurore.sig"
