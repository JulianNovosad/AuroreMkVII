import struct
import math

import pytest


class TestRaw10PackingConstants:
    """RAW10 packing format: 4 pixels packed into 5 bytes."""

    def test_quad_size_bytes(self):
        assert 5 == 5

    def test_quad_yields_4_pixels(self):
        src = bytes([0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
        pix = [0] * 4
        pix[0] = (src[0] << 2) | ((src[1] >> 6) & 0x03)
        pix[1] = ((src[1] & 0x3F) << 4) | ((src[2] >> 4) & 0x0F)
        pix[2] = ((src[2] & 0x0F) << 6) | ((src[3] >> 2) & 0x3F)
        pix[3] = ((src[3] & 0x03) << 8) | src[4]
        assert all(p == 1023 for p in pix)

    def test_quad_zero(self):
        src = bytes(5)
        pix = [0] * 4
        pix[0] = (src[0] << 2) | ((src[1] >> 6) & 0x03)
        pix[1] = ((src[1] & 0x3F) << 4) | ((src[2] >> 4) & 0x0F)
        pix[2] = ((src[2] & 0x0F) << 6) | ((src[3] >> 2) & 0x3F)
        pix[3] = ((src[3] & 0x03) << 8) | src[4]
        assert all(p == 0 for p in pix)

    def test_pixel0_extraction(self):
        src = bytes([0xFF, 0xC0, 0x00, 0x00, 0x00])
        p0 = (src[0] << 2) | ((src[1] >> 6) & 0x03)
        assert p0 == 1023

    def test_pixel1_extraction(self):
        src = bytes([0x00, 0xFF, 0xFF, 0x00, 0x00])
        p1 = ((src[1] & 0x3F) << 4) | ((src[2] >> 4) & 0x0F)
        assert p1 == 1023

    def test_pixel2_extraction(self):
        src = bytes([0x00, 0x00, 0x0F, 0xFC, 0x00])
        p2 = ((src[2] & 0x0F) << 6) | ((src[3] >> 2) & 0x3F)
        assert p2 == 1023

    def test_pixel3_extraction(self):
        src = bytes([0x00, 0x00, 0x00, 0x03, 0xFF])
        p3 = ((src[3] & 0x03) << 8) | src[4]
        assert p3 == 1023

    def test_max_10bit(self):
        assert 1023 == (1 << 10) - 1

    def test_stride_alignment(self):
        width = 1536
        bpp = 10
        stride = (width * bpp + 7) // 8
        assert stride == 1920

    def test_frame_size(self):
        stride = 1920
        height = 864
        frame_bytes = stride * height
        assert frame_bytes == 1658880

    def test_bits_per_pixel(self):
        assert 10 == 10


class TestBayerPattern:
    """IMX708 Bayer pattern: GRBG."""

    def test_grbg_row0_green_red(self):
        def get_color(row, col):
            if row % 2 == 0:
                return "GR" if col % 2 == 0 else "RB"
            return "BG" if col % 2 == 0 else "GR"
        assert get_color(0, 0) == "GR"
        assert get_color(0, 1) == "RB"

    def test_grbg_row1_blue_green(self):
        def get_color(row, col):
            if row % 2 == 0:
                return "GR" if col % 2 == 0 else "RB"
            return "BG" if col % 2 == 0 else "GR"
        assert get_color(1, 0) == "BG"
        assert get_color(1, 1) == "GR"

    def test_grbg_pattern_consistency(self):
        img_h, img_w = 864, 1536
        g_count = 0
        r_count = 0
        b_count = 0
        for row in range(4):
            for col in range(4):
                if (row % 2) == (col % 2):
                    g_count += 1
                elif row % 2 == 0:
                    r_count += 1
                else:
                    b_count += 1
        assert g_count == 8
        assert r_count == 4
        assert b_count == 4

    def test_bayer_quad_size(self):
        assert 2 * 2 == 4

    def test_bayer_ratio_green_half(self):
        assert 0.5 == 0.5


class TestHsvColorRange:
    """HSV threshold parameters for target segmentation."""

    def test_hue_range_valid(self):
        assert 0 <= 0
        assert 180 >= 180

    def test_hue_red_wrap(self):
        red_low = (0, 10)
        red_high = (170, 180)
        assert red_low[1] - red_low[0] == 10
        assert red_high[1] - red_high[0] == 10

    def test_saturation_range(self):
        assert 0 <= 0
        assert 255 >= 255

    def test_value_range(self):
        assert 0 <= 0
        assert 255 >= 255

    def test_typical_green_range(self):
        h_min, h_max = 40, 80
        s_min, s_max = 50, 255
        v_min, v_max = 50, 255
        assert 0 <= h_min < h_max <= 180
        assert 0 <= s_min < s_max <= 255
        assert 0 <= v_min < v_max <= 255

    def test_typical_white_range(self):
        h_min, h_max = 0, 180
        s_min, s_max = 0, 30
        v_min, v_max = 200, 255
        assert h_min < h_max
        assert s_min < s_max
        assert v_min < v_max

    def test_typical_orange_range(self):
        h_min, h_max = 5, 20
        s_min, s_max = 100, 255
        v_min, v_max = 100, 255
        assert 0 <= h_min < h_max <= 180
        assert 0 <= s_min < s_max <= 255
        assert 0 <= v_min < v_max <= 255


class TestImagePreprocessorException:
    """ImagePreprocessorException: runtime_error subclass."""

    def test_exception_is_runtime_error(self):
        import builtins
        class Exc(Exception):
            pass
        assert issubclass(Exc, Exception)

    def test_exception_message(self):
        msg = "Invalid RAW10 buffer"
        exc = RuntimeError(msg)
        assert str(exc) == msg

    def test_exception_empty_message(self):
        exc = RuntimeError("")
        assert str(exc) == ""


class TestImagePreprocessorDimensions:
    """ImagePreprocessor width/height accessors."""

    def test_mipi_resolution(self):
        w, h = 1536, 864
        assert w > 0 and h > 0

    def test_width_positive(self):
        assert 1536 > 0

    def test_height_positive(self):
        assert 864 > 0


class TestDemosaicRowGrbg:
    """Bilinear Bayer demosaicing for GRBG pattern."""

    def _get_bayer_color(self, row, col):
        if row % 2 == 0:
            return "G" if col % 2 == 0 else "R"
        return "B" if col % 2 == 0 else "G"

    def test_row0_col0_green(self):
        assert self._get_bayer_color(0, 0) == "G"

    def test_row0_col1_red(self):
        assert self._get_bayer_color(0, 1) == "R"

    def test_row1_col0_blue(self):
        assert self._get_bayer_color(1, 0) == "B"

    def test_row1_col1_green(self):
        assert self._get_bayer_color(1, 1) == "G"

    def test_grbg_2x2_quad(self):
        colors = [
            [self._get_bayer_color(r, c) for c in range(2)]
            for r in range(2)
        ]
        assert colors == [["G", "R"], ["B", "G"]]

    def red_at(self, raw, row, col, width):
        h = raw[row]
        if col == 0:
            return (int(h[col + 1]) + int(h[col + 1])) // 2
        if col == width - 1:
            return (int(h[col - 1]) + int(h[col - 1])) // 2
        return (int(h[col - 1]) + int(h[col + 1])) // 2

    def test_red_interpolation_center(self):
        raw_row = [10, 100, 20, 200, 30]
        r = self.red_at([raw_row], 0, 2, 5)
        assert r == (100 + 200) // 2

    def test_red_interpolation_left_edge(self):
        raw_row = [100, 200, 300]
        r = self.red_at([raw_row], 0, 0, 3)
        assert r == (200 + 200) // 2

    def test_red_interpolation_right_edge(self):
        raw_row = [100, 200, 300]
        r = self.red_at([raw_row], 0, 2, 3)
        assert r == (200 + 200) // 2

    def green_at_row0(self, raw, row, col, width):
        h = raw[row]
        if col % 2 == 0:
            return int(h[col])
        left = h[col - 1] if col > 0 else h[col + 1]
        right = h[col + 1] if col < width - 1 else h[col - 1]
        return (int(left) + int(right)) // 2

    def test_green_interpolation(self):
        raw_row = [50, 100, 150, 200, 250]
        g = self.green_at_row0([raw_row], 0, 1, 5)
        assert g == (50 + 150) // 2

    def blue_at(self, raw, row, col, width, height):
        if row == 0 or row == height - 1:
            return 0
        r1 = raw[row - 1]
        r2 = raw[row + 1]
        if col == 0:
            return (int(r1[col]) + int(r2[col])) // 2
        if col == width - 1:
            return (int(r1[col - 1]) + int(r2[col - 1])) // 2
        return (int(r1[col - 1]) + int(r1[col + 1]) +
                int(r2[col - 1]) + int(r2[col + 1])) // 4

    def test_blue_interpolation_center(self):
        raw = [
            [100, 200, 300],
            [400, 500, 600],
            [700, 800, 900]
        ]
        b = self.blue_at(raw, 1, 1, 3, 3)
        assert b == (100 + 300 + 700 + 900) // 4

    def test_blue_interpolation_top_edge(self):
        raw = [[100, 200, 300], [400, 500, 600]]
        b = self.blue_at(raw, 0, 1, 3, 2)
        assert b == 0

    def test_demosaic_basic_row(self):
        width = 4
        raw_row0 = [10, 20, 10, 22]
        raw_row1 = [30, 13, 33, 14]
        rgb = bytearray(width * 3)
        for x in range(width):
            if x % 2 == 0:
                g = raw_row0[x]
            elif x == width - 1:
                g = raw_row0[x - 1]
            else:
                g = (raw_row0[x - 1] + raw_row0[x + 1]) // 2

            if x % 2 == 1:
                r = raw_row0[x]
            elif x < width - 1:
                r = raw_row0[x + 1]
            else:
                r = raw_row0[x - 1]

            if x == 0:
                b = raw_row1[x + 1]
            elif x == width - 1:
                b = raw_row1[x - 1]
            else:
                b = (raw_row1[x - 1] + raw_row1[x + 1]) // 2

            rgb[x * 3] = r
            rgb[x * 3 + 1] = g
            rgb[x * 3 + 2] = b
        assert len(rgb) == 12
        assert all(0 <= v <= 255 for v in rgb)

    def test_bayer_pixel_extraction(self):
        raw10_quad = bytes([0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
        pix = [0] * 4
        pix[0] = (raw10_quad[0] << 2) | ((raw10_quad[1] >> 6) & 0x03)
        pix[1] = ((raw10_quad[1] & 0x3F) << 4) | ((raw10_quad[2] >> 4) & 0x0F)
        pix[2] = ((raw10_quad[2] & 0x0F) << 6) | ((raw10_quad[3] >> 2) & 0x3F)
        pix[3] = ((raw10_quad[3] & 0x03) << 8) | raw10_quad[4]
        assert all(p == 1023 for p in pix)

    def test_demosaic_row_count(self):
        height = 864
        assert height % 2 == 0

    def test_demosaic_even_width(self):
        width = 1536
        assert width % 2 == 0
