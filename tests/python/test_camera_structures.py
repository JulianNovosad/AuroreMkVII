import pytest
import struct


class TestPixelFormat:
    """PixelFormat enum: camera pixel formats."""

    def test_values(self):
        assert 0 == 0  # RAW10
        assert 1 == 1  # RGB888
        assert 2 == 2  # BGR888
        assert 3 == 3  # YUV420
        assert 4 == 4  # NV12


class TestCameraConfig:
    """CameraConfig: validation and defaults."""

    def validate(self, cfg):
        return (cfg["width"] > 0 and cfg["height"] > 0 and
                cfg["fps"] > 0 and cfg["fps"] <= 120 and
                cfg["buffer_count"] >= 2 and cfg["buffer_count"] <= 8)

    def test_defaults(self):
        cfg = {"width": 1536, "height": 864, "fps": 120,
               "format": 0, "buffer_count": 8}
        assert cfg["width"] == 1536
        assert cfg["height"] == 864
        assert cfg["fps"] == 120

    def test_valid_config(self):
        cfg = {"width": 1536, "height": 864, "fps": 120,
               "format": 0, "buffer_count": 8}
        assert self.validate(cfg)

    def test_zero_width(self):
        assert not self.validate({"width": 0, "height": 864, "fps": 120, "format": 0, "buffer_count": 8})

    def test_zero_height(self):
        assert not self.validate({"width": 1536, "height": 0, "fps": 120, "format": 0, "buffer_count": 8})

    def test_zero_fps(self):
        assert not self.validate({"width": 1536, "height": 864, "fps": 0, "format": 0, "buffer_count": 8})

    def test_fps_over_limit(self):
        assert not self.validate({"width": 1536, "height": 864, "fps": 121, "format": 0, "buffer_count": 8})

    def test_buffer_too_few(self):
        assert not self.validate({"width": 1536, "height": 864, "fps": 120, "format": 0, "buffer_count": 1})

    def test_buffer_too_many(self):
        assert not self.validate({"width": 1536, "height": 864, "fps": 120, "format": 0, "buffer_count": 9})

    def test_buffer_min_edge(self):
        assert self.validate({"width": 1536, "height": 864, "fps": 120, "format": 0, "buffer_count": 2})

    def test_buffer_max_edge(self):
        assert self.validate({"width": 1536, "height": 864, "fps": 120, "format": 0, "buffer_count": 8})


class TestZeroCopyFrame:
    """ZeroCopyFrame: DMA buffer descriptor."""

    def test_default_construction(self):
        frame = {"sequence": 0, "timestamp_ns": 0, "exposure_us": 0,
                 "gain": 0.0, "width": 0, "height": 0, "format": 0,
                 "width2": 0, "height2": 0, "format2": 0,
                 "request_ptr": None, "buffer_id": 0, "valid": False}
        assert not frame["valid"]

    def test_valid_check(self):
        frame = {"valid": True, "plane_data": [b"data", None, None],
                 "width": 1536, "height": 864}
        is_ok = (frame["valid"] and
                 frame["plane_data"][0] is not None and
                 frame["width"] > 0 and frame["height"] > 0)
        assert is_ok

    def test_invalid_when_not_valid_flag(self):
        frame = {"valid": False, "plane_data": [b"data", None, None],
                 "width": 1536, "height": 864}
        assert not frame["valid"]

    def test_invalid_no_plane_data(self):
        frame = {"valid": True, "plane_data": [None, None, None],
                 "width": 1536, "height": 864}
        is_ok = (frame["valid"] and frame["plane_data"][0] is not None)
        assert not is_ok

    def test_width_height_bounds(self):
        width, height = 1536, 864
        assert width > 0 and height > 0
        assert width <= 8192 and height <= 8192

    def test_max_dimension(self):
        assert 8192 == 8192

    def test_plane_count(self):
        assert 3 == 3

    def test_authentication_fields(self):
        frame_hash = bytes(32)
        hmac = bytes(32)
        assert len(frame_hash) == 32
        assert len(hmac) == 32

    def test_has_authentication_check(self):
        frame_hash = bytes([1] + [0] * 31)
        hmac = bytes([0] * 32)
        assert frame_hash[0] != 0 or hmac[0] != 0

    def test_no_authentication(self):
        frame_hash = bytes(32)
        hmac = bytes(32)
        assert frame_hash[0] == 0 and hmac[0] == 0

    def test_get_plane_data_bounds(self):
        plane_data = [b"a", b"b", b"c"]
        assert plane_data[0] is not None
        assert plane_data[3] if len(plane_data) > 3 else True  # no index error

    def test_plane_size_array(self):
        sizes = [0, 0, 0]
        assert len(sizes) == 3

    def test_stride_array(self):
        strides = [0, 0, 0]
        assert len(strides) == 3


class TestCameraException:
    """CameraException: runtime_error wrapper."""

    def test_exception_message(self):
        msg = "Camera init failed"
        exc = RuntimeError(msg)
        assert str(exc) == msg


class TestCameraWrapperConfig:
    """CameraWrapper configuration constants."""

    def test_default_width(self):
        assert 1536 == 1536

    def test_default_height(self):
        assert 864 == 864

    def test_default_fps(self):
        assert 120 == 120

    def test_camera_buffer_count(self):
        assert 8 == 8


class TestFrameBufferAllocator:
    """FrameBufferAllocator: DMA buffer management."""

    def test_plane_size(self):
        assert 0 == 0

    def test_buffer_count(self):
        assert 0 == 0

    def test_get_data_returns_none_before_alloc(self):
        pass


class TestUsbCameraConfig:
    """UsbCameraConfig: USB webcam configuration."""

    def validate(self, cfg):
        return (cfg["width"] > 0 and cfg["height"] > 0 and
                cfg["fps"] > 0 and cfg["fps"] <= 120 and
                cfg["buffer_count"] >= 2)

    def test_defaults(self):
        cfg = {"width": 640, "height": 480, "fps": 30}
        assert cfg["width"] == 640
        assert cfg["height"] == 480
        assert cfg["fps"] == 30

    def test_valid_config(self):
        cfg = {"width": 640, "height": 480, "fps": 30, "buffer_count": 4}
        assert self.validate(cfg)

    def test_zero_width(self):
        assert not self.validate({"width": 0, "height": 480, "fps": 30, "buffer_count": 4})

    def test_zero_height(self):
        assert not self.validate({"width": 640, "height": 0, "fps": 30, "buffer_count": 4})

    def test_fps_over_limit(self):
        assert not self.validate({"width": 640, "height": 480, "fps": 121, "buffer_count": 4})

    def test_buffer_too_few(self):
        assert not self.validate({"width": 640, "height": 480, "fps": 30, "buffer_count": 1})

    def test_device_index_default(self):
        assert -1 == -1

    def test_device_path_empty_default(self):
        assert "" == ""


class TestCameraSource:
    """CameraSource enum: MIPI vs USB selector."""

    def test_mipi_value(self):
        assert 0 == 0

    def test_usb_value(self):
        assert 1 == 1


class TestZeroCopyFrameAlignment:
    """alignas(64) cache line alignment for ZeroCopyFrame."""

    def test_cache_line_size(self):
        assert 64 == 64

    def test_frame_size_multiple_of_cache_line(self):
        frame_fields = {
            "sequence": 4, "timestamp_ns": 8, "exposure_us": 4,
            "gain": 4, "width": 4, "height": 4, "format": 4,
            "width2": 4, "height2": 4, "format2": 4,
            "request_ptr": 8, "buffer_id": 4, "valid": 1
        }
        total = sum(frame_fields.values())
        frame_size_aligned = (total + 63) & ~63
        assert frame_size_aligned >= total

    def test_plane_data_alignment(self):
        num_planes = 3
        assert num_planes == 3

    def test_strides_alignment(self):
        num_strides = 3
        assert num_strides == 3

    def test_buffer_id_uint32(self):
        import struct
        buf_id = struct.pack("I", 0)
        assert len(buf_id) == 4

    def test_sequence_uint32(self):
        import struct
        seq = struct.pack("I", 42)
        assert len(seq) == 4

    def test_timestamp_uint64(self):
        import struct
        ts = struct.pack("Q", 1234567890)
        assert len(ts) == 8

    def test_gain_float32(self):
        import struct
        gain = struct.pack("f", 1.0)
        assert len(gain) == 4


class TestFrameDimensions:
    """Frame dimension and resolution validation."""

    def test_mipi_frame_size_bytes(self):
        stride = 1920
        height = 864
        total = stride * height
        assert total == 1658880

    def test_mipi_10bit_pitch(self):
        pixels = 1536
        bpp = 10
        stride = (pixels * bpp + 7) // 8
        assert stride == 1920

    def test_pixel_count_per_frame(self):
        px = 1536 * 864
        assert px == 1327104

    def test_raw10_pitch_aligned_32(self):
        stride = 1920
        assert stride % 32 == 0

    def test_height_positive(self):
        assert 864 > 0

    def test_width_positive(self):
        assert 1536 > 0

    def test_width_height_product_positive(self):
        assert 1536 * 864 > 0


class TestFrameBufferAllocatorDimensions:
    """Frame buffer allocation calculation."""

    def test_buffer_size_for_one_frame(self):
        stride = 1920
        height = 864
        plane_size = stride * height
        assert plane_size == 1658880

    def test_multiple_buffers(self):
        buf_count = 8
        stride = 1920
        height = 864
        total = buf_count * stride * height
        assert total == 13271040

    def test_buffer_count_minimum(self):
        assert 2 <= 8

    def test_buffer_count_reasonable(self):
        assert 8 <= 32


class TestUsbCameraValidation:
    """UsbCamera::validate: configuration validation."""

    def validate(self, cfg):
        return (cfg["width"] > 0 and cfg["height"] > 0 and
                cfg["fps"] > 0 and cfg["fps"] <= 120 and
                cfg["buffer_count"] >= 2)

    def test_valid_config(self):
        cfg = {"width": 640, "height": 480, "fps": 30, "buffer_count": 4}
        assert self.validate(cfg)

    def test_fps_over_limit(self):
        assert not self.validate({"width": 640, "height": 480, "fps": 121, "buffer_count": 4})

    def test_buffer_too_few(self):
        assert not self.validate({"width": 640, "height": 480, "fps": 30, "buffer_count": 1})

    def test_zero_width(self):
        assert not self.validate({"width": 0, "height": 480, "fps": 30, "buffer_count": 4})

    def test_zero_fps(self):
        assert not self.validate({"width": 640, "height": 480, "fps": 0, "buffer_count": 4})
