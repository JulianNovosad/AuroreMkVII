---
type: community
cohesion: 0.06
members: 62
---

# Community 4 (.test_loads_valid_json)

**Cohesion:** 0.06 - loosely connected
**Members:** 62 nodes

## Members
- [[.test_ballistics_profiles_valid()]] - code - tests/python/test_config_validation.py
- [[.test_camera_fps_within_range()]] - code - tests/python/test_config_validation.py
- [[.test_camera_offset_exists()]] - code - tests/python/test_config_validation.py
- [[.test_camera_resolution_ratios()]] - code - tests/python/test_config_validation.py
- [[.test_camera_section()]] - code - tests/python/test_config_validation.py
- [[.test_confidence_in_range()]] - code - tests/python/test_telemetry_csv.py
- [[.test_cpu_affinities_in_range()]] - code - tests/python/test_config_validation.py
- [[.test_cpu_usage_in_range()]] - code - tests/python/test_telemetry_csv.py
- [[.test_detection_confidence_valid()]] - code - tests/python/test_telemetry_csv.py
- [[.test_empty_file_has_no_rows()]] - code - tests/python/test_telemetry_csv.py
- [[.test_gimbal_azimuth_limits()]] - code - tests/python/test_config_validation.py
- [[.test_gimbal_elevation_limits()]] - code - tests/python/test_config_validation.py
- [[.test_gimbal_i2c_config()]] - code - tests/python/test_config_validation.py
- [[.test_gimbal_section()]] - code - tests/python/test_config_validation.py
- [[.test_has_servo_section()]] - code - tests/python/test_config_validation.py
- [[.test_header_parses_correctly()]] - code - tests/python/test_telemetry_csv.py
- [[.test_inf_in_field_handled()]] - code - tests/python/test_telemetry_csv.py
- [[.test_interlock_default_state()]] - code - tests/python/test_config_validation.py
- [[.test_loads_valid_json()_1]] - code - tests/python/test_config_validation.py
- [[.test_loads_valid_json()]] - code - tests/python/test_config_validation.py
- [[.test_missing_field_raises_error()]] - code - tests/python/test_telemetry_csv.py
- [[.test_nan_in_field_handled()]] - code - tests/python/test_telemetry_csv.py
- [[.test_network_ports_in_range()]] - code - tests/python/test_config_validation.py
- [[.test_no_unknown_top_level_keys()]] - code - tests/python/test_config_validation.py
- [[.test_parse_all_float_fields()]] - code - tests/python/test_telemetry_csv.py
- [[.test_parse_int_fields()]] - code - tests/python/test_telemetry_csv.py
- [[.test_parse_single_row()]] - code - tests/python/test_telemetry_csv.py
- [[.test_required_columns_present()]] - code - tests/python/test_telemetry_csv.py
- [[.test_required_top_level_keys()]] - code - tests/python/test_config_validation.py
- [[.test_safety_fault_timeouts()]] - code - tests/python/test_config_validation.py
- [[.test_safety_vision_deadline()]] - code - tests/python/test_config_validation.py
- [[.test_safety_watchdog()]] - code - tests/python/test_config_validation.py
- [[.test_scheduler_priorities_in_range()]] - code - tests/python/test_config_validation.py
- [[.test_servo_angles_in_range()]] - code - tests/python/test_telemetry_csv.py
- [[.test_servo_command_sent_is_bool()]] - code - tests/python/test_telemetry_csv.py
- [[.test_system_section()]] - code - tests/python/test_config_validation.py
- [[.test_version_string()]] - code - tests/python/test_config_validation.py
- [[Path]] - code
- [[Telemetry CSV Log Validation Tests  Tests the CSV log format produced by Telemet]] - rationale - tests/python/test_telemetry_csv.py
- [[TestCalibrationConfig]] - code - tests/python/test_config_validation.py
- [[TestConfigStructure]] - code - tests/python/test_config_validation.py
- [[TestConfigValues]] - code - tests/python/test_config_validation.py
- [[TestCsvDataValidity]] - code - tests/python/test_telemetry_csv.py
- [[TestCsvEdgeCases]] - code - tests/python/test_telemetry_csv.py
- [[TestCsvHeader]] - code - tests/python/test_telemetry_csv.py
- [[TestCsvRowParsing]] - code - tests/python/test_telemetry_csv.py
- [[aurore_binary()]] - code - tests/python/conftest.py
- [[aurore_ports_open()]] - code - tests/python/conftest.py
- [[aurore_running()]] - code - tests/python/conftest.py
- [[build_dir()]] - code - tests/python/conftest.py
- [[calibration_path()]] - code - tests/python/conftest.py
- [[command_port()]] - code - tests/python/conftest.py
- [[config_dir()]] - code - tests/python/conftest.py
- [[config_path()]] - code - tests/python/conftest.py
- [[conftest.py]] - code - tests/python/conftest.py
- [[load_json()]] - code - tests/python/test_config_validation.py
- [[project_root()]] - code - tests/python/conftest.py
- [[pytest_configure()]] - code - tests/python/conftest.py
- [[sample_csv_file()]] - code - tests/python/test_telemetry_csv.py
- [[telemetry_port()]] - code - tests/python/conftest.py
- [[test_config_validation.py]] - code - tests/python/test_config_validation.py
- [[test_telemetry_csv.py]] - code - tests/python/test_telemetry_csv.py

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_4_test_loads_valid_json
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 143]]
- 1 edge to [[_COMMUNITY_Community 11 (connect_to)]]

## Top bridge nodes
- [[conftest.py]] - degree 12, connects to 1 community
- [[test_config_validation.py]] - degree 5, connects to 1 community