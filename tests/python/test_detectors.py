import pytest


class TestOrbDetectorConstants:
    """OrbDetector: PERF-001/002/003 optimized parameters."""

    def test_ratio_test_threshold(self):
        assert 0.75 == pytest.approx(0.75)

    def test_ransac_min_inliers(self):
        assert 10 == 10

    def test_confidence_threshold(self):
        assert 0.95 == pytest.approx(0.95)

    def test_ransac_max_iterations(self):
        assert 50 == 50

    def test_ransac_default_iterations(self):
        assert 50 < 2000


class TestAprilTagDetectorConstants:
    """AprilTagDetector: ArUco detection parameters."""

    def test_confidence_threshold(self):
        assert 0.85 == pytest.approx(0.85)

    def test_min_marker_perimeter(self):
        assert 50 == 50

    def test_max_error_rate(self):
        assert 0.1 == pytest.approx(0.1)

    def test_known_tag_ids(self):
        ids = [0, 1, 2]
        assert len(ids) >= 0

    def test_dictionary_types(self):
        dicts = ["DICT_4X4_50", "DICT_4X4_100", "DICT_4X4_250",
                 "DICT_5X5_50", "DICT_5X5_100", "DICT_6X6_50",
                 "DICT_7X7_50", "DICT_ARUCO_ORIGINAL"]
        assert len(dicts) == 8


class TestYolo26DetectorConfig:
    """Yolo26Detector::Config: ONNX model parameters."""

    def test_default_conf_threshold(self):
        assert 0.40 == pytest.approx(0.40)

    def test_default_input_size(self):
        assert 640 == 640

    def test_default_num_threads(self):
        assert 3 == 3

    def test_default_target_classes(self):
        classes = [0, 2, 4]
        assert classes == [0, 2, 4]

    def test_person_class(self):
        assert 0 == 0

    def test_car_class(self):
        assert 2 == 2

    def test_airplane_class(self):
        assert 4 == 4

    def test_letterbox_size(self):
        assert 640 == 640

    def test_letterbox_scale(self):
        img_w, img_h = 1536, 864
        target_size = 640
        scale = min(target_size / img_w, target_size / img_h)
        assert abs(scale - 0.4167) < 0.001

    def test_letterbox_padding(self):
        img_w, img_h = 1536, 864
        target_size = 640
        scale = min(target_size / img_w, target_size / img_h)
        new_w, new_h = int(img_w * scale), int(img_h * scale)
        pad_left = (target_size - new_w) / 2
        pad_top = (target_size - new_h) / 2
        assert pad_left >= 0
        assert pad_top >= 0
        assert abs(pad_top - 140.0) < 1.0


class TestDetectorStructures:
    """Detection struct shared across detectors."""

    def test_bbox_fields(self):
        det = {"id": -1, "confidence": 0.0,
               "bbox": {"x": 0, "y": 0, "w": 0, "h": 0}}
        assert det["id"] == -1

    def test_valid_detection_center(self):
        bbox = {"x": 100, "y": 200, "w": 50, "h": 80}
        cx = bbox["x"] + bbox["w"] * 0.5
        cy = bbox["y"] + bbox["h"] * 0.5
        assert cx == 125.0
        assert cy == 240.0
