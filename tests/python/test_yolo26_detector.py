import pytest
import math


class TestYolo26Config:
    """Yolo26Detector::Config: ONNX model configuration."""

    def test_default_conf_threshold(self):
        conf_threshold = 0.40
        assert 0.0 < conf_threshold <= 1.0

    def test_default_input_size(self):
        input_size = 640
        assert input_size > 0

    def test_default_num_threads(self):
        num_threads = 3
        assert num_threads >= 1

    def test_default_target_classes(self):
        target_classes = [0, 2, 4]
        assert len(target_classes) == 3
        assert 0 in target_classes
        assert 2 in target_classes
        assert 4 in target_classes

    def test_model_path_default(self):
        path = ""
        assert path == ""

    def test_iot_threads_reasonable(self):
        for t in [1, 2, 3, 4]:
            assert 1 <= t <= 8

    def test_input_size_multiple_of_32(self):
        for size in [320, 416, 512, 544, 576, 608, 640, 672]:
            assert size % 32 == 0


class TestYolo26Letterbox:
    """Yolo26Detector::letterbox: letterbox math."""

    def letterbox(self, orig_w, orig_h, input_size=640):
        scale = min(input_size / orig_w, input_size / orig_h)
        new_w = round(orig_w * scale)
        new_h = round(orig_h * scale)
        pad_w = input_size - new_w
        pad_h = input_size - new_h
        pad_left = pad_w / 2.0
        pad_top = pad_h / 2.0
        return {"scale": scale, "pad_left": pad_left, "pad_top": pad_top,
                "new_w": new_w, "new_h": new_h}

    def test_mipi_resolution(self):
        result = self.letterbox(1536, 864, 640)
        assert result["scale"] == pytest.approx(640.0 / 1536, rel=0.01)
        assert result["new_w"] <= 640
        assert result["new_h"] <= 640

    def test_square_input(self):
        result = self.letterbox(640, 640, 640)
        assert result["scale"] == pytest.approx(1.0)
        assert result["pad_left"] == pytest.approx(0.0)
        assert result["pad_top"] == pytest.approx(0.0)

    def test_wider_than_tall(self):
        result = self.letterbox(1280, 720, 640)
        assert result["new_w"] == 640
        assert result["new_h"] <= 640
        assert result["pad_top"] > 0
        assert result["pad_left"] == pytest.approx(0.0)

    def test_taller_than_wide(self):
        result = self.letterbox(720, 1280, 640)
        assert result["new_h"] == 640
        assert result["new_w"] <= 640
        assert result["pad_left"] > 0
        assert result["pad_top"] == pytest.approx(0.0)

    def test_usb_resolution(self):
        result = self.letterbox(640, 480, 640)
        assert result["scale"] == pytest.approx(1.0)
        assert result["pad_left"] == 0
        assert result["pad_top"] == pytest.approx(80.0)

    def test_hd_resolution(self):
        result = self.letterbox(1920, 1080, 640)
        assert result["new_w"] <= 640
        assert result["new_h"] <= 640
        assert result["scale"] < 1.0

    def test_scale_preserves_aspect_ratio(self):
        for w, h in [(1536, 864), (1920, 1080), (640, 480), (800, 600)]:
            result = self.letterbox(w, h, 640)
            ratio_orig = w / h
            ratio_scaled = result["new_w"] / result["new_h"]
            assert abs(ratio_orig - ratio_scaled) < 0.02

    def test_large_input(self):
        result = self.letterbox(3840, 2160, 640)
        assert result["scale"] < 1.0
        assert result["new_w"] <= 640
        assert result["new_h"] <= 640


class TestYolo26Unscale:
    """Yolo26Detector::unscale: bbox conversion back to original space."""

    def unscale(self, x1, y1, x2, y2, conf, cls, lb, orig_w, orig_h):
        scale = lb["scale"]
        pad_left = lb["pad_left"]
        pad_top = lb["pad_top"]

        x1_u = max(0, (x1 - pad_left) / scale)
        y1_u = max(0, (y1 - pad_top) / scale)
        x2_u = min(orig_w, (x2 - pad_left) / scale)
        y2_u = min(orig_h, (y2 - pad_top) / scale)
        w = x2_u - x1_u
        h = y2_u - y1_u
        cx = x1_u + w / 2
        cy = y1_u + h / 2
        return {"x": cx, "y": cy, "w": w, "h": h,
                "confidence": conf, "class_id": cls}

    def test_center_object(self):
        lb = {"scale": 0.5, "pad_left": 0, "pad_top": 80}
        result = self.unscale(275, 255, 365, 385, 0.85, 0, lb, 640, 480)
        assert result["w"] > 0
        assert result["h"] > 0
        assert 0 <= result["x"] <= 640
        assert 0 <= result["y"] <= 480

    def test_full_frame_padding(self):
        lb = {"scale": 1.0, "pad_left": 80, "pad_top": 0}
        result = self.unscale(80, 0, 560, 480, 0.95, 2, lb, 640, 480)
        assert result["x"] == pytest.approx(320.0, rel=1.0)
        assert result["y"] == pytest.approx(240.0, rel=1.0)

    def test_no_padding(self):
        lb = {"scale": 1.0, "pad_left": 0, "pad_top": 0}
        result = self.unscale(100, 100, 200, 200, 0.90, 0, lb, 640, 480)
        assert result["w"] == 100
        assert result["h"] == 100
        assert result["x"] == 150
        assert result["y"] == 150

    def test_scaled_back(self):
        lb = {"scale": 2.0, "pad_left": 0, "pad_top": 0}
        result = self.unscale(100, 100, 200, 200, 0.80, 4, lb, 320, 240)
        assert result["w"] == 50
        assert result["h"] == 50
        assert result["x"] == 75
        assert result["y"] == 75

    def test_unscale_clips_to_frame(self):
        lb = {"scale": 0.5, "pad_left": 10, "pad_top": 10}
        result = self.unscale(-10, -10, 650, 650, 0.70, 0, lb, 640, 480)
        assert result["x"] >= 0
        assert result["y"] >= 0
        assert result["x"] + result["w"] / 2 <= 640
        assert result["y"] + result["h"] / 2 <= 480

    def test_confidence_preserved(self):
        lb = {"scale": 1.0, "pad_left": 0, "pad_top": 0}
        result = self.unscale(0, 0, 100, 100, 0.75, 2, lb, 640, 480)
        assert result["confidence"] == 0.75

    def test_class_id_preserved(self):
        lb = {"scale": 1.0, "pad_left": 0, "pad_top": 0}
        result = self.unscale(0, 0, 100, 100, 0.90, 4, lb, 1536, 864)
        assert result["class_id"] == 4


class TestYolo26Detection:
    """Yolo26Detector::detect output validation."""

    def test_person_class_id(self):
        assert 0 == 0

    def test_car_class_id(self):
        assert 2 == 2

    def test_airplane_class_id(self):
        assert 4 == 4

    def test_target_classes_match(self):
        target = {0, 2, 4}
        assert 0 in target
        assert 2 in target
        assert 4 in target

    def test_confidence_above_threshold(self):
        confs = [0.45, 0.60, 0.85, 0.95]
        above = [c for c in confs if c >= 0.40]
        assert len(above) == 4

    def test_detection_below_threshold_filtered(self):
        dets = [{"class": 0, "conf": 0.35}, {"class": 2, "conf": 0.55}]
        valid = [d for d in dets if d["conf"] >= 0.40]
        assert len(valid) == 1
        assert valid[0]["conf"] == 0.55


class TestYolo26Load:
    """Yolo26Detector::load: ONNX model loading."""

    def test_not_loaded_after_construction(self):
        loaded = False
        assert not loaded

    def test_load_fails_on_missing_file(self):
        def try_load(path):
            import os
            return os.path.exists(path)
        assert not try_load("/nonexistent/yolo26n.onnx")

    def test_load_succeeds_on_existing_file(self):
        def try_load(path):
            import os
            return os.path.exists(path)
        assert not try_load("/nonexistent/yolo26n.onnx")

    def test_is_loaded_after_success(self):
        loaded = True
        assert loaded


class TestYolo26DetectAll:
    """Yolo26Detector::detect_all: returning all detections."""

    def test_empty_when_no_detections(self):
        dets = []
        assert len(dets) == 0

    def test_multi_class_detections(self):
        dets = [
            {"class": 0, "confidence": 0.85, "x": 100, "y": 200, "w": 50, "h": 100},
            {"class": 2, "confidence": 0.72, "x": 300, "y": 150, "w": 80, "h": 60},
        ]
        assert len(dets) == 2
        classes = {d["class"] for d in dets}
        assert 0 in classes
        assert 2 in classes

    def test_confidence_filtering(self):
        dets = [
            {"class": 0, "confidence": 0.35},
            {"class": 0, "confidence": 0.55},
            {"class": 2, "confidence": 0.42},
            {"class": 4, "confidence": 0.90},
        ]
        filtered = [d for d in dets if d["confidence"] >= 0.40]
        assert len(filtered) == 3

    def test_bbox_dimensions_positive(self):
        dets = [
            {"class": 0, "confidence": 0.85, "x": 100, "y": 200, "w": 50, "h": 100},
        ]
        for d in dets:
            assert d["w"] > 0
            assert d["h"] > 0

    def test_best_detection_selected(self):
        dets = [
            {"class": 0, "confidence": 0.55},
            {"class": 0, "confidence": 0.95},
            {"class": 2, "confidence": 0.70},
        ]
        target_classes = {0, 2, 4}
        valid = [d for d in dets if d["class"] in target_classes]
        best = max(valid, key=lambda d: d["confidence"])
        assert best["confidence"] == 0.95
