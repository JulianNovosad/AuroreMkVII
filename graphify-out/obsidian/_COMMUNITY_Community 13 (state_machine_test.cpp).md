---
type: community
cohesion: 0.08
members: 50
---

# Community 13 (state_machine_test.cpp)

**Cohesion:** 0.08 - loosely connected
**Members:** 50 nodes

## Members
- [[main()_46]] - code - tests/unit/state_machine_test.cpp
- [[state_machine_test.cpp]] - code - tests/unit/state_machine_test.cpp
- [[test_armed_lock_lost_returns_to_tracking()]] - code - tests/unit/state_machine_test.cpp
- [[test_armed_timeout_returns_to_tracking()]] - code - tests/unit/state_machine_test.cpp
- [[test_boot_failure_transitions_to_fault()]] - code - tests/unit/state_machine_test.cpp
- [[test_boot_to_idle_safe()]] - code - tests/unit/state_machine_test.cpp
- [[test_cancel_from_freecam_to_idle_safe()]] - code - tests/unit/state_machine_test.cpp
- [[test_cancel_from_idle_safe_no_effect()]] - code - tests/unit/state_machine_test.cpp
- [[test_cancel_from_search_to_idle_safe()]] - code - tests/unit/state_machine_test.cpp
- [[test_cancel_from_tracking_to_idle_safe()]] - code - tests/unit/state_machine_test.cpp
- [[test_crc16_deterministic()]] - code - tests/unit/state_machine_test.cpp
- [[test_disarm_from_armed_to_idle_safe()]] - code - tests/unit/state_machine_test.cpp
- [[test_disarm_from_non_armed_no_effect()]] - code - tests/unit/state_machine_test.cpp
- [[test_fault_from_any_state()]] - code - tests/unit/state_machine_test.cpp
- [[test_fault_from_armed_transitions_to_fault()]] - code - tests/unit/state_machine_test.cpp
- [[test_fault_from_boot_transitions_to_fault()]] - code - tests/unit/state_machine_test.cpp
- [[test_fault_from_freecam_transitions_to_fault()]] - code - tests/unit/state_machine_test.cpp
- [[test_fault_from_idle_safe_transitions_to_fault()]] - code - tests/unit/state_machine_test.cpp
- [[test_fault_from_search_transitions_to_fault()]] - code - tests/unit/state_machine_test.cpp
- [[test_fault_from_tracking_transitions_to_fault()]] - code - tests/unit/state_machine_test.cpp
- [[test_fault_interlock_inhibit()]] - code - tests/unit/state_machine_test.cpp
- [[test_idle_safe_to_freecam()]] - code - tests/unit/state_machine_test.cpp
- [[test_idle_safe_to_search()]] - code - tests/unit/state_machine_test.cpp
- [[test_initial_state()]] - code - tests/unit/state_machine_test.cpp
- [[test_invalid_armed_transition_rejected()]] - code - tests/unit/state_machine_test.cpp
- [[test_lock_confirmation_stable_window()]] - code - tests/unit/state_machine_test.cpp
- [[test_manual_reset_clears_authorization()]] - code - tests/unit/state_machine_test.cpp
- [[test_manual_reset_clears_fault_latch()]] - code - tests/unit/state_machine_test.cpp
- [[test_manual_reset_from_fault_to_idle_safe()]] - code - tests/unit/state_machine_test.cpp
- [[test_position_stability_boundary_2px()]] - code - tests/unit/state_machine_test.cpp
- [[test_position_stability_exceeds_2px()]] - code - tests/unit/state_machine_test.cpp
- [[test_position_stability_reset_on_state_change()]] - code - tests/unit/state_machine_test.cpp
- [[test_position_stability_single_frame_not_stable()]] - code - tests/unit/state_machine_test.cpp
- [[test_position_stability_three_stable_frames_transitions()]] - code - tests/unit/state_machine_test.cpp
- [[test_position_stability_unstable_positions_no_transition()]] - code - tests/unit/state_machine_test.cpp
- [[test_range_above_maximum_rejected()]] - code - tests/unit/state_machine_test.cpp
- [[test_range_below_minimum_rejected()]] - code - tests/unit/state_machine_test.cpp
- [[test_range_boundary_values_accepted()]] - code - tests/unit/state_machine_test.cpp
- [[test_range_checksum_mismatch_rejected()]] - code - tests/unit/state_machine_test.cpp
- [[test_range_infinity_rejected()]] - code - tests/unit/state_machine_test.cpp
- [[test_range_nan_rejected()]] - code - tests/unit/state_machine_test.cpp
- [[test_range_stale_data_rejected()]] - code - tests/unit/state_machine_test.cpp
- [[test_range_valid_data_accepted()]] - code - tests/unit/state_machine_test.cpp
- [[test_search_detection_transitions_to_tracking()]] - code - tests/unit/state_machine_test.cpp
- [[test_search_timeout()]] - code - tests/unit/state_machine_test.cpp
- [[test_tracking_deselect_low_confidence()]] - code - tests/unit/state_machine_test.cpp
- [[test_tracking_good_psr_resets_counter()]] - code - tests/unit/state_machine_test.cpp
- [[test_tracking_lost_returns_to_search()]] - code - tests/unit/state_machine_test.cpp
- [[test_tracking_to_armed_with_conditions()]] - code - tests/unit/state_machine_test.cpp
- [[test_transition_table_completeness()]] - code - tests/unit/state_machine_test.cpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_13_state_machine_testcpp
SORT file.name ASC
```
