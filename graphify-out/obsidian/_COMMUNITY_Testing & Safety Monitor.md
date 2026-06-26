---
type: community
cohesion: 0.04
members: 48
---

# Testing & Safety Monitor

**Cohesion:** 0.04 - loosely connected
**Members:** 48 nodes

## Members
- [[TEST()_24]] - code - tests/unit/test_safety_monitor_fault_codes.cpp
- [[sleep_ms()_5]] - code - tests/unit/test_safety_monitor_fault_codes.cpp
- [[test_fault_code_actuation_command_invalid]] - code
- [[test_fault_code_actuation_latency_exceeded]] - code
- [[test_fault_code_actuation_stalled]] - code
- [[test_fault_code_all_24_values]] - code
- [[test_fault_code_camera_timeout]] - code
- [[test_fault_code_categories]] - code
- [[test_fault_code_consecutive_deadline_misses]] - code
- [[test_fault_code_critical_temperature]] - code
- [[test_fault_code_emergency_stop_requested]] - code
- [[test_fault_code_frame_deadline_missed]] - code
- [[test_fault_code_i2c_nack]] - code
- [[test_fault_code_i2c_timeout]] - code
- [[test_fault_code_interlock_fault]] - code
- [[test_fault_code_memory_lock_failed]] - code
- [[test_fault_code_none]] - code
- [[test_fault_code_power_fault]] - code
- [[test_fault_code_range_data_invalid]] - code
- [[test_fault_code_range_data_stale]] - code
- [[test_fault_code_safety_comparator_mismatch]] - code
- [[test_fault_code_scheduling_policy_failed]] - code
- [[test_fault_code_timestamp_non_monotonic]] - code
- [[test_fault_code_vision_buffer_overrun]] - code
- [[test_fault_code_vision_latency_exceeded]] - code
- [[test_fault_code_vision_stalled]] - code
- [[test_fault_code_watchdog_feed_failed]] - code
- [[test_fault_severity_levels]] - code
- [[test_per_stage_monitor_config]] - code
- [[test_pipeline_stage_enum]] - code
- [[test_safety_event_initialization]] - code
- [[test_safety_event_max_reason_length]] - code
- [[test_safety_event_population]] - code
- [[test_safety_monitor_frame_counting]] - code
- [[test_safety_monitor_health_report_disabled]] - code
- [[test_safety_monitor_health_report_generation]] - code
- [[test_safety_monitor_record_stage_latency]] - code
- [[test_safety_monitor_recovery_callback]] - code
- [[test_safety_monitor_reset_stage_stats]] - code
- [[test_safety_monitor_stage_stall_detection]] - code
- [[test_stage_latency_stats_average]] - code
- [[test_stage_latency_stats_construction]] - code
- [[test_stage_latency_stats_record_latency]] - code
- [[test_stage_latency_stats_reset]] - code
- [[test_stage_latency_stats_stall_detection]] - code
- [[test_watchdog_kick_raii_basic]] - code
- [[test_watchdog_kick_raii_multiple_scopes]] - code
- [[test_watchdog_kick_raii_nested]] - code

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Testing__Safety_Monitor
SORT file.name ASC
```

## Connections to other communities
- 2 edges to [[_COMMUNITY_Community 2 (main)]]

## Top bridge nodes
- [[TEST()_24]] - degree 48, connects to 1 community
- [[sleep_ms()_5]] - degree 2, connects to 1 community