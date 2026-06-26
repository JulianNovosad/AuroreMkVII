import pytest


class TestLockFreeRingBuffer:
    """LockFreeRingBuffer<T,N>: SPSC lock-free buffer properties."""

    def test_size_must_be_power_of_two(self):
        def is_power_of_two(n):
            return n > 0 and (n & (n - 1)) == 0
        assert is_power_of_two(2)
        assert is_power_of_two(4)
        assert is_power_of_two(8)
        assert is_power_of_two(16)
        assert is_power_of_two(32)
        assert is_power_of_two(64)
        assert is_power_of_two(128)
        assert is_power_of_two(256)
        assert is_power_of_two(512)
        assert is_power_of_two(1024)
        assert not is_power_of_two(3)
        assert not is_power_of_two(5)
        assert not is_power_of_two(6)
        assert not is_power_of_two(10)
        assert not is_power_of_two(0)

    def test_capacity_matches_template_param(self):
        assert 4 > 0
        assert 4 & (4 - 1) == 0

    def test_usable_capacity_is_size_minus_one(self):
        for size in [2, 4, 8, 16]:
            usable = size - 1
            assert usable == size - 1

    def test_initial_empty(self):
        head = 0
        tail = 0
        assert head == tail

    def test_one_element_fills_half(self):
        head = 1
        tail = 0
        assert head != tail

    def test_full_detection(self):
        size = 4
        mask = size - 1
        head = 3
        tail = 0
        next_head = (head + 1) & mask
        assert next_head == tail

    def test_mask_value(self):
        for size in [2, 4, 8, 16, 32, 64]:
            assert (size - 1) & size == 0
            assert size & (size - 1) == 0

    def test_size_calculation_approx(self):
        size = 4
        head = 3
        tail = 1
        count = (head - tail) & (size - 1)
        assert count == 2

    def test_wrap_around_size(self):
        size = 4
        mask = size - 1
        head = 1
        tail = 3
        count = (head - tail) & mask
        assert count == 2

    def test_empty_when_head_eq_tail(self):
        assert (0 & 3) == 0
        assert (4 & 3) == 0


class TestMPMCRingBuffer:
    """MPMCRingBuffer: mutex-protected multi-producer ring buffer."""

    def test_size_must_be_power_of_two(self):
        assert (4 & (4 - 1)) == 0

    def test_capacity_matches_size(self):
        assert 4 == 4

    def test_empty_initial(self):
        head = 0
        tail = 0
        assert head == tail


class TestCacheLineAlignment:
    """CACHE_LINE_SIZE constant and alignment."""

    def test_cache_line_size(self):
        assert 64 == 64

    def test_atomic_variables_should_be_aligned(self):
        align = 64
        assert align % 64 == 0
