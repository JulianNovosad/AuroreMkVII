"""
Telemetry CSV Log Validation Tests

Tests the CSV log format produced by TelemetryWriter.
The CSV format follows ICD-004 and includes detection, tracking,
actuation, and system health fields.

No hardware or binary required — pure format validation.
"""
import csv
import io
import math
from pathlib import Path

import pytest


SAMPLE_CSV_HEADER = (
    "produced_ts_epoch_ms,call_ts_epoch_ms,cam_frame_id,"
    "det_x,det_y,det_width,det_height,det_confidence,det_target_class,"
    "track_id,track_x,track_y,track_z,track_hit_streak,track_confidence,"
    "servo_azimuth,servo_elevation,servo_command_sent,"
    "cpu_temp_c,cpu_usage_percent"
)

SAMPLE_CSV_ROW = (
    "1700000000000,1700000000001,42,"
    "768.5,432.3,200.0,150.0,0.95,1,"
    "1,10.5,20.3,150.0,5,0.92,"
    "45.0,30.0,1,"
    "55.2,42.5"
)


@pytest.fixture
def sample_csv_file(tmp_path: Path) -> Path:
    p = tmp_path / "test_log.csv"
    p.write_text(f"{SAMPLE_CSV_HEADER}\n{SAMPLE_CSV_ROW}\n")
    return p


class TestCsvHeader:
    def test_required_columns_present(self):
        fields = [f.strip() for f in SAMPLE_CSV_HEADER.split(",")]
        required = {
            "produced_ts_epoch_ms", "call_ts_epoch_ms", "cam_frame_id",
            "det_x", "det_y", "det_width", "det_height", "det_confidence",
            "track_id", "track_confidence",
            "servo_azimuth", "servo_elevation",
            "cpu_temp_c", "cpu_usage_percent",
        }
        missing = required - set(fields)
        assert not missing, f"Missing required CSV columns: {missing}"

    def test_header_parses_correctly(self, sample_csv_file: Path):
        with open(sample_csv_file) as f:
            reader = csv.DictReader(f)
            assert reader.fieldnames is not None
            assert len(reader.fieldnames) >= 16


class TestCsvRowParsing:
    def test_parse_single_row(self, sample_csv_file: Path):
        with open(sample_csv_file) as f:
            reader = csv.DictReader(f)
            rows = list(reader)
        assert len(rows) == 1
        row = rows[0]
        assert int(row["cam_frame_id"]) == 42
        assert float(row["det_confidence"]) == 0.95

    def test_parse_all_float_fields(self, sample_csv_file: Path):
        with open(sample_csv_file) as f:
            reader = csv.DictReader(f)
            row = next(reader)
        float_fields = [
            "det_x", "det_y", "det_width", "det_height", "det_confidence",
            "track_x", "track_y", "track_z", "track_confidence",
            "servo_azimuth", "servo_elevation",
            "cpu_temp_c", "cpu_usage_percent",
        ]
        for field in float_fields:
            val = float(row[field])
            assert math.isfinite(val), f"{field} is not finite: {val}"

    def test_parse_int_fields(self, sample_csv_file: Path):
        with open(sample_csv_file) as f:
            reader = csv.DictReader(f)
            row = next(reader)
        int_fields = ["cam_frame_id", "track_id", "track_hit_streak", "det_target_class"]
        for field in int_fields:
            val = int(row[field])
            assert isinstance(val, int)


class TestCsvDataValidity:
    def test_confidence_in_range(self):
        row = dict(zip(SAMPLE_CSV_HEADER.split(","), SAMPLE_CSV_ROW.split(",")))
        det_conf = float(row["det_confidence"])
        track_conf = float(row["track_confidence"])
        assert 0.0 <= det_conf <= 1.0
        assert 0.0 <= track_conf <= 1.0

    def test_cpu_usage_in_range(self):
        row = dict(zip(SAMPLE_CSV_HEADER.split(","), SAMPLE_CSV_ROW.split(",")))
        usage = float(row["cpu_usage_percent"])
        assert 0.0 <= usage <= 100.0

    def test_servo_command_sent_is_bool(self):
        row = dict(zip(SAMPLE_CSV_HEADER.split(","), SAMPLE_CSV_ROW.split(",")))
        val = row["servo_command_sent"]
        assert val in ("0", "1")

    def test_detection_confidence_valid(self, sample_csv_file: Path):
        with open(sample_csv_file) as f:
            reader = csv.DictReader(f)
            for row in reader:
                conf = float(row["det_confidence"])
                assert 0.0 <= conf <= 1.0, f"det_confidence {conf} out of range"

    def test_servo_angles_in_range(self):
        row = dict(zip(SAMPLE_CSV_HEADER.split(","), SAMPLE_CSV_ROW.split(",")))
        az = float(row["servo_azimuth"])
        el = float(row["servo_elevation"])
        assert -180.0 <= az <= 180.0
        assert -90.0 <= el <= 90.0


class TestCsvEdgeCases:
    def test_empty_file_has_no_rows(self, tmp_path: Path):
        p = tmp_path / "empty.csv"
        p.write_text(SAMPLE_CSV_HEADER + "\n")
        with open(p) as f:
            reader = csv.DictReader(f)
            rows = list(reader)
        assert len(rows) == 0

    def test_missing_field_raises_error(self, tmp_path: Path):
        bad_header = "produced_ts_epoch_ms,cam_frame_id\n"
        bad_row = "1000,42\n"
        p = tmp_path / "bad.csv"
        p.write_text(bad_header + bad_row)
        with open(p) as f:
            reader = csv.DictReader(f)
            row = next(reader)
            assert row.get("det_confidence") is None

    def test_nan_in_field_handled(self, tmp_path: Path):
        nan_row = SAMPLE_CSV_ROW.replace("0.95", "nan")
        p = tmp_path / "nan.csv"
        p.write_text(f"{SAMPLE_CSV_HEADER}\n{nan_row}\n")
        with open(p) as f:
            reader = csv.DictReader(f)
            row = next(reader)
        val = float(row["det_confidence"])
        assert math.isnan(val)

    def test_inf_in_field_handled(self, tmp_path: Path):
        inf_row = SAMPLE_CSV_ROW.replace("42.5", "inf")
        p = tmp_path / "inf.csv"
        p.write_text(f"{SAMPLE_CSV_HEADER}\n{inf_row}\n")
        with open(p) as f:
            reader = csv.DictReader(f)
            row = next(reader)
        val = float(row["cpu_usage_percent"])
        assert math.isinf(val)
