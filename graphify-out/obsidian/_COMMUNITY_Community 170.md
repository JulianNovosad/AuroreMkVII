---
type: community
cohesion: 0.25
members: 15
---

# Community 170

**Cohesion:** 0.25 - loosely connected
**Members:** 15 nodes

## Members
- [[.setup_valid_transitions()]] - code - tests/python/test_state_machine_properties.py
- [[.test_all_states_can_enter_fault()]] - code - tests/python/test_state_machine_properties.py
- [[.test_armed_has_three_exits()]] - code - tests/python/test_state_machine_properties.py
- [[.test_boot_only_goes_to_idle_or_fault()]] - code - tests/python/test_state_machine_properties.py
- [[.test_fault_can_only_transition_to_boot_or_idle()]] - code - tests/python/test_state_machine_properties.py
- [[.test_fault_goes_to_boot_or_idle()]] - code - tests/python/test_state_machine_properties.py
- [[.test_freecam_goes_to_idle_search_or_fault()]] - code - tests/python/test_state_machine_properties.py
- [[.test_idle_goes_to_freecam_search_or_fault()]] - code - tests/python/test_state_machine_properties.py
- [[.test_no_jumps_over_states()]] - code - tests/python/test_state_machine_properties.py
- [[.test_no_self_transitions()_1]] - code - tests/python/test_state_machine_properties.py
- [[.test_search_goes_to_idle_tracking_or_fault()]] - code - tests/python/test_state_machine_properties.py
- [[.test_tracking_has_four_exits()]] - code - tests/python/test_state_machine_properties.py
- [[.test_valid_transition_count()]] - code - tests/python/test_state_machine_properties.py
- [[All 49 possible transitions 7 valid, 42 invalid.]] - rationale - tests/python/test_state_machine_properties.py
- [[TestStateTransitionsExhaustive]] - code - tests/python/test_state_machine_properties.py

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_170
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 440]]

## Top bridge nodes
- [[TestStateTransitionsExhaustive]] - degree 15, connects to 1 community