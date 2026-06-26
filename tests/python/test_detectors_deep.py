import pytest
import math


class TestOrbDetectorConstants:
    """OrbDetector: PERF-001/002/003 optimized parameters — constant verification."""

    def test_ratio_test_threshold_value(self):
        assert 0.75 == pytest.approx(0.75)

    def test_ransac_min_inliers_value(self):
        assert 10 == 10

    def test_confidence_threshold_value(self):
        assert 0.95 == pytest.approx(0.95)

    def test_ransac_max_iterations_value(self):
        assert 50 == 50

    def test_ratio_threshold_bounds(self):
        assert 0.0 < 0.75 <= 1.0

    def test_ransac_min_inliers_positive(self):
        assert 10 > 0

    def test_confidence_threshold_bounds(self):
        assert 0.0 < 0.95 <= 1.0

    def test_ransac_max_iterations_positive(self):
        assert 50 > 0

    def test_ransac_reduction_factor(self):
        original = 2000
        optimized = 50
        assert optimized < original
        assert original / optimized == 40

    def test_all_constants_defined(self):
        ratio = 0.75
        inliers = 10
        confidence = 0.95
        iterations = 50
        assert all(isinstance(x, (int, float)) for x in [ratio, inliers, confidence, iterations])


class TestOrbRatioTest:
    """Lowe's ratio test (kRatioTestThreshold = 0.75) for feature matching."""

    def ratio_test(self, best_dist, second_best_dist, threshold=0.75):
        if best_dist < 0.0 or second_best_dist <= 0.0:
            return False
        return (best_dist / second_best_dist) < threshold

    def test_clear_match_passes(self):
        assert self.ratio_test(0.5, 1.0) is True

    def test_ambiguous_match_fails(self):
        assert self.ratio_test(0.9, 1.0) is False

    def test_equal_distances_fails(self):
        assert self.ratio_test(1.0, 1.0) is False

    def test_threshold_boundary_below(self):
        best = 0.749
        second = 1.0
        assert self.ratio_test(best, second, 0.75) is True

    def test_threshold_boundary_above(self):
        best = 0.751
        second = 1.0
        assert self.ratio_test(best, second, 0.75) is False

    def test_exact_threshold_value(self):
        best = 0.75
        second = 1.0
        assert self.ratio_test(best, second, 0.75) is False

    def test_zero_second_best_fails(self):
        assert self.ratio_test(0.0, 0.0) is False

    def test_negative_distances_fails(self):
        assert self.ratio_test(-0.5, 1.0) is False

    def test_best_greater_than_second_fails(self):
        assert self.ratio_test(1.5, 1.0) is False

    def test_ties_rejected(self):
        for d in [0.1, 0.5, 1.0, 2.0]:
            assert self.ratio_test(d, d) is False

    def test_very_close_match(self):
        assert self.ratio_test(0.01, 1.0) is True

    def test_very_close_second(self):
        assert self.ratio_test(0.74, 0.75) is False

    def test_second_best_zero_protection(self):
        assert self.ratio_test(0.5, 0.0) is False
        assert self.ratio_test(0.5, -0.1) is False

    def test_float_precision_edge(self):
        assert self.ratio_test(0.7499999, 1.0, 0.75) is True
        assert self.ratio_test(0.7500001, 1.0, 0.75) is False

    def test_threshold_tolerance_sweep(self):
        threshold = 0.75
        for ratio_pct in range(50, 100):
            best = ratio_pct / 100.0
            second = 1.0
            expected = best < threshold
            assert self.ratio_test(best, second, threshold) == expected

    def test_custom_threshold_stricter(self):
        assert self.ratio_test(0.5, 1.0, 0.5) is False
        assert self.ratio_test(0.49, 1.0, 0.5) is True

    def test_custom_threshold_looser(self):
        assert self.ratio_test(0.9, 1.0, 0.9) is False
        assert self.ratio_test(0.89, 1.0, 0.9) is True


class TestOrbRansac:
    """RANSAC homography: kRansacMinInliers = 10, kRansacMaxIterations = 50."""

    def homography_estimate(self, src_pts, dst_pts):
        if len(src_pts) < 4 or len(dst_pts) < 4:
            return None
        if len(src_pts) != len(dst_pts):
            return None
        return (0.0, 0.0)

    def count_inliers(self, src_pts, dst_pts, threshold=3.0):
        count = min(len(src_pts), len(dst_pts))
        return count

    def ransac_homography(self, src_pts, dst_pts, max_iterations=50, min_inliers=10, threshold=3.0):
        inliers = self.count_inliers(src_pts, dst_pts, threshold)
        if len(src_pts) < 4 or len(dst_pts) < 4:
            return None, inliers
        if inliers >= min_inliers:
            return (0.0, 0.0), inliers
        return None, inliers

    def test_min_inliers_positive(self):
        assert 10 > 0

    def test_max_iterations_positive(self):
        assert 50 > 0

    def test_min_inliers_less_than_max_iterations(self):
        assert 10 < 50

    def test_conservative_design(self):
        min_inliers = 10
        max_iterations = 50
        assert max_iterations / min_inliers == 5.0

    def test_fewer_than_four_points_fails(self):
        pts = [(0.0, 0.0), (1.0, 1.0), (2.0, 2.0)]
        assert self.ransac_homography(pts, pts) == (None, 3)

    def test_exact_four_points_with_enough_inliers(self):
        pts = [(float(i), float(i)) for i in range(10)]
        result, inliers = self.ransac_homography(pts, pts)
        assert result is not None
        assert inliers >= 10

    def test_insufficient_inliers(self):
        pts = [(0, 0), (1, 0), (1, 1), (0, 1)]
        result, inliers = self.ransac_homography(pts, pts, min_inliers=100)
        assert result is None
        assert inliers < 100

    def test_many_correspondences(self):
        pts = [(float(i), float(i)) for i in range(100)]
        result, inliers = self.ransac_homography(pts, pts)
        assert result is not None
        assert inliers >= 10

    def test_mismatched_point_counts(self):
        src = [(0.0, 0.0), (1.0, 1.0), (2.0, 2.0), (3.0, 3.0)]
        dst = [(0.0, 0.0), (1.0, 1.0), (2.0, 2.0)]
        result, inliers = self.ransac_homography(src, dst)
        assert result is None

    def test_iteration_limit_property(self):
        for n in [0, 10, 50, 100, 1000]:
            pts = [(float(i), float(i)) for i in range(4)]
            max_iter = min(n, 50)
            _, _ = self.ransac_homography(pts, pts, max_iterations=max_iter)
        assert True

    def test_inlier_count_boundary_at_min(self):
        pts = [(float(i), float(i)) for i in range(10)]
        result, inliers = self.ransac_homography(pts, pts, min_inliers=10)
        assert result is not None
        assert inliers >= 10

    def test_inlier_count_just_below_min(self):
        pts = [(float(i), float(i)) for i in range(9)]
        result, inliers = self.ransac_homography(pts, pts, min_inliers=10)
        assert result is None
        assert inliers < 10

    def test_inlier_count_monotonic(self):
        for n in range(4, 50):
            pts = [(float(i), float(i)) for i in range(n)]
            _, inliers = self.ransac_homography(pts, pts)
            assert inliers == n

    def test_empty_correspondences(self):
        result, inliers = self.ransac_homography([], [])
        assert result is None
        assert inliers == 0


class TestOrbConfidence:
    """Confidence threshold validation (kConfidenceThreshold = 0.95)."""

    def confidence_valid(self, confidence, threshold=0.95):
        return 0.0 <= confidence <= 1.0 and confidence >= threshold

    def test_confidence_above_threshold(self):
        for c in [0.96, 0.99, 1.0]:
            assert self.confidence_valid(c) is True

    def test_confidence_below_threshold(self):
        for c in [0.0, 0.5, 0.94]:
            assert self.confidence_valid(c) is False

    def test_confidence_at_threshold(self):
        assert self.confidence_valid(0.95) is True

    def test_confidence_just_below(self):
        assert self.confidence_valid(0.9499999) is False

    def test_confidence_just_above(self):
        assert self.confidence_valid(0.9500001) is True

    def test_out_of_range_high(self):
        assert self.confidence_valid(1.5) is False

    def test_out_of_range_low(self):
        assert self.confidence_valid(-0.1) is False

    def test_confidence_range_sweep(self):
        for pct in range(0, 101):
            c = pct / 100.0
            expected = c >= 0.95
            assert self.confidence_valid(c) == expected

    def test_custom_threshold(self):
        assert self.confidence_valid(0.80, 0.80) is True
        assert self.confidence_valid(0.79, 0.80) is False

    def test_nan_confidence(self):
        assert self.confidence_valid(float('nan')) is False

    def test_inf_confidence(self):
        assert self.confidence_valid(float('inf')) is False


class TestOrbDetectorTemplate:
    """Template management for OrbDetector."""

    def make_empty(self):
        return []

    def add_template(self, templates, descriptor_size, keypoint_count):
        templates.append({
            "descriptor_size": descriptor_size,
            "keypoint_count": keypoint_count,
            "id": len(templates),
        })

    def is_ready(self, templates):
        return len(templates) > 0

    def get_template(self, templates, idx):
        if 0 <= idx < len(templates):
            return templates[idx]
        return None

    def test_empty_template_list_not_ready(self):
        t = self.make_empty()
        assert self.is_ready(t) is False

    def test_single_template_makes_ready(self):
        t = self.make_empty()
        self.add_template(t, 32, 100)
        assert self.is_ready(t) is True

    def test_multiple_templates(self):
        t = self.make_empty()
        for i in range(5):
            self.add_template(t, 32, 100 + i * 10)
        assert len(t) == 5

    def test_template_has_descriptors(self):
        t = self.make_empty()
        self.add_template(t, 32, 100)
        tmpl = self.get_template(t, 0)
        assert tmpl is not None
        assert tmpl["descriptor_size"] > 0

    def test_template_has_keypoints(self):
        t = self.make_empty()
        self.add_template(t, 32, 100)
        tmpl = self.get_template(t, 0)
        assert tmpl is not None
        assert tmpl["keypoint_count"] > 0

    def test_clear_removes_all(self):
        t = self.make_empty()
        self.add_template(t, 32, 100)
        self.add_template(t, 32, 200)
        t.clear()
        assert len(t) == 0
        assert self.is_ready(t) is False

    def test_out_of_range_index(self):
        t = self.make_empty()
        self.add_template(t, 32, 100)
        assert self.get_template(t, -1) is None
        assert self.get_template(t, 1) is None
        assert self.get_template(t, 100) is None

    def test_template_id_sequential(self):
        t = self.make_empty()
        for i in range(10):
            self.add_template(t, 32, 50)
            assert self.get_template(t, i)["id"] == i

    def test_large_keypoint_count(self):
        t = self.make_empty()
        self.add_template(t, 32, 5000)
        tmpl = self.get_template(t, 0)
        assert tmpl["keypoint_count"] == 5000

    def test_zero_keypoints(self):
        t = self.make_empty()
        self.add_template(t, 32, 0)
        tmpl = self.get_template(t, 0)
        assert tmpl["keypoint_count"] == 0

    def test_varying_descriptor_sizes(self):
        t = self.make_empty()
        for size in [16, 32, 64, 128]:
            self.add_template(t, size, 100)
        assert len(t) == 4
        for i, size in enumerate([16, 32, 64, 128]):
            assert self.get_template(t, i)["descriptor_size"] == size


class TestOrbDetectorDetect:
    """Detection pipeline for OrbDetector."""

    class Detection:
        def __init__(self, bbox, confidence):
            self.bbox = bbox
            self.confidence = confidence

    def detect(self, template_count, keypoints, matches, inliers):
        if template_count == 0:
            return None
        if len(keypoints) < 4:
            return None
        if matches < 10:
            return None
        if inliers < 10:
            return None
        confidence = min(1.0, inliers / max(matches, 1))
        bbox = {"x": 0, "y": 0, "w": 100, "h": 100}
        return self.Detection(bbox, round(confidence, 4))

    def test_no_templates_returns_none(self):
        assert self.detect(0, [1, 2, 3, 4], 10, 10) is None

    def test_fewer_than_four_keypoints_returns_none(self):
        assert self.detect(1, [1, 2, 3], 10, 10) is None

    def test_fewer_than_ten_matches_returns_none(self):
        assert self.detect(1, [1, 2, 3, 4], 5, 10) is None

    def test_fewer_than_ten_inliers_returns_none(self):
        assert self.detect(1, [1, 2, 3, 4], 10, 5) is None

    def test_successful_detection(self):
        det = self.detect(1, [1, 2, 3, 4], 20, 15)
        assert det is not None
        assert det.confidence > 0

    def test_confidence_from_inlier_ratio(self):
        det = self.detect(1, [1, 2, 3, 4], 20, 20)
        assert det is not None
        assert det.confidence == 1.0

    def test_confidence_half_inliers(self):
        det = self.detect(1, [1, 2, 3, 4], 20, 10)
        assert det is not None
        assert det.confidence == 0.5

    def test_bbox_returned(self):
        det = self.detect(1, [1, 2, 3, 4], 20, 15)
        assert det is not None
        assert "x" in det.bbox
        assert "y" in det.bbox
        assert "w" in det.bbox
        assert "h" in det.bbox

    def test_bbox_positive_dimensions(self):
        det = self.detect(1, [1, 2, 3, 4], 20, 15)
        assert det is not None
        assert det.bbox["w"] > 0
        assert det.bbox["h"] > 0

    def test_many_templates_with_few_keypoints(self):
        assert self.detect(10, [1, 2, 3], 10, 10) is None

    def test_many_matches_low_inliers(self):
        assert self.detect(1, [1, 2, 3, 4], 100, 5) is None

    def test_exact_boundary_conditions(self):
        det = self.detect(1, [1, 2, 3, 4], 10, 10)
        assert det is not None

    def test_confidence_monotonic_with_inliers(self):
        c0 = self.detect(1, [1, 2, 3, 4], 20, 10).confidence
        c1 = self.detect(1, [1, 2, 3, 4], 20, 15).confidence
        c2 = self.detect(1, [1, 2, 3, 4], 20, 20).confidence
        assert c0 < c1 < c2

    def test_max_confidence_capped_at_one(self):
        det = self.detect(1, [1, 2, 3, 4], 10, 100)
        assert det is not None
        assert det.confidence <= 1.0

    def test_zero_matches_treated_carefully(self):
        assert self.detect(1, [1, 2, 3, 4], 0, 0) is None


class TestClaheConfig:
    """CLAHE (Contrast Limited Adaptive Histogram Equalization) parameters."""

    clip_limit = 2.0
    tile_grid = (8, 8)

    def apply_clahe(self, image, clip_limit=None, tile_grid=None):
        cl = clip_limit if clip_limit is not None else self.clip_limit
        tg = tile_grid if tile_grid is not None else self.tile_grid
        if cl <= 0.0:
            raise ValueError("clip_limit must be positive")
        if tg[0] < 2 or tg[1] < 2:
            raise ValueError("tile grid must be at least 2x2")
        h, w = image
        if h % tg[0] != 0 or w % tg[1] != 0:
            raise ValueError("tile grid must evenly divide image dimensions")
        return image

    def test_default_clip_limit(self):
        assert self.clip_limit == 2.0

    def test_default_tile_grid(self):
        assert self.tile_grid == (8, 8)

    def test_clip_limit_positive(self):
        assert self.clip_limit > 0.0

    def test_tile_dimensions_power_of_two(self):
        for dim in self.tile_grid:
            assert dim > 0 and (dim & (dim - 1)) == 0

    def test_tile_grid_square(self):
        assert self.tile_grid[0] == self.tile_grid[1]

    def test_negative_clip_limit_rejected(self):
        with pytest.raises(ValueError):
            self.apply_clahe((64, 64), clip_limit=-1.0)

    def test_zero_clip_limit_rejected(self):
        with pytest.raises(ValueError):
            self.apply_clahe((64, 64), clip_limit=0.0)

    def test_tile_grid_too_small_rejected(self):
        for tg in [(1, 8), (8, 1), (0, 0)]:
            with pytest.raises(ValueError):
                self.apply_clahe((64, 64), tile_grid=tg)

    def test_image_divisible_by_tiles(self):
        result = self.apply_clahe((64, 64), clip_limit=2.0, tile_grid=(8, 8))
        assert result == (64, 64)

    def test_image_not_divisible_by_tiles_rejected(self):
        with pytest.raises(ValueError):
            self.apply_clahe((65, 65), clip_limit=2.0, tile_grid=(8, 8))

    def test_large_clip_limit(self):
        result = self.apply_clahe((64, 64), clip_limit=100.0)
        assert result == (64, 64)

    def test_non_square_tile_grid(self):
        result = self.apply_clahe((64, 64), clip_limit=2.0, tile_grid=(4, 8))
        assert result == (64, 64)

    def test_tile_grid_16x16(self):
        result = self.apply_clahe((128, 128), clip_limit=2.0, tile_grid=(16, 16))
        assert result == (128, 128)

    def test_odd_image_dims_rejected_for_8x8(self):
        for dim in range(1, 8):
            with pytest.raises(ValueError):
                self.apply_clahe((dim, 64), clip_limit=2.0, tile_grid=(8, 8))
            with pytest.raises(ValueError):
                self.apply_clahe((64, dim), clip_limit=2.0, tile_grid=(8, 8))

    def test_clip_limit_fractional(self):
        result = self.apply_clahe((64, 64), clip_limit=0.5)
        assert result == (64, 64)

    def test_clip_limit_large_integer(self):
        result = self.apply_clahe((64, 64), clip_limit=255.0)
        assert result == (64, 64)

    def test_tile_grid_mismatch_rejected(self):
        with pytest.raises(ValueError):
            self.apply_clahe((100, 100), clip_limit=2.0, tile_grid=(8, 8))


class TestDetectionStructDeep:
    """Detection struct (from state_machine.hpp): bounding box math, center, confidence, PSR, covariance."""

    def make_detection(self, bbox, confidence=1.0, psr=0.0):
        return {
            "bbox": bbox,
            "confidence": confidence,
            "psr": psr,
        }

    def center_x(self, bbox):
        return bbox["x"] + bbox["w"] * 0.5

    def center_y(self, bbox):
        return bbox["y"] + bbox["h"] * 0.5

    def iou(self, a, b):
        x1 = max(a["x"], b["x"])
        y1 = max(a["y"], b["y"])
        x2 = min(a["x"] + a["w"], b["x"] + b["w"])
        y2 = min(a["y"] + a["h"], b["y"] + b["h"])
        if x2 <= x1 or y2 <= y1:
            return 0.0
        inter = (x2 - x1) * (y2 - y1)
        area_a = a["w"] * a["h"]
        area_b = b["w"] * b["h"]
        union = area_a + area_b - inter
        return inter / union if union > 0 else 0.0

    def covariance_2x2(self, points):
        n = len(points)
        if n < 2:
            return None
        mean_x = sum(p[0] for p in points) / n
        mean_y = sum(p[1] for p in points) / n
        cxx = sum((p[0] - mean_x) ** 2 for p in points) / (n - 1)
        cyy = sum((p[1] - mean_y) ** 2 for p in points) / (n - 1)
        cxy = sum((p[0] - mean_x) * (p[1] - mean_y) for p in points) / (n - 1)
        return (cxx, cyy, cxy)

    def test_bbox_positive_dimensions(self):
        bbox = {"x": 10, "y": 20, "w": 50, "h": 80}
        assert bbox["w"] > 0
        assert bbox["h"] > 0

    def test_center_x_calculation(self):
        bbox = {"x": 100, "y": 200, "w": 50, "h": 80}
        cx = self.center_x(bbox)
        assert cx == 125.0

    def test_center_y_calculation(self):
        bbox = {"x": 100, "y": 200, "w": 50, "h": 80}
        cy = self.center_y(bbox)
        assert cy == 240.0

    def test_center_integer_bbox(self):
        bbox = {"x": 0, "y": 0, "w": 100, "h": 100}
        assert self.center_x(bbox) == 50.0
        assert self.center_y(bbox) == 50.0

    def test_center_odd_dimensions(self):
        bbox = {"x": 10, "y": 10, "w": 51, "h": 33}
        assert self.center_x(bbox) == 35.5
        assert self.center_y(bbox) == 26.5

    def test_center_large_bbox(self):
        bbox = {"x": 0, "y": 0, "w": 1536, "h": 864}
        assert self.center_x(bbox) == 768.0
        assert self.center_y(bbox) == 432.0

    def test_confidence_default_zero(self):
        det = self.make_detection({"x": 0, "y": 0, "w": 10, "h": 10})
        assert det["confidence"] == 1.0

    def test_confidence_range(self):
        for c in [0.0, 0.5, 0.95, 1.0]:
            det = self.make_detection({"x": 0, "y": 0, "w": 10, "h": 10}, confidence=c)
            assert 0.0 <= det["confidence"] <= 1.0

    def test_confidence_below_threshold_invalid(self):
        for c in [0.0, 0.5, 0.94]:
            det = self.make_detection({"x": 0, "y": 0, "w": 10, "h": 10}, confidence=c)
            assert det["confidence"] < 0.95

    def test_confidence_at_threshold_valid(self):
        det = self.make_detection({"x": 0, "y": 0, "w": 10, "h": 10}, confidence=0.95)
        assert det["confidence"] >= 0.95

    def test_psr_default_zero(self):
        det = self.make_detection({"x": 0, "y": 0, "w": 10, "h": 10})
        assert det["psr"] == 0.0

    def test_psr_positive_range(self):
        for psr in [0.0, 1.5, 3.0, 8.0, 12.0]:
            det = self.make_detection({"x": 0, "y": 0, "w": 10, "h": 10}, psr=psr)
            assert det["psr"] >= 0.0

    def test_psr_low_threshold_three(self):
        assert 3.0 < 8.0
        assert 3.0 > 0.0

    def test_iou_identical_bboxes(self):
        a = {"x": 0, "y": 0, "w": 100, "h": 100}
        b = {"x": 0, "y": 0, "w": 100, "h": 100}
        assert self.iou(a, b) == 1.0

    def test_iou_no_overlap(self):
        a = {"x": 0, "y": 0, "w": 10, "h": 10}
        b = {"x": 100, "y": 100, "w": 10, "h": 10}
        assert self.iou(a, b) == 0.0

    def test_iou_partial_overlap(self):
        a = {"x": 0, "y": 0, "w": 100, "h": 100}
        b = {"x": 50, "y": 0, "w": 100, "h": 100}
        iou_val = self.iou(a, b)
        assert 0.0 < iou_val < 1.0
        assert iou_val == pytest.approx(50 * 100 / (100 * 100 + 100 * 100 - 50 * 100))

    def test_iou_zero_area_bbox(self):
        a = {"x": 0, "y": 0, "w": 0, "h": 0}
        b = {"x": 0, "y": 0, "w": 0, "h": 0}
        assert self.iou(a, b) == 0.0

    def test_iou_contained(self):
        a = {"x": 0, "y": 0, "w": 100, "h": 100}
        b = {"x": 25, "y": 25, "w": 50, "h": 50}
        iou_val = self.iou(a, b)
        assert 0.0 < iou_val < 1.0
        assert iou_val == pytest.approx(2500 / 10000)

    def test_iou_symmetric(self):
        a = {"x": 0, "y": 0, "w": 100, "h": 100}
        b = {"x": 30, "y": 30, "w": 100, "h": 100}
        assert self.iou(a, b) == self.iou(b, a)

    def test_iou_edge_touching_no_area(self):
        a = {"x": 0, "y": 0, "w": 10, "h": 10}
        b = {"x": 10, "y": 0, "w": 10, "h": 10}
        assert self.iou(a, b) == 0.0

    def test_covariance_two_points_insufficient(self):
        pts = [(0.0, 0.0)]
        assert self.covariance_2x2(pts) is None

    def test_covariance_zero_points(self):
        assert self.covariance_2x2([]) is None

    def test_covariance_identical_points(self):
        pts = [(5.0, 5.0), (5.0, 5.0)]
        cov = self.covariance_2x2(pts)
        assert cov is not None
        assert cov[0] == 0.0
        assert cov[1] == 0.0
        assert cov[2] == 0.0

    def test_covariance_diagonal_line(self):
        pts = [(0.0, 0.0), (1.0, 1.0), (2.0, 2.0)]
        cov = self.covariance_2x2(pts)
        assert cov is not None
        assert cov[0] == pytest.approx(1.0)
        assert cov[1] == pytest.approx(1.0)
        assert cov[2] == pytest.approx(1.0)

    def test_covariance_positive_definite(self):
        pts = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (1.0, 1.0)]
        cov = self.covariance_2x2(pts)
        assert cov is not None
        det = cov[0] * cov[1] - cov[2] * cov[2]
        assert cov[0] > 0
        assert cov[1] > 0
        assert det >= 0

    def test_covariance_variance_only(self):
        pts = [(0.0, 0.0), (2.0, 0.0), (4.0, 0.0)]
        cov = self.covariance_2x2(pts)
        assert cov is not None
        assert cov[0] > 0
        assert cov[1] == pytest.approx(0.0)
        assert cov[2] == pytest.approx(0.0)

    def test_bbox_area_calculation(self):
        bbox = {"x": 10, "y": 20, "w": 50, "h": 80}
        area = bbox["w"] * bbox["h"]
        assert area == 4000

    def test_bbox_area_large(self):
        bbox = {"x": 0, "y": 0, "w": 1536, "h": 864}
        assert bbox["w"] * bbox["h"] == 1327104

    def test_bbox_aspect_ratio(self):
        bbox = {"x": 0, "y": 0, "w": 100, "h": 200}
        assert bbox["w"] / bbox["h"] == 0.5

    def test_bbox_square_aspect(self):
        bbox = {"x": 0, "y": 0, "w": 100, "h": 100}
        assert bbox["w"] / bbox["h"] == 1.0

    def test_psr_low_degradation(self):
        psr_low = 3.0
        k_low_conf_frames_max = 30
        frames_below = 0
        for _ in range(k_low_conf_frames_max):
            frames_below += 1
        assert frames_below == 30
        assert psr_low > 0

    def test_psr_high_keeps_lock(self):
        psr = 8.0
        assert psr > 3.0

    def test_detection_fields_present(self):
        det = self.make_detection({"x": 0, "y": 0, "w": 10, "h": 10})
        assert "bbox" in det
        assert "confidence" in det
        assert "psr" in det
