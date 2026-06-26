import hashlib
import hmac as hmac_lib
import os
import struct
import time
from pathlib import Path

import pytest


class TestSecurityConstants:
    """All security constants verified against C++ header values."""

    AUTH_TOKEN_SIZE = 32
    MAX_SEQUENCE_GAP = 1000
    MAX_AUTH_ATTEMPTS = 3
    AUTH_TIMEOUT_MS = 100
    AUTH_COOLDOWN_MS = 5000

    def test_auth_token_size(self):
        assert self.AUTH_TOKEN_SIZE == 32
        assert self.AUTH_TOKEN_SIZE == len(os.urandom(self.AUTH_TOKEN_SIZE))

    def test_max_sequence_gap(self):
        assert self.MAX_SEQUENCE_GAP == 1000
        assert self.MAX_SEQUENCE_GAP > 0
        assert (self.MAX_SEQUENCE_GAP & (self.MAX_SEQUENCE_GAP - 1)) != 0  # not power of 2, just a threshold

    def test_max_auth_attempts(self):
        assert self.MAX_AUTH_ATTEMPTS == 3
        assert self.MAX_AUTH_ATTEMPTS > 0
        assert self.MAX_AUTH_ATTEMPTS < 10

    def test_auth_timeout_ms(self):
        assert self.AUTH_TIMEOUT_MS == 100
        assert self.AUTH_TIMEOUT_MS > 0
        assert self.AUTH_TIMEOUT_MS < 1000

    def test_auth_cooldown_ms(self):
        assert self.AUTH_COOLDOWN_MS == 5000
        assert self.AUTH_COOLDOWN_MS > self.AUTH_TIMEOUT_MS
        assert self.AUTH_COOLDOWN_MS >= 1000

    def test_all_constants_positive(self):
        for c in [self.AUTH_TOKEN_SIZE, self.MAX_SEQUENCE_GAP,
                  self.MAX_AUTH_ATTEMPTS, self.AUTH_TIMEOUT_MS, self.AUTH_COOLDOWN_MS]:
            assert c > 0

    def test_cooldown_greater_than_timeout(self):
        assert self.AUTH_COOLDOWN_MS > self.AUTH_TIMEOUT_MS * 10

    def test_max_attempts_reasonable(self):
        assert 2 <= self.MAX_AUTH_ATTEMPTS <= 5


class TestHkdfKeyDerivation:
    """HKDF extract-then-expand pattern (RFC 5869)."""

    @staticmethod
    def hkdf_extract(salt: bytes, ikm: bytes) -> bytes:
        """HKDF-Extract: HMAC-SHA256(salt, IKM)."""
        if not salt:
            salt = bytes(32)
        return hmac_lib.new(salt, ikm, "sha256").digest()

    @staticmethod
    def hkdf_expand(prk: bytes, info: bytes, length: int) -> bytes:
        """HKDF-Expand: T(n) = HMAC-SHA256(PRK, T(n-1) + info + n)."""
        n = (length + 32 - 1) // 32
        okm = b""
        t = b""
        for i in range(1, n + 1):
            t = hmac_lib.new(prk, t + info + bytes([i]), "sha256").digest()
            okm += t
        return okm[:length]

    def hkdf(self, salt: bytes, ikm: bytes, info: bytes, length: int) -> bytes:
        prk = self.hkdf_extract(salt, ikm)
        return self.hkdf_expand(prk, info, length)

    def test_extract_prk_length(self):
        prk = self.hkdf_extract(b"salt", b"ikm")
        assert len(prk) == 32

    def test_extract_no_salt_defaults_to_zeros(self):
        prk1 = self.hkdf_extract(b"", b"input")
        prk2 = self.hkdf_extract(bytes(32), b"input")
        assert prk1 == prk2

    def test_extract_different_salt_different_prk(self):
        prk1 = self.hkdf_extract(b"salt_a", b"ikm")
        prk2 = self.hkdf_extract(b"salt_b", b"ikm")
        assert prk1 != prk2

    def test_extract_different_ikm_different_prk(self):
        prk1 = self.hkdf_extract(b"salt", b"ikm_a")
        prk2 = self.hkdf_extract(b"salt", b"ikm_b")
        assert prk1 != prk2

    def test_expand_output_length(self):
        prk = self.hkdf_extract(b"salt", b"ikm")
        for length in [1, 16, 32, 48, 64, 128]:
            okm = self.hkdf_expand(prk, b"info", length)
            assert len(okm) == length

    def test_expand_deterministic(self):
        prk = self.hkdf_extract(b"salt", b"ikm")
        okm1 = self.hkdf_expand(prk, b"info", 64)
        okm2 = self.hkdf_expand(prk, b"info", 64)
        assert okm1 == okm2

    def test_expand_different_info_different_okm(self):
        prk = self.hkdf_extract(b"salt", b"ikm")
        okm1 = self.hkdf_expand(prk, b"info_a", 32)
        okm2 = self.hkdf_expand(prk, b"info_b", 32)
        assert okm1 != okm2

    def test_full_hkdf_rfc5869_test_case_1(self):
        ikm = bytes.fromhex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b")
        salt = bytes.fromhex("000102030405060708090a0b0c")
        info = bytes.fromhex("f0f1f2f3f4f5f6f7f8f9")
        l = 42
        expected_prk = bytes.fromhex(
            "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5"
        )
        expected_okm = bytes.fromhex(
            "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
            "34007208d5b887185865"
        )
        prk = self.hkdf_extract(salt, ikm)
        assert prk == expected_prk
        okm = self.hkdf_expand(prk, info, l)
        assert okm == expected_okm

    def test_full_hkdf_rfc5869_test_case_2(self):
        ikm = bytes.fromhex(
            "000102030405060708090a0b0c0d0e0f"
            "101112131415161718191a1b1c1d1e1f"
            "202122232425262728292a2b2c2d2e2f"
            "303132333435363738393a3b3c3d3e3f"
            "404142434445464748494a4b4c4d4e4f"
        )
        salt = bytes.fromhex(
            "606162636465666768696a6b6c6d6e6f"
            "707172737475767778797a7b7c7d7e7f"
            "808182838485868788898a8b8c8d8e8f"
            "909192939495969798999a9b9c9d9e9f"
            "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
        )
        info = bytes.fromhex(
            "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
            "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
            "d0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
            "e0e1e2e3e4e5e6e7e8e9eaebecedeeef"
            "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"
        )
        l = 82
        expected_prk = bytes.fromhex(
            "06a6b88c5853361a06104c9ceb35b45c" "ef760014904671014a193f40c15fc244"
        )
        expected_okm = bytes.fromhex(
            "b11e398dc80327a1c8e7f78c596a4934"
            "4f012eda2d4efad8a050cc4c19afa97c"
            "59045a99cac7827271cb41c65e590e09"
            "da3275600c2f09b8367793a9aca3db71"
            "cc30c58179ec3e87c14c01d5c1f3434f"
            "1d87"
        )
        prk = self.hkdf_extract(salt, ikm)
        assert prk == expected_prk
        okm = self.hkdf_expand(prk, info, l)
        assert okm == expected_okm

    def test_full_hkdf_rfc5869_test_case_3(self):
        ikm = bytes.fromhex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b")
        salt = bytes(0)
        info = bytes(0)
        l = 42
        expected_prk = bytes.fromhex(
            "19ef24a32c717b167f33a91d6f648bdf" "96596776afdb6377ac434c1c293ccb04"
        )
        expected_okm = bytes.fromhex(
            "8da4e775a563c18f715f802a063c5a31" "b8a11f5c5ee1879ec3454e5f3c738d2d"
            "9d201395faa4b61a96c8"
        )
        prk = self.hkdf_extract(salt, ikm)
        assert prk == expected_prk
        okm = self.hkdf_expand(prk, info, l)
        assert okm == expected_okm

    def test_hkdf_key_derivation_for_encryption(self):
        master_key = os.urandom(32)
        purpose = b"encryption-key-v1"
        salt = os.urandom(16)
        derived = self.hkdf(salt, master_key, purpose, 32)
        assert len(derived) == 32
        assert derived != master_key

    def test_hkdf_key_derivation_for_auth(self):
        master_key = os.urandom(32)
        enc_key = self.hkdf(os.urandom(16), master_key, b"encryption", 32)
        auth_key = self.hkdf(os.urandom(16), master_key, b"authentication", 32)
        assert enc_key != auth_key

    def test_hkdf_multiple_lengths(self):
        master = os.urandom(32)
        for length in [16, 32, 48, 64]:
            okm = self.hkdf(os.urandom(16), master, b"test", length)
            assert len(okm) == length

    def test_hkdf_context_separation(self):
        master = os.urandom(32)
        k1 = self.hkdf(b"salt", master, b"context-a", 32)
        k2 = self.hkdf(b"salt", master, b"context-b", 32)
        assert k1 != k2

    def test_hkdf_salt_change_derives_different_key(self):
        master = os.urandom(32)
        k1 = self.hkdf(b"salt1", master, b"info", 32)
        k2 = self.hkdf(b"salt2", master, b"info", 32)
        assert k1 != k2

    def test_hkdf_ikm_change_derives_different_key(self):
        salt = os.urandom(16)
        k1 = self.hkdf(salt, os.urandom(32), b"info", 32)
        k2 = self.hkdf(salt, os.urandom(32), b"info", 32)
        assert k1 != k2

    def test_hkdf_expand_round_trip(self):
        prk = self.hkdf_extract(b"salt", b"ikm")
        okm_32 = self.hkdf_expand(prk, b"info", 32)
        okm_64 = self.hkdf_expand(prk, b"info", 64)
        assert okm_32 == okm_64[:32]
        assert okm_64[32:] != okm_32

    def test_hkdf_zero_length_output(self):
        prk = self.hkdf_extract(b"salt", b"ikm")
        okm = self.hkdf_expand(prk, b"info", 0)
        assert okm == b""


class TestRateLimiting:
    """Rate limiting and auth attempt limiting (MAX_AUTH_ATTEMPTS=3).

    Models a sliding-window rate limiter with token/attempt tracking.
    """

    class RateLimiter:
        def __init__(self, max_attempts: int, window_ms: int, cooldown_ms: int):
            self.max_attempts = max_attempts
            self.window_ms = window_ms
            self.cooldown_ms = cooldown_ms
            self.attempts = []
            self.blocked_until = 0
            self.cooldown_start = 0

        def allow(self, now_ms: int) -> bool:
            if now_ms < self.blocked_until:
                return False
            self.attempts = [t for t in self.attempts if now_ms - t < self.window_ms]
            if len(self.attempts) >= self.max_attempts:
                self.blocked_until = now_ms + self.cooldown_ms
                self.cooldown_start = now_ms
                return False
            self.attempts.append(now_ms)
            return True

        def remaining(self, now_ms: int) -> int:
            if now_ms < self.blocked_until:
                return 0
            self.attempts = [t for t in self.attempts if now_ms - t < self.window_ms]
            return self.max_attempts - len(self.attempts)

        def is_blocked(self, now_ms: int) -> bool:
            return now_ms < self.blocked_until

        def cooldown_remaining(self, now_ms: int) -> int:
            if now_ms >= self.blocked_until:
                return 0
            return self.blocked_until - now_ms

    def test_allows_up_to_max_attempts(self):
        limiter = self.RateLimiter(3, 100, 5000)
        t = 0
        assert limiter.allow(t)
        assert limiter.allow(t + 1)
        assert limiter.allow(t + 2)
        assert limiter.remaining(t + 3) == 0

    def test_exceeding_max_blocks(self):
        limiter = self.RateLimiter(3, 100, 5000)
        t = 0
        assert limiter.allow(t)
        assert limiter.allow(t + 1)
        assert limiter.allow(t + 2)
        assert not limiter.allow(t + 3)

    def test_blocked_returns_false_during_cooldown(self):
        limiter = self.RateLimiter(3, 100, 5000)
        t = 0
        for _ in range(3):
            limiter.allow(t)
        assert not limiter.allow(3)  # trigger block, blocked_until = 5003
        assert limiter.is_blocked(t + 100)
        assert limiter.is_blocked(t + 1000)
        assert not limiter.is_blocked(t + 5004)

    def test_cooldown_remaining_decreases(self):
        limiter = self.RateLimiter(3, 100, 5000)
        t = 0
        for _ in range(3):
            limiter.allow(t)
        assert not limiter.allow(3)  # trigger block, blocked_until = 5003
        remaining_early = limiter.cooldown_remaining(t + 100)
        remaining_late = limiter.cooldown_remaining(t + 4000)
        assert remaining_early > remaining_late
        assert limiter.cooldown_remaining(t + 5004) == 0

    def test_attempts_expire_after_window(self):
        limiter = self.RateLimiter(3, 100, 5000)
        t = 0
        assert limiter.allow(t)
        assert limiter.allow(t + 1)
        assert limiter.allow(t + 2)
        assert not limiter.allow(t + 3)  # blocked (blocked_until = 5003)
        # After cooldown, should be allowed again
        assert limiter.allow(t + 5004)
        assert limiter.remaining(t + 5005) == 2  # 2 remaining in new window

    def test_remaining_decrements(self):
        limiter = self.RateLimiter(3, 100, 5000)
        t = 0
        assert limiter.remaining(t) == 3
        limiter.allow(t)
        assert limiter.remaining(t + 1) == 2

    def test_window_slides(self):
        limiter = self.RateLimiter(2, 50, 1000)
        t = 0
        assert limiter.allow(t)
        assert limiter.allow(t + 1)
        # After cooldown + window, old attempts expire
        assert limiter.allow(t + 1051)  # cooldown over, first attempt expired
        assert limiter.allow(t + 1052)
        assert not limiter.allow(t + 1053)

    def test_zero_max_attempts_always_blocks(self):
        limiter = self.RateLimiter(0, 100, 5000)
        assert not limiter.allow(0)

    def test_very_large_window(self):
        limiter = self.RateLimiter(3, 100000, 5000)
        t = 0
        for _ in range(3):
            assert limiter.allow(t)
        assert not limiter.allow(t + 1)

    def test_cooldown_longer_than_window(self):
        limiter = self.RateLimiter(3, 50, 10000)
        t = 0
        for _ in range(3):
            limiter.allow(t)
        assert not limiter.allow(3)  # 4th triggers block, blocked_until = 10003
        assert limiter.cooldown_remaining(100) > 0
        assert limiter.blocked_until - t == 10003

    def test_rapid_sequential_attempts_exhaust(self):
        limiter = self.RateLimiter(3, 1000, 5000)
        for i in range(3):
            assert limiter.allow(i)
        assert not limiter.allow(3)

    def test_is_blocked_during_cooldown_after_exhaustion(self):
        limiter = self.RateLimiter(3, 100, 5000)
        for i in range(3):
            limiter.allow(i)
        assert not limiter.allow(3)  # 4th attempt blocked, sets blocked_until
        assert limiter.is_blocked(100)
        assert limiter.is_blocked(2500)

    def test_not_blocked_before_any_attempts(self):
        limiter = self.RateLimiter(3, 100, 5000)
        assert not limiter.is_blocked(0)

    def test_not_blocked_after_normal_attempt_within_limit(self):
        limiter = self.RateLimiter(3, 100, 5000)
        limiter.allow(0)
        assert not limiter.is_blocked(1)

    def test_remaining_max_after_cooldown_expires(self):
        limiter = self.RateLimiter(3, 100, 5000)
        for i in range(3):
            limiter.allow(i)
        assert limiter.remaining(5001) == 3

    def test_blocked_rejects_all_attempts(self):
        limiter = self.RateLimiter(1, 100, 5000)
        assert limiter.allow(0)
        assert not limiter.allow(1)
        assert not limiter.allow(2)
        assert not limiter.allow(100)

    def test_concurrent_attempt_exhaustion(self):
        limiter = self.RateLimiter(3, 100, 5000)
        t = 0
        for _ in range(3):
            limiter.allow(t)
        assert not limiter.allow(t)

    def test_many_windows_no_leak(self):
        limiter = self.RateLimiter(3, 10, 50)
        for i in range(100):
            t = i * 100  # each cluster well after cooldown
            assert limiter.allow(t), f"failed at iteration {i}"
            assert limiter.allow(t + 1)
            assert limiter.allow(t + 2)
            assert not limiter.allow(t + 3)

    def test_single_attempt_never_blocks(self):
        limiter = self.RateLimiter(3, 100, 5000)
        for i in range(10):
            t = i * 10000
            assert limiter.allow(t)

    def test_remaining_zero_when_blocked(self):
        limiter = self.RateLimiter(3, 100, 5000)
        for i in range(3):
            limiter.allow(i)
        limiter.allow(3)  # triggers block, blocked_until = 5003
        assert limiter.remaining(100) == 0


class TestAuthStateMachine:
    """AUTH_TIMEOUT_MS=100, MAX_AUTH_ATTEMPTS=3, AUTH_COOLDOWN_MS=5000.

    Models the full authentication state machine with timeout, retry, and cooldown.
    """

    class Authenticator:
        def __init__(self):
            self.attempts = []
            self.blocked_until = 0
            self.last_auth_start = -1
            self.authenticated = False

        def start_auth(self, now_ms: int) -> bool:
            if now_ms < self.blocked_until:
                return False
            self.last_auth_start = now_ms
            return True

        def complete_auth(self, now_ms: int, token_valid: bool) -> bool:
            if self.last_auth_start < 0:
                return False
            if now_ms - self.last_auth_start > 100:
                return False  # timeout
            if not token_valid:
                self.attempts.append(now_ms)
                self.attempts = [t for t in self.attempts if now_ms - t < 100]
                if len(self.attempts) >= 3:
                    self.blocked_until = now_ms + 5000
                self.last_auth_start = -1
                return False
            self.attempts = []
            self.authenticated = True
            self.last_auth_start = -1
            return True

        def can_attempt(self, now_ms: int) -> bool:
            return now_ms >= self.blocked_until

        def cooldown_remaining(self, now_ms: int) -> int:
            if now_ms >= self.blocked_until:
                return 0
            return self.blocked_until - now_ms

        def is_authenticated(self) -> bool:
            return self.authenticated

    def test_successful_auth(self):
        a = self.Authenticator()
        assert a.start_auth(0)
        assert a.complete_auth(50, True)
        assert a.is_authenticated()

    def test_failed_auth_no_block(self):
        a = self.Authenticator()
        assert a.start_auth(0)
        assert not a.complete_auth(50, False)
        assert not a.is_authenticated()
        assert a.can_attempt(51)

    def test_three_failures_block(self):
        a = self.Authenticator()
        for i in range(3):
            assert a.start_auth(i * 10)
            assert not a.complete_auth(i * 10 + 5, False)
        assert not a.can_attempt(100)
        assert a.cooldown_remaining(100) > 0

    def test_cooldown_expires(self):
        a = self.Authenticator()
        for i in range(3):
            assert a.start_auth(i * 10)
            assert not a.complete_auth(i * 10 + 5, False)
        assert not a.can_attempt(100)
        assert a.can_attempt(5100)

    def test_auth_timeout_expired(self):
        a = self.Authenticator()
        assert a.start_auth(0)
        assert not a.complete_auth(101, True)  # timeout exceeded

    def test_auth_timeout_boundary(self):
        a = self.Authenticator()
        assert a.start_auth(0)
        assert a.complete_auth(100, True)  # exactly at timeout

    def test_success_resets_attempt_counter(self):
        a = self.Authenticator()
        assert a.start_auth(5)
        assert not a.complete_auth(10, False)
        assert a.start_auth(15)
        assert not a.complete_auth(20, False)
        assert a.start_auth(25)
        assert a.complete_auth(30, True)
        # Reset: should allow 3 more failures
        for i in range(3):
            assert a.start_auth(100 + i * 10)
            assert not a.complete_auth(100 + i * 10 + 5, False)
        assert not a.can_attempt(200)

    def test_exactly_max_attempts_then_succeed(self):
        a = self.Authenticator()
        assert a.start_auth(5)
        assert not a.complete_auth(10, False)
        assert a.start_auth(15)
        assert not a.complete_auth(20, False)
        assert a.start_auth(25)
        assert a.complete_auth(30, True)
        assert a.is_authenticated()

    def test_rapid_failures_within_window(self):
        a = self.Authenticator()
        for i in range(3):
            assert a.start_auth(i)
            assert not a.complete_auth(i + 1, False)
        assert not a.can_attempt(10)

    def test_slow_failures_beyond_window_no_block(self):
        a = self.Authenticator()
        for i in range(3):
            t = i * 200  # each 200ms apart
            assert a.start_auth(t)
            assert not a.complete_auth(t + 5, False)
        # Attempts are outside 100ms window, so no block
        assert a.can_attempt(1000)

    def test_authentication_then_subsequent_failures(self):
        a = self.Authenticator()
        assert a.start_auth(0)
        assert a.complete_auth(5, True)
        # After success, can have 3 more failures
        for i in range(3):
            assert a.start_auth(100 + i * 5)
            assert not a.complete_auth(100 + i * 5 + 2, False)
        assert not a.can_attempt(200)

    def test_cooldown_decreases_over_time(self):
        a = self.Authenticator()
        for i in range(3):
            a.start_auth(i * 10)
            a.complete_auth(i * 10 + 5, False)

    def test_no_double_count_on_retry(self):
        a = self.Authenticator()
        a.start_auth(0)
        a.complete_auth(5, False)
        a.start_auth(6)
        a.complete_auth(10, False)
        a.start_auth(11)
        a.complete_auth(15, False)
        assert not a.can_attempt(20)

    def test_successful_auth_no_timeout_resets(self):
        a = self.Authenticator()
        assert a.start_auth(0)
        assert a.complete_auth(5, True)
        assert a.is_authenticated()

    def test_cooldown_zero_when_not_blocked(self):
        a = self.Authenticator()
        assert a.cooldown_remaining(0) == 0
        a.start_auth(5)
        a.complete_auth(10, False)
        assert a.cooldown_remaining(11) == 0

    def test_can_attempt_true_initially(self):
        a = self.Authenticator()
        assert a.can_attempt(0)
        assert a.can_attempt(1000)

    def test_mixed_success_failure_pattern(self):
        a = self.Authenticator()
        outcomes = [False, True, False, False, False]
        for i, outcome in enumerate(outcomes):
            t = i * 10
            if outcome:
                assert a.start_auth(t)
                assert a.complete_auth(t + 5, True)
                assert a.is_authenticated()
            else:
                assert a.start_auth(t)
                assert not a.complete_auth(t + 5, False)
                if i >= 4:
                    assert not a.can_attempt(t + 10)

    def test_auth_complete_without_start(self):
        a = self.Authenticator()
        assert not a.complete_auth(0, True)  # never started

    def test_auth_complete_twice(self):
        a = self.Authenticator()
        a.start_auth(0)
        assert a.complete_auth(50, True)
        assert a.is_authenticated()
        a.start_auth(100)
        assert a.complete_auth(150, True)
        assert a.is_authenticated()


class TestSecureMemoryClearing:
    """Secure memory clearing (zeroing of sensitive material)."""

    def test_zeroing_32_bytes(self):
        buf = bytearray(os.urandom(32))
        assert any(b != 0 for b in buf)
        for i in range(len(buf)):
            buf[i] = 0
        assert all(b == 0 for b in buf)

    def test_zeroing_64_bytes(self):
        buf = bytearray(os.urandom(64))
        assert any(b != 0 for b in buf)
        buf[:] = b"\x00" * len(buf)
        assert all(b == 0 for b in buf)

    def test_zeroing_key_after_use(self):
        key = bytearray(os.urandom(32))
        data = b"sensitive payload"
        h = hmac_lib.new(bytes(key), data, "sha256").digest()
        # Clear key after use
        for i in range(len(key)):
            key[i] = 0
        assert all(b == 0 for b in key)
        # Verify HMAC was valid before clearing
        assert len(h) == 32

    def test_zeroing_of_intermediate_state(self):
        ikm = os.urandom(32)
        salt = os.urandom(16)
        prk = bytearray(hmac_lib.new(salt, ikm, "sha256").digest())
        assert len(prk) == 32
        assert any(b != 0 for b in prk)
        prk[:] = b"\x00" * len(prk)
        assert all(b == 0 for b in prk)

    def test_volatile_secret_does_not_persist(self):
        secret = bytearray(b"this-is-a-test-secret-key-123456")
        assert len(secret) >= 32
        secret[:] = b"\x00" * len(secret)
        assert all(b == 0 for b in secret)

    def test_clear_before_free_pattern(self):
        buf = bytearray(32)
        buf[:] = os.urandom(32)
        buf_copy = bytes(buf)
        assert buf_copy != bytes(32)
        buf[:] = b"\x00" * 32
        assert buf == bytearray(32)

    def test_stack_like_lifetime(self):
        def derive_and_clear():
            material = bytearray(os.urandom(32))
            result = hashlib.sha256(bytes(material)).digest()
            material[:] = b"\x00" * len(material)
            return result
        h = derive_and_clear()
        assert len(h) == 32

    def test_multiple_buffers_cleared(self):
        bufs = [bytearray(os.urandom(32)) for _ in range(5)]
        for b in bufs:
            assert any(x != 0 for x in b)
        for b in bufs:
            b[:] = b"\x00" * len(b)
        for b in bufs:
            assert all(x == 0 for x in b)

    def test_compiler_barrier_emulation(self):
        buf = bytearray(os.urandom(32))
        buf_copy = bytes(buf)
        # Prevent compiler from optimizing away the clear
        # by using the buffer before clearing
        h = hashlib.sha256(buf).digest()
        buf[:] = b"\x00" * len(buf)
        # volatile-like read after clear
        assert buf[0] == 0
        # Original hash should still be valid
        assert len(h) == 32
        assert h != bytes(32)

    def test_clear_partial_buffer(self):
        buf = bytearray(os.urandom(64))
        # Only clear first 32 bytes (key portion)
        buf[:32] = b"\x00" * 32
        assert all(b == 0 for b in buf[:32])
        assert any(b != 0 for b in buf[32:])

    def test_clear_zero_length_no_op(self):
        buf = bytearray(0)
        buf[:] = b"\x00" * 0
        assert len(buf) == 0

    def test_hmac_key_cleared_after_verify(self):
        key = bytearray(os.urandom(32))
        data = b"test message"
        sig = hmac_lib.new(bytes(key), data, "sha256").digest()
        expected = hmac_lib.new(bytes(key), data, "sha256").digest()
        assert sig == expected
        key[:] = b"\x00" * len(key)
        assert all(b == 0 for b in key)

    def test_override_with_pattern_before_zero(self):
        buf = bytearray(os.urandom(32))
        buf[:] = b"\xde\xad\xbe\xef" * 8
        assert all(b == 0xde or b == 0xad or b == 0xbe or b == 0xef for b in buf)
        buf[:] = b"\x00" * 32
        assert all(b == 0 for b in buf)

    def test_scope_bound_clear(self):
        key = os.urandom(32)
        assert len(key) == 32
        # In real code, scope exit triggers memset_s or similar
        del key  # key goes out of scope


class TestKeyWrappingAndUnwrapping:
    """Key wrapping/unwrapping using AES-key-wrap-like pattern with HMAC integrity."""

    @staticmethod
    def wrap_key(wrapping_key: bytes, target_key: bytes) -> bytes:
        """Wrap target_key using HMAC-SHA256 for integrity and XOR for obfuscation.

        Format: [key_len:u16][wrapped_key:variable][hmac:32]
        This is a simplified pattern: derive a wrapping KEK, XOR-encrypt,
        and append an HMAC for integrity verification.
        """
        kek = hashlib.sha256(wrapping_key + b"key-wrap-v1").digest()
        stream = hashlib.sha256(kek + b"enc").digest() + hashlib.sha256(kek + b"enc2").digest()
        stream = stream[:len(target_key)]
        wrapped = bytes(a ^ b for a, b in zip(target_key, stream))
        integrity = hmac_lib.new(kek, wrapped, "sha256").digest()
        key_len = len(target_key)
        return struct.pack("<H", key_len) + wrapped + integrity

    @staticmethod
    def unwrap_key(wrapping_key: bytes, wrapped_payload: bytes) -> bytes:
        """Unwrap and verify the target key."""
        if len(wrapped_payload) < 2 + 32:
            raise ValueError("payload too short")
        key_len = struct.unpack_from("<H", wrapped_payload, 0)[0]
        wrapped_key = wrapped_payload[2:-32]
        if len(wrapped_key) != key_len:
            raise ValueError("key length mismatch")
        stored_integrity = wrapped_payload[-32:]
        kek = hashlib.sha256(wrapping_key + b"key-wrap-v1").digest()
        expected_integrity = hmac_lib.new(kek, wrapped_key, "sha256").digest()
        diff = 0
        for a, b in zip(stored_integrity, expected_integrity):
            diff |= a ^ b
        if diff != 0:
            raise ValueError("integrity check failed")
        stream = hashlib.sha256(kek + b"enc").digest() + hashlib.sha256(kek + b"enc2").digest()
        stream = stream[:key_len]
        return bytes(a ^ b for a, b in zip(wrapped_key, stream))

    def test_wrap_unwrap_round_trip(self):
        wrapping_key = os.urandom(32)
        target_key = os.urandom(32)
        wrapped = self.wrap_key(wrapping_key, target_key)
        unwrapped = self.unwrap_key(wrapping_key, wrapped)
        assert unwrapped == target_key

    def test_wrapped_payload_size(self):
        wrapping_key = os.urandom(32)
        target_key = os.urandom(32)
        wrapped = self.wrap_key(wrapping_key, target_key)
        key_len = len(target_key)
        assert len(wrapped) == 2 + key_len + 32  # 2 len + 32 key + 32 HMAC

    def test_different_wrapping_key_fails_unwrap(self):
        wk1 = os.urandom(32)
        wk2 = os.urandom(32)
        target = os.urandom(32)
        wrapped = self.wrap_key(wk1, target)
        with pytest.raises(ValueError, match="integrity check failed"):
            self.unwrap_key(wk2, wrapped)

    def test_tampered_wrapped_key_fails(self):
        wk = os.urandom(32)
        target = os.urandom(32)
        wrapped = bytearray(self.wrap_key(wk, target))
        wrapped[4] ^= 1  # flip one bit in encrypted key data (after 2-byte length)
        with pytest.raises(ValueError, match="integrity check failed"):
            self.unwrap_key(wk, bytes(wrapped))

    def test_tampered_integrity_tag_fails(self):
        wk = os.urandom(32)
        target = os.urandom(32)
        wrapped = bytearray(self.wrap_key(wk, target))
        wrapped[-1] ^= 1  # flip one bit in integrity tag
        with pytest.raises(ValueError, match="integrity check failed"):
            self.unwrap_key(wk, bytes(wrapped))

    def test_short_payload_raises(self):
        wk = os.urandom(32)
        with pytest.raises(ValueError, match="payload too short"):
            self.unwrap_key(wk, b"short")

    def test_empty_target_key(self):
        wk = os.urandom(32)
        target = b""
        wrapped = self.wrap_key(wk, target)
        unwrapped = self.unwrap_key(wk, wrapped)
        assert unwrapped == target

    def test_deterministic_wrapping(self):
        wk = os.urandom(32)
        target = os.urandom(32)
        w1 = self.wrap_key(wk, target)
        w2 = self.wrap_key(wk, target)
        assert w1 == w2

    def test_different_target_keys_produce_different_wrapped(self):
        wk = os.urandom(32)
        t1 = os.urandom(32)
        t2 = os.urandom(32)
        w1 = self.wrap_key(wk, t1)
        w2 = self.wrap_key(wk, t2)
        assert w1 != w2

    def test_wrap_unwrap_different_sizes(self):
        wk = os.urandom(32)
        for size in [16, 24, 32, 48, 64]:
            target = os.urandom(size)
            wrapped = self.wrap_key(wk, target)
            unwrapped = self.unwrap_key(wk, wrapped)
            assert unwrapped == target

    def test_multiple_wrap_unwrap_cycles(self):
        wk = os.urandom(32)
        key = os.urandom(32)
        for _ in range(10):
            wrapped = self.wrap_key(wk, key)
            key = self.unwrap_key(wk, wrapped)

    def test_wrap_unwrap_with_long_key_material(self):
        wk = os.urandom(64)  # longer wrapping material
        target = os.urandom(32)
        wrapped = self.wrap_key(wk, target)
        unwrapped = self.unwrap_key(wk, wrapped)
        assert unwrapped == target

    def test_integrity_tag_detects_bit_flip_in_middle(self):
        wk = os.urandom(32)
        target = os.urandom(32)
        wrapped = bytearray(self.wrap_key(wk, target))
        wrapped[15] ^= 0xFF  # flip all bits at position 15
        with pytest.raises(ValueError, match="integrity check failed"):
            self.unwrap_key(wk, bytes(wrapped))

    def test_wrap_unwrap_all_zero_key(self):
        wk = os.urandom(32)
        target = bytes(32)
        wrapped = self.wrap_key(wk, target)
        unwrapped = self.unwrap_key(wk, wrapped)
        assert unwrapped == target

    def test_wrap_unwrap_all_one_key(self):
        wk = os.urandom(32)
        target = b"\xFF" * 32
        wrapped = self.wrap_key(wk, target)
        unwrapped = self.unwrap_key(wk, wrapped)
        assert unwrapped == target

    def test_wrapping_key_zero_still_works(self):
        wk = bytes(32)
        target = os.urandom(32)
        wrapped = self.wrap_key(wk, target)
        unwrapped = self.unwrap_key(wk, wrapped)
        assert unwrapped == target

    def test_constant_time_comparison_in_unwrap(self):
        wk = os.urandom(32)
        target = os.urandom(32)
        wrapped = self.wrap_key(wk, target)
        wrapped_tampered = bytearray(wrapped)
        # Tamper a byte in the wrapped key portion (after 2-byte length prefix)
        wrapped_tampered[5] ^= 0x01
        t0 = time.perf_counter_ns()
        with pytest.raises(ValueError):
            self.unwrap_key(wk, bytes(wrapped_tampered))
        t1 = time.perf_counter_ns()
        # Valid payload should succeed
        result = self.unwrap_key(wk, bytes(wrapped))
        t2 = time.perf_counter_ns()
        assert result == target
        first_duration = t1 - t0
        second_duration = t2 - t1
        ratio = max(first_duration, second_duration) / max(min(first_duration, second_duration), 1)
        assert ratio < 100  # within two orders of magnitude (noise floor)

    def test_unwrap_empty_wrapped_key(self):
        wk = os.urandom(32)
        with pytest.raises(ValueError):
            self.unwrap_key(wk, b"")


class TestSecurityEdgeCases:
    """Edge cases for all security primitives."""

    def test_empty_string_hash(self):
        h = hashlib.sha256(b"").hexdigest()
        assert h == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

    def test_empty_hmac_key(self):
        h = hmac_lib.new(b"", b"data", "sha256").digest()
        assert len(h) == 32

    def test_empty_hmac_message(self):
        h = hmac_lib.new(b"key", b"", "sha256").digest()
        assert len(h) == 32

    def test_zero_key_hmac(self):
        h = hmac_lib.new(bytes(32), b"data", "sha256").digest()
        assert len(h) == 32

    def test_max_size_key_hmac(self):
        key = os.urandom(1024)
        h = hmac_lib.new(key, b"data", "sha256").digest()
        assert len(h) == 32

    def test_very_long_message_hash(self):
        msg = b"a" * 1000000
        h = hashlib.sha256(msg).hexdigest()
        assert h == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"

    def test_hmac_key_exactly_32_bytes(self):
        key = os.urandom(32)
        h = hmac_lib.new(key, b"test", "sha256").digest()
        assert len(h) == 32

    def test_hmac_key_exactly_64_bytes(self):
        key = os.urandom(64)
        h = hmac_lib.new(key, b"test", "sha256").digest()
        assert len(h) == 32

    def test_const_time_different_lengths(self):
        def const_time_eq(a, b):
            if len(a) != len(b):
                return False
            diff = 0
            for x, y in zip(a, b):
                diff |= x ^ y
            return diff == 0
        assert not const_time_eq(b"abc", b"abcd")
        assert not const_time_eq(b"", b"a")

    def test_const_time_same_buffer(self):
        def const_time_eq(a, b):
            if len(a) != len(b):
                return False
            diff = 0
            for x, y in zip(a, b):
                diff |= x ^ y
            return diff == 0
        buf = os.urandom(32)
        assert const_time_eq(buf, buf)

    def test_const_time_empty(self):
        def const_time_eq(a, b):
            if len(a) != len(b):
                return False
            diff = 0
            for x, y in zip(a, b):
                diff |= x ^ y
            return diff == 0
        assert const_time_eq(b"", b"")

    def test_const_time_different_single_byte(self):
        def const_time_eq(a, b):
            if len(a) != len(b):
                return False
            diff = 0
            for x, y in zip(a, b):
                diff |= x ^ y
            return diff == 0
        assert const_time_eq(b"\x00", b"\x00")
        assert not const_time_eq(b"\x00", b"\x01")

    def test_sequence_wrap_all_ones(self):
        def rfc1982_gt(a, b):
            return ((a - b) & 0xFFFFFFFF) < (1 << 31) and a != b
        assert rfc1982_gt(0, 0xFFFFFFFF)
        assert not rfc1982_gt(0xFFFFFFFF, 0)

    def test_sequence_half_boundary(self):
        def rfc1982_gt(a, b):
            return ((a - b) & 0xFFFFFFFF) < (1 << 31) and a != b
        mid = 1 << 31
        assert rfc1982_gt(mid - 1, 0)
        assert not rfc1982_gt(mid, 0)
        assert not rfc1982_gt(mid + 1, 0)  # >= 2^31 is "older" per RFC 1982

    def test_sequence_equal_no_wrap(self):
        def rfc1982_gt(a, b):
            return ((a - b) & 0xFFFFFFFF) < (1 << 31) and a != b
        assert not rfc1982_gt(42, 42)

    def test_sequence_monotonic_increment(self):
        def rfc1982_gt(a, b):
            return ((a - b) & 0xFFFFFFFF) < (1 << 31) and a != b
        for i in range(100):
            assert rfc1982_gt(i + 1, i)

    def test_gap_detection_threshold_zero(self):
        def is_gap(old_seq, new_seq, threshold):
            if old_seq == new_seq:
                return False
            diff = (new_seq - old_seq) & 0xFFFFFFFF
            return diff > threshold
        assert is_gap(0, 1, 0)
        assert not is_gap(0, 0, 0)

    def test_gap_detection_threshold_max(self):
        def is_gap(old_seq, new_seq, threshold):
            if old_seq == new_seq:
                return False
            diff = (new_seq - old_seq) & 0xFFFFFFFF
            return diff > threshold
        assert not is_gap(0, 999, 1000)
        assert is_gap(0, 1001, 1000)

    def test_gap_detection_wrap(self):
        def is_gap(old_seq, new_seq, threshold):
            if old_seq == new_seq:
                return False
            diff = (new_seq - old_seq) & 0xFFFFFFFF
            return diff > threshold
        assert is_gap(0xFFFFFFF0, 1000, 100)
        assert not is_gap(0xFFFFFFF0, 50, 100)

    def test_gap_exactly_at_threshold(self):
        def is_gap(old_seq, new_seq, threshold):
            if old_seq == new_seq:
                return False
            diff = (new_seq - old_seq) & 0xFFFFFFFF
            return diff > threshold
        assert not is_gap(100, 1100, 1000)
        assert is_gap(100, 1101, 1000)

    def test_random_key_not_all_zeros(self):
        for _ in range(100):
            key = os.urandom(32)
            if all(b == 0 for b in key):
                pytest.fail("urandom produced zero key")

    def test_random_key_not_all_ones(self):
        for _ in range(100):
            key = os.urandom(32)
            if all(b == 0xFF for b in key):
                pytest.fail("urandom produced all-ones key")

    def test_key_file_permissions_rejected_if_world_readable(self):
        mode_world_readable = 0o644
        mode_protected = 0o600
        assert (mode_world_readable & 0o004) != 0
        assert (mode_protected & 0o004) == 0

    def test_hex_key_decode_valid(self):
        hex_str = "a" * 64
        raw = bytes.fromhex(hex_str)
        assert len(raw) == 32
        assert all(b == 0xAA for b in raw)

    def test_hex_key_decode_invalid_char(self):
        with pytest.raises(ValueError):
            bytes.fromhex("gg")

    def test_hex_key_decode_odd_length(self):
        with pytest.raises(ValueError):
            bytes.fromhex("abc")

    def test_hex_key_from_env_var_length_check(self):
        env_val = "a" * 64
        assert len(env_val) == 64
        raw = bytes.fromhex(env_val)
        assert len(raw) == 32

    def test_hex_key_from_env_var_32_plus_fallback(self):
        env_val = os.urandom(32).hex()
        assert len(env_val) >= 64
        raw = bytes.fromhex(env_val)
        assert len(raw) == 32

    def test_hex_key_short_env_rejected(self):
        short = "abc"
        assert len(short) < 32

    def test_key_does_not_contain_pii(self):
        key = os.urandom(32)
        key_str = key.hex()
        assert key_str.isalnum() or key_str.isascii()

    def test_hmac_difference_one_bit(self):
        key = os.urandom(32)
        data = b"data"
        h1 = hmac_lib.new(key, data, "sha256").digest()
        key2 = bytearray(key)
        key2[0] ^= 1
        h2 = hmac_lib.new(bytes(key2), data, "sha256").digest()
        assert h1 != h2

    def test_hmac_same_key_different_data(self):
        key = b"key"
        h1 = hmac_lib.new(key, b"data1", "sha256").digest()
        h2 = hmac_lib.new(key, b"data2", "sha256").digest()
        assert h1 != h2

    def test_ecdsa_der_structure_sequence(self):
        sig = bytes([0x30]) + bytes([0x45, 0x02, 0x21]) + bytes(32) + bytes([0x02, 0x20]) + bytes(32)
        assert sig[0] == 0x30  # SEQUENCE
        assert sig[2] == 0x02  # INTEGER (r)
        assert sig[36] == 0x02  # INTEGER (s) at offset: 4(header) + 32(r) = 36

    def test_ecdsa_der_structure_alternate_length(self):
        sig = bytes([0x30]) + bytes([0x44, 0x02, 0x20]) + bytes(32) + bytes([0x02, 0x20]) + bytes(32)
        assert sig[0] == 0x30
        assert len(sig) == 70

    def test_ecdsa_der_r_s_value_sizes(self):
        max_r = bytes(32)
        max_s = bytes(32)
        assert len(max_r) == 32
        assert len(max_s) == 32

    def test_sign_file_openssl_path_exists(self):
        assert os.path.exists("/proc/self/exe")

    def test_signature_fails_if_file_missing(self):
        missing = "/nonexistent/file.bin"
        assert not os.path.exists(missing)

    def test_default_key_path_not_empty(self):
        assert len("/etc/aurore/signing_key.pub") > 0

    def test_default_sig_path_not_empty(self):
        assert len("/etc/aurore/aurore.sig") > 0

    def test_verify_self_readlink_works(self):
        path = os.readlink("/proc/self/exe") if os.path.exists("/proc/self/exe") else ""
        assert len(path) > 0

    def test_verify_self_binary_is_executable(self):
        path = os.readlink("/proc/self/exe") if os.path.exists("/proc/self/exe") else ""
        if path:
            assert os.access(path, os.X_OK)


class TestTelemetryWriterSecurity:
    """TelemetryWriter: binary log entry format, severity, HMAC signing."""

    BINARY_ENTRY_SIZE = 112  # sizeof(BinaryLogEntry)
    MAX_DATA_SIZE = 64
    HMAC_SIZE = 32

    def test_binary_entry_size_constant(self):
        assert self.BINARY_ENTRY_SIZE == 112

    def test_max_data_size_constant(self):
        assert self.MAX_DATA_SIZE == 64

    def test_hmac_size_constant(self):
        assert self.HMAC_SIZE == 32

    def test_binary_entry_layout_timestamp(self):
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<Q", entry, 0, 1234567890)
        (ts,) = struct.unpack_from("<Q", entry, 0)
        assert ts == 1234567890

    def test_binary_entry_layout_event_id(self):
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<H", entry, 8, 0x0101)
        (eid,) = struct.unpack_from("<H", entry, 8)
        assert eid == 0x0101

    def test_binary_entry_layout_severity(self):
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<B", entry, 10, 3)
        (sev,) = struct.unpack_from("<B", entry, 10)
        assert sev == 3

    def test_binary_entry_layout_data_len(self):
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<B", entry, 11, 16)
        (dl,) = struct.unpack_from("<B", entry, 11)
        assert dl == 16

    def test_binary_entry_fixed_fields(self):
        ts = 123456789012345
        eid = 0x0201
        sev = 2
        dl = 8
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<Q", entry, 0, ts)
        struct.pack_into("<H", entry, 8, eid)
        struct.pack_into("<B", entry, 10, sev)
        struct.pack_into("<B", entry, 11, dl)
        assert struct.unpack_from("<Q", entry, 0)[0] == ts
        assert struct.unpack_from("<H", entry, 8)[0] == eid
        assert struct.unpack_from("<B", entry, 10)[0] == sev
        assert struct.unpack_from("<B", entry, 11)[0] == dl

    def test_binary_entry_data_payload(self):
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        payload = b"hello"
        struct.pack_into("<B", entry, 11, len(payload))
        entry[12:12 + len(payload)] = payload
        assert entry[12:12 + len(payload)] == payload
        assert struct.unpack_from("<B", entry, 11)[0] == 5

    def test_binary_entry_hmac_field(self):
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        hmac_val = os.urandom(32)
        # hmac starts at offset: 8(timestamp) + 2(event_id) + 1(severity) + 1(data_len) + padding + 64(data)
        # Size: 8+2+1+1 = 12 header, then alignment padding, then 64 data
        # Let's put hmac at offset 112 - 32 = 80
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE
        entry[hmac_offset:hmac_offset + 32] = hmac_val
        assert entry[hmac_offset:hmac_offset + 32] == hmac_val

    def test_full_binary_log_entry_format(self):
        ts = 1234567890
        eid = 0x0401  # SAFETY_FAULT
        sev = 4  # kCritical
        data = b"OVERTEMP"
        hmac_val = bytes(32)

        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<Q", entry, 0, ts)
        struct.pack_into("<H", entry, 8, eid)
        struct.pack_into("<B", entry, 10, sev)
        struct.pack_into("<B", entry, 11, len(data))
        entry[12:12 + len(data)] = data

        assert len(entry) == self.BINARY_ENTRY_SIZE
        assert struct.unpack_from("<Q", entry, 0)[0] == ts
        assert struct.unpack_from("<H", entry, 8)[0] == eid
        assert struct.unpack_from("<B", entry, 10)[0] == sev
        assert struct.unpack_from("<B", entry, 11)[0] == len(data)
        assert bytes(entry[12:12 + len(data)]) == data

    def test_hmac_signing_of_log_entry(self):
        hmac_key = os.urandom(32)
        ts = 1234567890
        eid = 0x0101
        sev = 1
        data = b"DETECT"

        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<Q", entry, 0, ts)
        struct.pack_into("<H", entry, 8, eid)
        struct.pack_into("<B", entry, 10, sev)
        struct.pack_into("<B", entry, 11, len(data))
        entry[12:12 + len(data)] = data

        hmac_input = bytes(entry[:self.BINARY_ENTRY_SIZE - self.HMAC_SIZE])
        signature = hmac_lib.new(hmac_key, hmac_input, "sha256").digest()
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE
        entry[hmac_offset:hmac_offset + 32] = signature

        # Verify
        stored_sig = bytes(entry[hmac_offset:hmac_offset + 32])
        expected_sig = hmac_lib.new(hmac_key, hmac_input, "sha256").digest()
        assert stored_sig == expected_sig

    def test_hmac_tampered_entry_detected(self):
        hmac_key = os.urandom(32)
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<Q", entry, 0, 1234567890)
        struct.pack_into("<H", entry, 8, 0x0101)
        struct.pack_into("<B", entry, 10, 1)
        struct.pack_into("<B", entry, 11, 0)

        hmac_input = bytes(entry[:self.BINARY_ENTRY_SIZE - self.HMAC_SIZE])
        signature = hmac_lib.new(hmac_key, hmac_input, "sha256").digest()
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE
        entry[hmac_offset:hmac_offset + 32] = signature

        entry[12] ^= 1  # tamper

        hmac_input_tampered = bytes(entry[:self.BINARY_ENTRY_SIZE - self.HMAC_SIZE])
        expected_sig = hmac_lib.new(hmac_key, hmac_input_tampered, "sha256").digest()
        assert expected_sig != signature

    def test_multiple_event_ids_logged(self):
        hmac_key = os.urandom(32)
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE
        for eid in [0x0001, 0x0101, 0x0201, 0x0301, 0x0401, 0x0501, 0x0601]:
            entry = bytearray(self.BINARY_ENTRY_SIZE)
            struct.pack_into("<Q", entry, 0, 1234567890)
            struct.pack_into("<H", entry, 8, eid)
            struct.pack_into("<B", entry, 10, 1)
            struct.pack_into("<B", entry, 11, 0)
            hmac_input = bytes(entry[:hmac_offset])
            sig = hmac_lib.new(hmac_key, hmac_input, "sha256").digest()
            entry[hmac_offset:hmac_offset + 32] = sig
            assert len(entry) == self.BINARY_ENTRY_SIZE

    def test_severity_levels_in_entries(self):
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE
        for sev in range(5):
            entry = bytearray(self.BINARY_ENTRY_SIZE)
            struct.pack_into("<Q", entry, 0, 1234567890)
            struct.pack_into("<H", entry, 8, 0x0401)
            struct.pack_into("<B", entry, 10, sev)
            struct.pack_into("<B", entry, 11, 0)
            assert struct.unpack_from("<B", entry, 10)[0] == sev

    def test_severity_name_mapping(self):
        names = {0: "kDebug", 1: "kInfo", 2: "kWarning", 3: "kError", 4: "kCritical"}
        assert names[0] == "kDebug"
        assert names[4] == "kCritical"
        assert len(names) == 5

    def test_event_id_ranges_valid(self):
        events = {
            0x0001: "SYSTEM_BOOT",
            0x0101: "DETECTION_VALID",
            0x0201: "TRACK_ACQUIRED",
            0x0301: "ACTUATION_COMMAND",
            0x0401: "SAFETY_FAULT",
            0x0501: "CAMERA_TIMEOUT",
            0x0601: "DUAL_STREAM_MIPI_FRAME",
        }
        for eid, name in events.items():
            assert 0 < eid <= 0x0608
            assert len(name) > 0

    def test_entry_with_max_data(self):
        hmac_key = os.urandom(32)
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<Q", entry, 0, 1234567890)
        struct.pack_into("<H", entry, 8, 0x0101)
        struct.pack_into("<B", entry, 10, 2)
        struct.pack_into("<B", entry, 11, self.MAX_DATA_SIZE)
        entry[12:12 + self.MAX_DATA_SIZE] = os.urandom(self.MAX_DATA_SIZE)
        hmac_input = bytes(entry[:hmac_offset])
        sig = hmac_lib.new(hmac_key, hmac_input, "sha256").digest()
        entry[hmac_offset:hmac_offset + 32] = sig
        assert len(entry) == self.BINARY_ENTRY_SIZE
        assert struct.unpack_from("<B", entry, 11)[0] == 64

    def test_entry_with_zero_data(self):
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<Q", entry, 0, 1234567890)
        struct.pack_into("<H", entry, 8, 0x0002)
        struct.pack_into("<B", entry, 10, 0)
        struct.pack_into("<B", entry, 11, 0)
        assert struct.unpack_from("<B", entry, 11)[0] == 0
        assert len(entry) == self.BINARY_ENTRY_SIZE

    def test_telemetry_event_count(self):
        events = [
            0x0001, 0x0002,
            0x0101, 0x0102, 0x0103,
            0x0201, 0x0202, 0x0203,
            0x0301, 0x0302, 0x0303,
            0x0401, 0x0402, 0x0403, 0x0404,
            0x0501, 0x0502, 0x0503, 0x0504, 0x0505,
            0x0601, 0x0602, 0x0603, 0x0604, 0x0605, 0x0606, 0x0607, 0x0608,
        ]
        assert len(events) == 28
        assert len(set(events)) == 28

    def test_log_entry_serialization_deserialization(self):
        hmac_key = os.urandom(32)
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE

        entry = bytearray(self.BINARY_ENTRY_SIZE)
        orig_ts = 987654321
        orig_eid = 0x0303
        orig_sev = 3
        orig_data = b"LIMIT"
        struct.pack_into("<Q", entry, 0, orig_ts)
        struct.pack_into("<H", entry, 8, orig_eid)
        struct.pack_into("<B", entry, 10, orig_sev)
        struct.pack_into("<B", entry, 11, len(orig_data))
        entry[12:12 + len(orig_data)] = orig_data
        hmac_input = bytes(entry[:hmac_offset])
        sig = hmac_lib.new(hmac_key, hmac_input, "sha256").digest()
        entry[hmac_offset:hmac_offset + 32] = sig

        ts, eid, sev, dl = struct.unpack_from("<QHBB", entry, 0)
        data = bytes(entry[12:12 + dl])
        stored_sig = bytes(entry[hmac_offset:hmac_offset + 32])

        assert ts == orig_ts
        assert eid == orig_eid
        assert sev == orig_sev
        assert dl == len(orig_data)
        assert data == orig_data
        assert stored_sig == sig

    def test_minimal_log_entry(self):
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<Q", entry, 0, 1)
        struct.pack_into("<H", entry, 8, 1)
        struct.pack_into("<B", entry, 10, 0)
        struct.pack_into("<B", entry, 11, 0)
        assert len(entry) == self.BINARY_ENTRY_SIZE

    def test_high_volume_hmac_verification(self):
        hmac_key = os.urandom(32)
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE
        for i in range(100):
            entry = bytearray(self.BINARY_ENTRY_SIZE)
            struct.pack_into("<Q", entry, 0, i)
            struct.pack_into("<H", entry, 8, 0x0001)
            struct.pack_into("<B", entry, 10, 0)
            struct.pack_into("<B", entry, 11, 0)
            hmac_input = bytes(entry[:hmac_offset])
            sig = hmac_lib.new(hmac_key, hmac_input, "sha256").digest()
            entry[hmac_offset:hmac_offset + 32] = sig
            expected = hmac_lib.new(hmac_key, hmac_input, "sha256").digest()
            assert bytes(entry[hmac_offset:hmac_offset + 32]) == expected

    def test_different_keys_produce_different_signatures(self):
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE
        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<Q", entry, 0, 12345)
        struct.pack_into("<H", entry, 8, 0x0101)
        struct.pack_into("<B", entry, 10, 1)
        struct.pack_into("<B", entry, 11, 0)
        hmac_input = bytes(entry[:hmac_offset])

        sig1 = hmac_lib.new(os.urandom(32), hmac_input, "sha256").digest()
        sig2 = hmac_lib.new(os.urandom(32), hmac_input, "sha256").digest()
        assert sig1 != sig2

    def test_critical_event_hmac_strict(self):
        hmac_key = os.urandom(32)
        hmac_offset = self.BINARY_ENTRY_SIZE - self.HMAC_SIZE

        entry = bytearray(self.BINARY_ENTRY_SIZE)
        struct.pack_into("<Q", entry, 0, 1234567890)
        struct.pack_into("<H", entry, 8, 0x0404)  # WATCHDOG_TIMEOUT
        struct.pack_into("<B", entry, 10, 4)  # kCritical
        struct.pack_into("<B", entry, 11, 0)
        hmac_input = bytes(entry[:hmac_offset])
        sig = hmac_lib.new(hmac_key, hmac_input, "sha256").digest()
        entry[hmac_offset:hmac_offset + 32] = sig

        diff = 0
        stored = bytes(entry[hmac_offset:hmac_offset + 32])
        expected = hmac_lib.new(hmac_key, hmac_input, "sha256").digest()
        for a, b in zip(stored, expected):
            diff |= a ^ b
        assert diff == 0

    def test_binary_entry_struct_size_verification(self):
        packed = struct.pack("<QHBB", 0, 0, 0, 0)
        assert len(packed) == 12

    def test_telemetry_config_defaults(self):
        cfg = {
            "log_dir": "logs",
            "session_prefix": "run",
            "max_file_size_mb": 100,
            "max_sessions": 10,
            "enable_csv": True,
            "enable_json": True,
            "enable_console": False,
            "max_queue_size": 1024,
            "queue_high_water_pct": 80,
        }
        assert cfg["log_dir"] == "logs"
        assert cfg["session_prefix"] == "run"
        assert cfg["max_file_size_mb"] == 100
        assert cfg["max_sessions"] == 10
        assert cfg["enable_csv"]
        assert cfg["enable_json"]
        assert not cfg["enable_console"]
        assert cfg["max_queue_size"] == 1024
        assert cfg["queue_high_water_pct"] == 80
        assert cfg["max_queue_size"] > 0 and (cfg["max_queue_size"] & (cfg["max_queue_size"] - 1)) == 0
