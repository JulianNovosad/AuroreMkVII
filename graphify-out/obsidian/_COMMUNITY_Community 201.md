---
type: community
cohesion: 0.15
members: 13
---

# Community 201

**Cohesion:** 0.15 - loosely connected
**Members:** 13 nodes

## Members
- [[.get_state()]] - code - include/aurore/interlock_controller.hpp
- [[.is_actuation_allowed()]] - code - include/aurore/interlock_controller.hpp
- [[InterlockState]] - code
- [[InterlockStatus]] - code - include/aurore/interlock_controller.hpp
- [[actuation_inhibited]] - code - include/aurore/interlock_controller.hpp
- [[fault_count]] - code - include/aurore/interlock_controller.hpp
- [[get_status()]] - code - src/safety/interlock_controller.cpp
- [[interlock_state_to_string()]] - code - include/aurore/interlock_controller.hpp
- [[last_change_ns]] - code - include/aurore/interlock_controller.hpp
- [[last_watchdog_feed_ns]] - code - include/aurore/interlock_controller.hpp
- [[state_1]] - code - include/aurore/interlock_controller.hpp
- [[transition_count]] - code - include/aurore/interlock_controller.hpp
- [[watchdog_feeds]] - code - include/aurore/interlock_controller.hpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_201
SORT file.name ASC
```

## Connections to other communities
- 3 edges to [[_COMMUNITY_Community 28]]
- 3 edges to [[_COMMUNITY_Community 120]]
- 2 edges to [[_COMMUNITY_Community 2 (main)]]

## Top bridge nodes
- [[InterlockState]] - degree 5, connects to 2 communities
- [[.get_state()]] - degree 3, connects to 2 communities
- [[InterlockStatus]] - degree 10, connects to 1 community
- [[get_status()]] - degree 3, connects to 1 community
- [[interlock_state_to_string()]] - degree 2, connects to 1 community