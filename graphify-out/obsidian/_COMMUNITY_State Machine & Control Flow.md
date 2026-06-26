---
type: community
cohesion: 0.02
members: 87
---

# State Machine & Control Flow

**Cohesion:** 0.02 - loosely connected
**Members:** 87 nodes

## Members
- [[.clear_fault_latch_for_test()]] - code - include/aurore/state_machine.hpp
- [[.has_valid_range()]] - code - include/aurore/state_machine.hpp
- [[.set_timing_stable()]] - code - include/aurore/state_machine.hpp
- [[.set_timing_stable_for_test()]] - code - include/aurore/state_machine.hpp
- [[StateMachine]] - code - include/aurore/state_machine.hpp
- [[align_sustained_ms_]] - code - include/aurore/state_machine.hpp
- [[check_prediction_delta]] - code - include/aurore/state_machine.hpp
- [[compute_crc16]] - code - include/aurore/state_machine.hpp
- [[enter_state]] - code - include/aurore/state_machine.hpp
- [[fault_latched_]] - code - include/aurore/state_machine.hpp
- [[first_detection_]] - code - include/aurore/state_machine.hpp
- [[force_state_for_test]] - code - include/aurore/state_machine.hpp
- [[gimbal_]] - code - include/aurore/state_machine.hpp
- [[has_operator_authorization]] - code - include/aurore/state_machine.hpp
- [[has_stable_timing]] - code - include/aurore/state_machine.hpp
- [[has_valid_lock]] - code - include/aurore/state_machine.hpp
- [[has_zero_faults]] - code - include/aurore/state_machine.hpp
- [[have_first_detection_]] - code - include/aurore/state_machine.hpp
- [[have_prediction_]] - code - include/aurore/state_machine.hpp
- [[have_valid_range_]] - code - include/aurore/state_machine.hpp
- [[interlock_enabled__1]] - code - include/aurore/state_machine.hpp
- [[is_interlock_enabled]] - code - include/aurore/state_machine.hpp
- [[is_position_stable]] - code - include/aurore/state_machine.hpp
- [[kAlignErrorMaxDeg]] - code - include/aurore/state_machine.hpp
- [[kAlignSustainMs]] - code - include/aurore/state_machine.hpp
- [[kArmedTimeoutMs]] - code - include/aurore/state_machine.hpp
- [[kConfidenceMin]] - code - include/aurore/state_machine.hpp
- [[kGimbalErrorMaxDeg]] - code - include/aurore/state_machine.hpp
- [[kGimbalVelocityMaxDs]] - code - include/aurore/state_machine.hpp
- [[kLockConfirmThreshold]] - code - include/aurore/state_machine.hpp
- [[kLockConfirmWindowMs]] - code - include/aurore/state_machine.hpp
- [[kLowConfFramesMax]] - code - include/aurore/state_machine.hpp
- [[kPHitMin]] - code - include/aurore/state_machine.hpp
- [[kPositionStabilityPx]] - code - include/aurore/state_machine.hpp
- [[kPredictionDeltaPx]] - code - include/aurore/state_machine.hpp
- [[kPsrFailThreshold]] - code - include/aurore/state_machine.hpp
- [[kPsrLowThreshold]] - code - include/aurore/state_machine.hpp
- [[kRedetectionScoreMin]] - code - include/aurore/state_machine.hpp
- [[kSearchTimeoutMs]] - code - include/aurore/state_machine.hpp
- [[kSettledFramesMin]] - code - include/aurore/state_machine.hpp
- [[kSpatialGatePx]] - code - include/aurore/state_machine.hpp
- [[kStableFramesMin]] - code - include/aurore/state_machine.hpp
- [[last_valid_range_]] - code - include/aurore/state_machine.hpp
- [[lock_confirm_age_ms_]] - code - include/aurore/state_machine.hpp
- [[lock_confirm_stable_frames_]] - code - include/aurore/state_machine.hpp
- [[lock_confirmed_]] - code - include/aurore/state_machine.hpp
- [[low_conf_frames_]] - code - include/aurore/state_machine.hpp
- [[on_ballistics_solution]] - code - include/aurore/state_machine.hpp
- [[on_boot_failure]] - code - include/aurore/state_machine.hpp
- [[on_detection]] - code - include/aurore/state_machine.hpp
- [[on_fault]] - code - include/aurore/state_machine.hpp
- [[on_fire_command]] - code - include/aurore/state_machine.hpp
- [[on_gimbal_status]] - code - include/aurore/state_machine.hpp
- [[on_init_complete]] - code - include/aurore/state_machine.hpp
- [[on_lrf_range]] - code - include/aurore/state_machine.hpp
- [[on_manual_reset]] - code - include/aurore/state_machine.hpp
- [[on_redetection_score]] - code - include/aurore/state_machine.hpp
- [[on_tracker_initialized]] - code - include/aurore/state_machine.hpp
- [[on_tracker_update]] - code - include/aurore/state_machine.hpp
- [[operator_authorized_]] - code - include/aurore/state_machine.hpp
- [[position_history_]] - code - include/aurore/state_machine.hpp
- [[position_history_idx_]] - code - include/aurore/state_machine.hpp
- [[position_valid_]] - code - include/aurore/state_machine.hpp
- [[predicted_x_]] - code - include/aurore/state_machine.hpp
- [[predicted_y_]] - code - include/aurore/state_machine.hpp
- [[range_age_ms_]] - code - include/aurore/state_machine.hpp
- [[redetection_score_]] - code - include/aurore/state_machine.hpp
- [[request_cancel]] - code - include/aurore/state_machine.hpp
- [[request_disarm]] - code - include/aurore/state_machine.hpp
- [[request_freecam]] - code - include/aurore/state_machine.hpp
- [[request_search]] - code - include/aurore/state_machine.hpp
- [[reset_target_validation]] - code - include/aurore/state_machine.hpp
- [[set_interlock_enabled]] - code - include/aurore/state_machine.hpp
- [[set_operator_authorization]] - code - include/aurore/state_machine.hpp
- [[set_state_change_callback]] - code - include/aurore/state_machine.hpp
- [[solution_]] - code - include/aurore/state_machine.hpp
- [[stable_frame_count_]] - code - include/aurore/state_machine.hpp
- [[state_2]] - code - include/aurore/state_machine.hpp
- [[state_age_]] - code - include/aurore/state_machine.hpp
- [[tick]] - code - include/aurore/state_machine.hpp
- [[timing_stable_]] - code - include/aurore/state_machine.hpp
- [[transition]] - code - include/aurore/state_machine.hpp
- [[update_lock_confirmation]] - code - include/aurore/state_machine.hpp
- [[update_position_history]] - code - include/aurore/state_machine.hpp
- [[update_prediction]] - code - include/aurore/state_machine.hpp
- [[velocity_history_]] - code - include/aurore/state_machine.hpp
- [[velocity_history_idx_]] - code - include/aurore/state_machine.hpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/State_Machine__Control_Flow
SORT file.name ASC
```

## Connections to other communities
- 4 edges to [[_COMMUNITY_Community 82]]
- 2 edges to [[_COMMUNITY_Community 33]]
- 2 edges to [[_COMMUNITY_Community 223]]
- 1 edge to [[_COMMUNITY_Community 68]]
- 1 edge to [[_COMMUNITY_Community 409]]
- 1 edge to [[_COMMUNITY_Community 554]]

## Top bridge nodes
- [[StateMachine]] - degree 97, connects to 6 communities