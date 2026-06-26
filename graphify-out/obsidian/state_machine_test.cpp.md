---
source_file: "tests/unit/state_machine_test.cpp"
type: "code"
community: "Community 13 (state_machine_test.cpp)"
location: "L1"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/Community_13_state_machine_testcpp
---

# state_machine_test.cpp

## Connections
- [[main()_46]] - `contains` [EXTRACTED]
- [[test_armed_lock_lost_returns_to_tracking()]] - `contains` [EXTRACTED]
- [[test_armed_timeout_returns_to_tracking()]] - `contains` [EXTRACTED]
- [[test_boot_failure_transitions_to_fault()]] - `contains` [EXTRACTED]
- [[test_boot_to_idle_safe()]] - `contains` [EXTRACTED]
- [[test_cancel_from_freecam_to_idle_safe()]] - `contains` [EXTRACTED]
- [[test_cancel_from_idle_safe_no_effect()]] - `contains` [EXTRACTED]
- [[test_cancel_from_search_to_idle_safe()]] - `contains` [EXTRACTED]
- [[test_cancel_from_tracking_to_idle_safe()]] - `contains` [EXTRACTED]
- [[test_crc16_deterministic()]] - `contains` [EXTRACTED]
- [[test_disarm_from_armed_to_idle_safe()]] - `contains` [EXTRACTED]
- [[test_disarm_from_non_armed_no_effect()]] - `contains` [EXTRACTED]
- [[test_fault_from_any_state()]] - `contains` [EXTRACTED]
- [[test_fault_from_armed_transitions_to_fault()]] - `contains` [EXTRACTED]
- [[test_fault_from_boot_transitions_to_fault()]] - `contains` [EXTRACTED]
- [[test_fault_from_freecam_transitions_to_fault()]] - `contains` [EXTRACTED]
- [[test_fault_from_idle_safe_transitions_to_fault()]] - `contains` [EXTRACTED]
- [[test_fault_from_search_transitions_to_fault()]] - `contains` [EXTRACTED]
- [[test_fault_from_tracking_transitions_to_fault()]] - `contains` [EXTRACTED]
- [[test_fault_interlock_inhibit()]] - `contains` [EXTRACTED]
- [[test_idle_safe_to_freecam()]] - `contains` [EXTRACTED]
- [[test_idle_safe_to_search()]] - `contains` [EXTRACTED]
- [[test_initial_state()]] - `contains` [EXTRACTED]
- [[test_invalid_armed_transition_rejected()]] - `contains` [EXTRACTED]
- [[test_lock_confirmation_stable_window()]] - `contains` [EXTRACTED]
- [[test_manual_reset_clears_authorization()]] - `contains` [EXTRACTED]
- [[test_manual_reset_clears_fault_latch()]] - `contains` [EXTRACTED]
- [[test_manual_reset_from_fault_to_idle_safe()]] - `contains` [EXTRACTED]
- [[test_position_stability_boundary_2px()]] - `contains` [EXTRACTED]
- [[test_position_stability_exceeds_2px()]] - `contains` [EXTRACTED]
- [[test_position_stability_reset_on_state_change()]] - `contains` [EXTRACTED]
- [[test_position_stability_single_frame_not_stable()]] - `contains` [EXTRACTED]
- [[test_position_stability_three_stable_frames_transitions()]] - `contains` [EXTRACTED]
- [[test_position_stability_unstable_positions_no_transition()]] - `contains` [EXTRACTED]
- [[test_range_above_maximum_rejected()]] - `contains` [EXTRACTED]
- [[test_range_below_minimum_rejected()]] - `contains` [EXTRACTED]
- [[test_range_boundary_values_accepted()]] - `contains` [EXTRACTED]
- [[test_range_checksum_mismatch_rejected()]] - `contains` [EXTRACTED]
- [[test_range_infinity_rejected()]] - `contains` [EXTRACTED]
- [[test_range_nan_rejected()]] - `contains` [EXTRACTED]
- [[test_range_stale_data_rejected()]] - `contains` [EXTRACTED]
- [[test_range_valid_data_accepted()]] - `contains` [EXTRACTED]
- [[test_search_detection_transitions_to_tracking()]] - `contains` [EXTRACTED]
- [[test_search_timeout()]] - `contains` [EXTRACTED]
- [[test_tracking_deselect_low_confidence()]] - `contains` [EXTRACTED]
- [[test_tracking_good_psr_resets_counter()]] - `contains` [EXTRACTED]
- [[test_tracking_lost_returns_to_search()]] - `contains` [EXTRACTED]
- [[test_tracking_to_armed_with_conditions()]] - `contains` [EXTRACTED]
- [[test_transition_table_completeness()]] - `contains` [EXTRACTED]

#graphify/code #graphify/EXTRACTED #community/Community_13_state_machine_testcpp