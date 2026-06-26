---
source_file: "tests/unit/test_state_machine_transitions.cpp"
type: "code"
community: "Testing & Safety Monitor"
location: "L80"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/Testing__Safety_Monitor
---

# TEST()

## Connections
- [[test_armed_fire_command]] - `references` [EXTRACTED]
- [[test_armed_fire_command_without_interlock]] - `references` [EXTRACTED]
- [[test_armed_interlock_control]] - `references` [EXTRACTED]
- [[test_armed_no_timeout_before_threshold]] - `references` [EXTRACTED]
- [[test_armed_only_from_tracking]] - `references` [EXTRACTED]
- [[test_armed_requires_operator_authorization]] - `references` [EXTRACTED]
- [[test_armed_timeout_returns_to_tracking]] - `references` [EXTRACTED]
- [[test_armed_to_fault_on_fault]] - `references` [EXTRACTED]
- [[test_boot_state_transitions]] - `references` [EXTRACTED]
- [[test_boot_to_fault_on_fault]] - `references` [EXTRACTED]
- [[test_concurrent_fault_injection]] - `references` [EXTRACTED]
- [[test_detection_structure]] - `references` [EXTRACTED]
- [[test_fault_from_all_states]] - `references` [EXTRACTED]
- [[test_fault_from_armed_latches]] - `references` [EXTRACTED]
- [[test_fault_interlock_inhibit]] - `references` [EXTRACTED]
- [[test_fault_no_automatic_recovery]] - `references` [EXTRACTED]
- [[test_fault_state_latching]] - `references` [EXTRACTED]
- [[test_fcs_state_enum_values]] - `references` [EXTRACTED]
- [[test_fcs_state_names]] - `references` [EXTRACTED]
- [[test_fire_control_solution_structure]] - `references` [EXTRACTED]
- [[test_freecam_interlock_inhibit]] - `references` [EXTRACTED]
- [[test_freecam_no_auto_lock]] - `references` [EXTRACTED]
- [[test_freecam_to_fault_on_fault]] - `references` [EXTRACTED]
- [[test_freecam_transitions_to_idle_safe]] - `references` [EXTRACTED]
- [[test_freecam_transitions_to_search]] - `references` [EXTRACTED]
- [[test_has_operator_authorization]] - `references` [EXTRACTED]
- [[test_has_valid_lock]] - `references` [EXTRACTED]
- [[test_has_zero_faults]] - `references` [EXTRACTED]
- [[test_idle_safe_interlock_inhibit]] - `references` [EXTRACTED]
- [[test_idle_safe_to_fault_on_fault]] - `references` [EXTRACTED]
- [[test_idle_safe_transitions_to_freecam]] - `references` [EXTRACTED]
- [[test_idle_safe_transitions_to_search]] - `references` [EXTRACTED]
- [[test_rapid_state_transitions]] - `references` [EXTRACTED]
- [[test_search_detection_below_confidence_threshold]] - `references` [EXTRACTED]
- [[test_search_gimbal_settling]] - `references` [EXTRACTED]
- [[test_search_no_timeout_before_threshold]] - `references` [EXTRACTED]
- [[test_search_to_fault_on_fault]] - `references` [EXTRACTED]
- [[test_search_transitions_to_idle_safe_on_timeout]] - `references` [EXTRACTED]
- [[test_search_transitions_to_tracking]] - `references` [EXTRACTED]
- [[test_state_age_accumulation]] - `references` [EXTRACTED]
- [[test_state_change_callback]] - `references` [EXTRACTED]
- [[test_state_machine_construction]] - `references` [EXTRACTED]
- [[test_state_machine_initial_state]] - `references` [EXTRACTED]
- [[test_state_machine_transitions.cpp]] - `contains` [EXTRACTED]
- [[test_track_solution_structure]] - `references` [EXTRACTED]
- [[test_tracking_maintains_on_valid_tracker]] - `references` [EXTRACTED]
- [[test_tracking_redetection_below_threshold]] - `references` [EXTRACTED]
- [[test_tracking_to_fault_on_fault]] - `references` [EXTRACTED]
- [[test_tracking_transitions_to_armed_with_conditions]] - `references` [EXTRACTED]
- [[test_tracking_transitions_to_search_on_lost_lock]] - `references` [EXTRACTED]
- [[test_transition_table_coverage]] - `references` [EXTRACTED]

#graphify/code #graphify/EXTRACTED #community/Testing__Safety_Monitor