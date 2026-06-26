---
source_file: "tests/unit/state_machine_test.cpp"
type: "code"
community: "Community 13 (state_machine_test.cpp)"
location: "L781"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/Community_13_state_machine_testcpp
---

# main()

## Connections
- [[state_machine_test.cpp]] - `contains` [EXTRACTED]
- [[test_armed_lock_lost_returns_to_tracking()]] - `calls` [EXTRACTED]
- [[test_armed_timeout_returns_to_tracking()]] - `calls` [EXTRACTED]
- [[test_boot_failure_transitions_to_fault()]] - `calls` [EXTRACTED]
- [[test_boot_to_idle_safe()]] - `calls` [EXTRACTED]
- [[test_cancel_from_freecam_to_idle_safe()]] - `calls` [EXTRACTED]
- [[test_cancel_from_idle_safe_no_effect()]] - `calls` [EXTRACTED]
- [[test_cancel_from_search_to_idle_safe()]] - `calls` [EXTRACTED]
- [[test_cancel_from_tracking_to_idle_safe()]] - `calls` [EXTRACTED]
- [[test_crc16_deterministic()]] - `calls` [EXTRACTED]
- [[test_disarm_from_armed_to_idle_safe()]] - `calls` [EXTRACTED]
- [[test_disarm_from_non_armed_no_effect()]] - `calls` [EXTRACTED]
- [[test_fault_from_any_state()]] - `calls` [EXTRACTED]
- [[test_fault_from_armed_transitions_to_fault()]] - `calls` [EXTRACTED]
- [[test_fault_from_boot_transitions_to_fault()]] - `calls` [EXTRACTED]
- [[test_fault_from_freecam_transitions_to_fault()]] - `calls` [EXTRACTED]
- [[test_fault_from_idle_safe_transitions_to_fault()]] - `calls` [EXTRACTED]
- [[test_fault_from_search_transitions_to_fault()]] - `calls` [EXTRACTED]
- [[test_fault_from_tracking_transitions_to_fault()]] - `calls` [EXTRACTED]
- [[test_fault_interlock_inhibit()]] - `calls` [EXTRACTED]
- [[test_idle_safe_to_freecam()]] - `calls` [EXTRACTED]
- [[test_idle_safe_to_search()]] - `calls` [EXTRACTED]
- [[test_initial_state()]] - `calls` [EXTRACTED]
- [[test_invalid_armed_transition_rejected()]] - `calls` [EXTRACTED]
- [[test_lock_confirmation_stable_window()]] - `calls` [EXTRACTED]
- [[test_manual_reset_clears_authorization()]] - `calls` [EXTRACTED]
- [[test_manual_reset_clears_fault_latch()]] - `calls` [EXTRACTED]
- [[test_manual_reset_from_fault_to_idle_safe()]] - `calls` [EXTRACTED]
- [[test_position_stability_boundary_2px()]] - `calls` [EXTRACTED]
- [[test_position_stability_exceeds_2px()]] - `calls` [EXTRACTED]
- [[test_position_stability_reset_on_state_change()]] - `calls` [EXTRACTED]
- [[test_position_stability_single_frame_not_stable()]] - `calls` [EXTRACTED]
- [[test_position_stability_three_stable_frames_transitions()]] - `calls` [EXTRACTED]
- [[test_position_stability_unstable_positions_no_transition()]] - `calls` [EXTRACTED]
- [[test_range_above_maximum_rejected()]] - `calls` [EXTRACTED]
- [[test_range_below_minimum_rejected()]] - `calls` [EXTRACTED]
- [[test_range_boundary_values_accepted()]] - `calls` [EXTRACTED]
- [[test_range_checksum_mismatch_rejected()]] - `calls` [EXTRACTED]
- [[test_range_infinity_rejected()]] - `calls` [EXTRACTED]
- [[test_range_nan_rejected()]] - `calls` [EXTRACTED]
- [[test_range_stale_data_rejected()]] - `calls` [EXTRACTED]
- [[test_range_valid_data_accepted()]] - `calls` [EXTRACTED]
- [[test_search_detection_transitions_to_tracking()]] - `calls` [EXTRACTED]
- [[test_search_timeout()]] - `calls` [EXTRACTED]
- [[test_tracking_deselect_low_confidence()]] - `calls` [EXTRACTED]
- [[test_tracking_good_psr_resets_counter()]] - `calls` [EXTRACTED]
- [[test_tracking_lost_returns_to_search()]] - `calls` [EXTRACTED]
- [[test_tracking_to_armed_with_conditions()]] - `calls` [EXTRACTED]
- [[test_transition_table_completeness()]] - `calls` [EXTRACTED]

#graphify/code #graphify/EXTRACTED #community/Community_13_state_machine_testcpp