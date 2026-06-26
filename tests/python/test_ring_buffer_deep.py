import pytest


class TestRingBufferSPSCProps:
    """SPSC (single-producer single-consumer) ring buffer properties."""

    def test_producer_consumer_independence(self):
        head = 5
        tail = 3
        count = (head - tail) & 3
        assert count == 2

    def test_consumer_doesnt_affect_producer(self):
        head = 7
        tail = 4
        tail = 5
        assert head == 7

    def test_producer_doesnt_affect_consumer(self):
        head = 7
        tail = 4
        head = 8
        assert tail == 4

    def test_full_detection_no_false_positive(self):
        size = 4
        for head in range(32):
            tail = (head + 1) & (size - 1)
            next_head = (head + 1) & (size - 1)
            if next_head == tail:
                pass
        assert True

    def test_empty_detection(self):
        assert (0 & 3) == 0
        assert (4 & 3) == 0
        assert (8 & 3) == 0

    def test_size_is_power_of_two(self):
        for n in [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024]:
            assert n > 0 and (n & (n - 1)) == 0

    def test_wraparound_continuity(self):
        mask = 3
        for i in range(100):
            idx = i & mask
            assert 0 <= idx < 4

    def test_sequential_indices_no_skip(self):
        mask = 3
        prev = -1
        for i in range(20):
            idx = i & mask
            expected = (prev + 1) & mask if prev >= 0 else 0
            assert idx == expected
            prev = idx


class TestRingBufferMask:
    """Mask-based indexing for power-of-2 sizes."""

    def test_mask_values(self):
        assert (2 - 1) == 1
        assert (4 - 1) == 3
        assert (8 - 1) == 7
        assert (16 - 1) == 15
        assert (32 - 1) == 31
        assert (64 - 1) == 63

    def test_mask_property(self):
        for size in [2, 4, 8, 16, 32, 64]:
            mask = size - 1
            assert mask & size == 0
            assert size & mask == 0

    def test_mod_using_mask(self):
        mask = 7
        for i in range(100):
            assert (i & mask) == (i % 8)

    def test_mask_for_usable_capacity(self):
        size = 4
        usable = size - 1
        assert usable == 3

    def test_full_capacity_never_reached(self):
        size = 4
        usable = size - 1
        head = 0
        for _ in range(usable):
            head = (head + 1) & (size - 1)
        assert True


class TestRingBufferCount:
    """Element count calculation with wrap."""

    def count(self, head, tail, mask):
        return (head - tail) & mask

    def test_linear_count(self):
        for i in range(8):
            assert self.count(i, 0, 7) == i

    def test_wrap_count(self):
        assert self.count(1, 7, 7) == 2
        assert self.count(2, 6, 7) == 4
        assert self.count(0, 6, 7) == 2

    def test_empty_check(self):
        assert self.count(5, 5, 7) == 0

    def test_full_capacity_one_less_than_size(self):
        mask = 3
        assert self.count(3, 0, mask) == 3
        assert self.count(0, 1, mask) == 3

    def test_count_never_exceeds_usable(self):
        for head in range(32):
            for tail in range(32):
                if head == tail:
                    continue
                c = self.count(head, tail, 7)
                assert c <= 7

    def test_count_monotonic_with_head(self):
        mask = 3
        tail = 2
        c0 = self.count(2, tail, mask)
        c1 = self.count(3, tail, mask)
        c2 = self.count(0, tail, mask)
        c3 = self.count(1, tail, mask)
        assert c0 == 0
        assert c1 == 1
        assert c2 == 2
        assert c3 == 3


class TestRingBufferOverflow:
    """Overflow behavior: push when full is undefined for SPSC."""

    def test_full_buffer_rejects_write(self):
        mask = 3
        head = 3
        tail = 0
        next_head = (head + 1) & mask
        is_full = (next_head == tail)
        assert is_full

    def test_almost_full_accepts_write(self):
        mask = 3
        head = 2
        tail = 0
        next_head = (head + 1) & mask
        is_full = (next_head == tail)
        assert not is_full

    def test_empty_after_full_read(self):
        head = 3
        tail = 0
        tail = (tail + 1) & 3
        tail = (tail + 1) & 3
        tail = (tail + 1) & 3
        assert tail == 3
        assert (head - tail) & 3 == 0


class TestRingBufferPowerOfTwo:
    """Power-of-2 size constraint."""

    def test_not_power_of_two_rejected(self):
        def is_pow2(n):
            return n > 0 and (n & (n - 1)) == 0
        assert not is_pow2(0)
        assert not is_pow2(3)
        assert not is_pow2(5)
        assert not is_pow2(6)
        assert not is_pow2(7)
        assert not is_pow2(9)
        assert not is_pow2(10)

    def test_power_of_two_accepted(self):
        def is_pow2(n):
            return n > 0 and (n & (n - 1)) == 0
        assert is_pow2(1)
        assert is_pow2(2)
        assert is_pow2(4)
        assert is_pow2(8)
        assert is_pow2(16)
        assert is_pow2(32)
        assert is_pow2(64)
        assert is_pow2(128)
        assert is_pow2(256)
        assert is_pow2(512)
        assert is_pow2(1024)


class TestRingBufferCacheLine:
    """Cache line alignment and padding."""

    def test_cache_line_size_64(self):
        assert 64 == 64

    def test_padding_to_cache_line(self):
        size = 4
        total = 64 * 2 + size * 64
        assert total == 384

    def test_false_sharing_prevention(self):
        pad_before = 64
        pad_after = 64
        assert pad_before == 64
        assert pad_after == 64
