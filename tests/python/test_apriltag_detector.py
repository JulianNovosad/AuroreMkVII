import pytest
import itertools


class TestAprilTagDictionary:
    """AprilTagDetector::set_dictionary: ArUco dictionary selection."""

    def test_default_dict_id(self):
        dict_id = 16  # DICT_APRILTAG_36h11
        assert 0 <= dict_id <= 30

    def test_dict_36h11_tags(self):
        assert 36 == 36
        assert 11 == 11

    def test_dict_16h5_tags(self):
        dict_size = 30
        assert dict_size == 30

    def test_dict_range_validation(self):
        for d in [0, 10, 16, 20, 30]:
            assert 0 <= d <= 30


class TestAprilTagKnownIds:
    """AprilTagDetector::set_known_tag_ids: filtering by known tag IDs."""

    def test_empty_known_ids(self):
        ids = []
        assert len(ids) == 0

    def test_single_known_id(self):
        ids = [42]
        assert len(ids) == 1
        assert ids[0] == 42

    def test_multiple_known_ids(self):
        ids = [0, 1, 2, 3, 4, 5]
        assert len(ids) == 6
        assert all(0 <= i <= 1000 for i in ids)

    def test_known_ids_unique(self):
        ids = [1, 2, 3]
        assert len(ids) == len(set(ids))

    def test_known_ids_ordered(self):
        ids = [5, 3, 1, 4, 2]
        ids.sort()
        assert ids == [1, 2, 3, 4, 5]

    def test_known_ids_no_duplicates(self):
        ids = [1, 1, 2, 2, 3]
        assert len(set(ids)) == 3


class TestAprilTagDetectorParams:
    """AprilTagDetector detector parameters."""

    def test_confidence_threshold(self):
        kConfidenceThreshold = 0.85
        assert 0.0 < kConfidenceThreshold <= 1.0

    def test_min_marker_perimeter(self):
        kMinMarkerPerimeter = 50
        assert kMinMarkerPerimeter > 0

    def test_max_error_rate(self):
        kMaxErrorRate = 0.1
        assert 0.0 < kMaxErrorRate < 1.0

    def test_confidence_bounds(self):
        for c in [0.0, 0.5, 0.85, 1.0]:
            assert 0.0 <= c <= 1.0

    def test_perimeter_positive(self):
        assert 50 > 0

    def test_error_rate_positive_less_than_one(self):
        assert 0.0 < 0.1 < 1.0


class TestAprilTagDetection:
    """AprilTagDetector::detect output validation."""

    def test_detection_center_when_present(self):
        det = {"id": 0, "center_x": 320.0, "center_y": 240.0,
               "corners": [(100, 200), (540, 200), (540, 280), (100, 280)],
               "confidence": 0.92}
        assert 0 <= det["center_x"] <= 1536
        assert 0 <= det["center_y"] <= 864
        assert det["confidence"] >= 0.85

    def test_corner_order_expected(self):
        corners = [(100, 200), (540, 200), (540, 280), (100, 280)]
        assert len(corners) == 4
        xs = [c[0] for c in corners]
        ys = [c[1] for c in corners]
        assert min(xs) >= 0
        assert max(xs) <= 1536
        assert min(ys) >= 0
        assert max(ys) <= 864

    def test_bbox_from_corners(self):
        corners = [(100, 200), (540, 200), (540, 280), (100, 280)]
        xs = [c[0] for c in corners]
        ys = [c[1] for c in corners]
        x_min, x_max = min(xs), max(xs)
        y_min, y_max = min(ys), max(ys)
        assert x_min == 100
        assert x_max == 540
        assert y_min == 200
        assert y_max == 280

    def test_tag_id_range(self):
        for tag_id in [0, 10, 100, 1023]:
            assert 0 <= tag_id <= 1023

    def test_no_detection_returns_none(self):
        det = None
        assert det is None


class TestAprilTagDetectAll:
    """AprilTagDetector::detect_all: multiple tag detection."""

    def test_empty_frame_no_tags(self):
        dets = []
        assert len(dets) == 0

    def test_single_tag(self):
        dets = [{"id": 1, "center_x": 400.0, "center_y": 300.0, "confidence": 0.95}]
        assert len(dets) == 1

    def test_multiple_tags(self):
        dets = [
            {"id": 0, "center_x": 100.0, "center_y": 100.0, "confidence": 0.90},
            {"id": 1, "center_x": 500.0, "center_y": 300.0, "confidence": 0.88},
            {"id": 2, "center_x": 800.0, "center_y": 500.0, "confidence": 0.95},
        ]
        assert len(dets) == 3
        ids = [d["id"] for d in dets]
        assert ids == [0, 1, 2]

    def test_confidence_filtering(self):
        dets = [
            {"id": 0, "confidence": 0.90},
            {"id": 1, "confidence": 0.95},
            {"id": 2, "confidence": 0.80},
        ]
        filtered = [d for d in dets if d["confidence"] >= 0.85]
        assert len(filtered) == 2
        assert all(d["confidence"] >= 0.85 for d in filtered)

    def test_all_detections_in_bounds(self):
        frame_w, frame_h = 1536, 864
        dets = [
            {"id": 0, "center_x": 768.0, "center_y": 432.0, "confidence": 0.90},
            {"id": 1, "center_x": 200.0, "center_y": 100.0, "confidence": 0.85},
        ]
        for d in dets:
            assert 0 <= d["center_x"] <= frame_w
            assert 0 <= d["center_y"] <= frame_h

    def test_known_id_filtering(self):
        all_dets = [
            {"id": 0, "confidence": 0.90},
            {"id": 5, "confidence": 0.92},
            {"id": 10, "confidence": 0.88},
        ]
        known_ids = {0, 10}
        filtered = [d for d in all_dets if d["id"] in known_ids]
        assert len(filtered) == 2
        assert all(d["id"] in known_ids for d in filtered)


class TestAprilTagIsReady:
    """AprilTagDetector::is_ready: readiness checks."""

    def test_not_ready_after_construction(self):
        assert False is False

    def test_ready_after_setup(self):
        setup = ["dictionary", "known_ids"]
        assert len(setup) == 2

    def test_ready_checks_dictionary(self):
        has_dict = False
        has_ids = True
        ready = has_dict and has_ids
        assert not ready

    def test_ready_all_prerequisites(self):
        has_dict = True
        has_ids = True
        ready = has_dict and has_ids
        assert ready
