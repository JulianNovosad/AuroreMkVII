---
type: community
cohesion: 0.09
members: 40
---

# Community 33

**Cohesion:** 0.09 - loosely connected
**Members:** 40 nodes

## Members
- [[StateMachine()]] - code - src/state_machine/state_machine.cpp
- [[check_prediction_delta()]] - code - src/state_machine/state_machine.cpp
- [[compute_crc16()]] - code - src/state_machine/state_machine.cpp
- [[enter_state()]] - code - src/state_machine/state_machine.cpp
- [[fault_code]] - code - include/aurore/safety_monitor.hpp
- [[fcs_state]] - code - include/aurore/hud_socket.hpp
- [[fcs_state_name()]] - code - src/state_machine/state_machine.cpp
- [[force_state_for_test()]] - code - src/state_machine/state_machine.cpp
- [[has_operator_authorization()]] - code - src/state_machine/state_machine.cpp
- [[has_stable_timing()]] - code - src/state_machine/state_machine.cpp
- [[has_valid_lock()]] - code - src/state_machine/state_machine.cpp
- [[has_zero_faults()]] - code - src/state_machine/state_machine.cpp
- [[is_interlock_enabled()]] - code - src/state_machine/state_machine.cpp
- [[is_position_stable()]] - code - src/state_machine/state_machine.cpp
- [[on_ballistics_solution()]] - code - src/state_machine/state_machine.cpp
- [[on_boot_failure()]] - code - src/state_machine/state_machine.cpp
- [[on_change_]] - code - include/aurore/state_machine.hpp
- [[on_detection()]] - code - src/state_machine/state_machine.cpp
- [[on_fault()]] - code - src/state_machine/state_machine.cpp
- [[on_fire_command()]] - code - src/state_machine/state_machine.cpp
- [[on_init_complete()]] - code - src/state_machine/state_machine.cpp
- [[on_lrf_range()]] - code - src/state_machine/state_machine.cpp
- [[on_manual_reset()]] - code - src/state_machine/state_machine.cpp
- [[on_redetection_score()]] - code - src/state_machine/state_machine.cpp
- [[on_tracker_initialized()]] - code - src/state_machine/state_machine.cpp
- [[on_tracker_update()]] - code - src/state_machine/state_machine.cpp
- [[request_cancel()]] - code - src/state_machine/state_machine.cpp
- [[request_disarm()]] - code - src/state_machine/state_machine.cpp
- [[request_freecam()]] - code - src/state_machine/state_machine.cpp
- [[request_search()]] - code - src/state_machine/state_machine.cpp
- [[reset_target_validation()]] - code - src/state_machine/state_machine.cpp
- [[set_interlock_enabled()]] - code - src/state_machine/state_machine.cpp
- [[set_operator_authorization()]] - code - src/state_machine/state_machine.cpp
- [[state()]] - code - src/state_machine/state_machine.cpp
- [[state_machine.cpp]] - code - src/state_machine/state_machine.cpp
- [[tick()]] - code - src/state_machine/state_machine.cpp
- [[transition()]] - code - src/state_machine/state_machine.cpp
- [[update_lock_confirmation()]] - code - src/state_machine/state_machine.cpp
- [[update_position_history()]] - code - src/state_machine/state_machine.cpp
- [[update_prediction()]] - code - src/state_machine/state_machine.cpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_33
SORT file.name ASC
```

## Connections to other communities
- 3 edges to [[_COMMUNITY_Community 320]]
- 2 edges to [[_COMMUNITY_State Machine & Control Flow]]
- 2 edges to [[_COMMUNITY_Community 116]]
- 2 edges to [[_COMMUNITY_Community 83]]
- 1 edge to [[_COMMUNITY_Community 407]]
- 1 edge to [[_COMMUNITY_Community 493]]
- 1 edge to [[_COMMUNITY_Community 68]]
- 1 edge to [[_COMMUNITY_Community 82]]
- 1 edge to [[_COMMUNITY_Community 409]]
- 1 edge to [[_COMMUNITY_Community 223]]
- 1 edge to [[_COMMUNITY_Community 554]]

## Top bridge nodes
- [[state_machine.cpp]] - degree 38, connects to 2 communities
- [[fcs_state]] - degree 7, connects to 2 communities
- [[update_position_history()]] - degree 4, connects to 2 communities
- [[update_prediction()]] - degree 4, connects to 2 communities
- [[on_ballistics_solution()]] - degree 7, connects to 1 community