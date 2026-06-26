---
source_file: "include/aurore/safety_monitor.hpp"
type: "code"
community: "Community 9 (safetymonitor)"
location: "L322"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/Community_9_safetymonitor
---

# SafetyMonitor

## Connections
- [[.SafetyMonitor()]] - `references` [EXTRACTED]
- [[.WatchdogKick()]] - `references` [EXTRACTED]
- [[.check_actuation_health()]] - `method` [EXTRACTED]
- [[.check_vision_health()]] - `method` [EXTRACTED]
- [[.clear_fault()]] - `method` [EXTRACTED]
- [[.current_fault()]] - `method` [EXTRACTED]
- [[.deadline_misses()]] - `method` [EXTRACTED]
- [[.generate_health_report()]] - `method` [EXTRACTED]
- [[.get_correlated_subsystem()]] - `method` [EXTRACTED]
- [[.get_healthy_frames()]] - `method` [EXTRACTED]
- [[.get_total_frames()]] - `method` [EXTRACTED]
- [[.get_total_stalls()]] - `method` [EXTRACTED]
- [[.init()]] - `method` [EXTRACTED]
- [[.is_emergency_active()]] - `method` [EXTRACTED]
- [[.is_running()_3]] - `method` [EXTRACTED]
- [[.is_system_safe()]] - `method` [EXTRACTED]
- [[.kick_watchdog()]] - `method` [EXTRACTED]
- [[.record_frame_complete()]] - `method` [EXTRACTED]
- [[.record_stage_latency()]] - `method` [EXTRACTED]
- [[.reset_stage_stats()]] - `method` [EXTRACTED]
- [[.run_cycle()]] - `method` [EXTRACTED]
- [[.set_log_callback()]] - `method` [EXTRACTED]
- [[.set_recovery_callback()]] - `method` [EXTRACTED]
- [[.set_safety_action_callback()]] - `method` [EXTRACTED]
- [[.start()]] - `method` [EXTRACTED]
- [[.stop()]] - `method` [EXTRACTED]
- [[.trigger_emergency_stop()]] - `method` [EXTRACTED]
- [[.trigger_fault()]] - `method` [EXTRACTED]
- [[.update_actuation_frame()]] - `method` [EXTRACTED]
- [[.update_vision_frame()]] - `method` [EXTRACTED]
- [[.watchdog_thread_func()]] - `method` [EXTRACTED]
- [[SafetyEvent]] - `references` [EXTRACTED]
- [[SafetyFaultCode]] - `references` [EXTRACTED]
- [[SafetyMonitorConfig]] - `references` [EXTRACTED]
- [[StageLatencyStats]] - `references` [EXTRACTED]
- [[TimestampNs]] - `references` [EXTRACTED]
- [[WatchdogKick]] - `references` [EXTRACTED]
- [[actuation_frame_count_]] - `defines` [EXTRACTED]
- [[atomic]] - `references` [EXTRACTED]
- [[config__6]] - `defines` [EXTRACTED]
- [[consecutive_misses_]] - `defines` [EXTRACTED]
- [[emergency_active_]] - `defines` [EXTRACTED]
- [[healthy_frames_]] - `defines` [EXTRACTED]
- [[last_actuation_count_]] - `defines` [EXTRACTED]
- [[last_actuation_sequence_]] - `defines` [EXTRACTED]
- [[last_actuation_timestamp_ns_]] - `defines` [EXTRACTED]
- [[last_event_]] - `defines` [EXTRACTED]
- [[last_kick_time_ns_]] - `defines` [EXTRACTED]
- [[last_vision_count_]] - `defines` [EXTRACTED]
- [[last_vision_sequence_]] - `defines` [EXTRACTED]
- [[last_vision_timestamp_ns_]] - `defines` [EXTRACTED]
- [[log_callback_]] - `references` [EXTRACTED]
- [[log_user_data_]] - `defines` [EXTRACTED]
- [[recovery_callback_]] - `references` [EXTRACTED]
- [[recovery_user_data_]] - `defines` [EXTRACTED]
- [[running__8]] - `defines` [EXTRACTED]
- [[safety_action_callback_]] - `references` [EXTRACTED]
- [[safety_action_user_data_]] - `defines` [EXTRACTED]
- [[safety_monitor.hpp]] - `contains` [EXTRACTED]
- [[stage_stats_]] - `defines` [EXTRACTED]
- [[system_safe_]] - `defines` [EXTRACTED]
- [[thread]] - `references` [EXTRACTED]
- [[total_frame_stalls_]] - `defines` [EXTRACTED]
- [[total_frames_]] - `defines` [EXTRACTED]
- [[vision_frame_count_]] - `defines` [EXTRACTED]
- [[watchdog_running_]] - `defines` [EXTRACTED]
- [[watchdog_thread_]] - `defines` [EXTRACTED]

#graphify/code #graphify/EXTRACTED #community/Community_9_safetymonitor