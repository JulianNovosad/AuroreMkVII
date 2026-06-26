---
type: community
cohesion: 0.18
members: 11
---

# Community 258

**Cohesion:** 0.18 - loosely connected
**Members:** 11 nodes

## Members
- [[DmaStats]] - code
- [[HeapStats]] - code
- [[ResourceStats]] - code
- [[StackStats]] - code
- [[ThermalStats]] - code
- [[ThrottleState]] - code
- [[check_growth_envelope()]] - code - src/test_infrastructure.cpp
- [[check_safety_threshold()]] - code - src/test_infrastructure.cpp
- [[check_throttle_state()]] - code - src/test_infrastructure.cpp
- [[get_stats()]] - code - src/test_infrastructure.cpp
- [[is_exhausted()]] - code - src/test_infrastructure.cpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_258
SORT file.name ASC
```

## Connections to other communities
- 5 edges to [[_COMMUNITY_Community 5 (test_infrastructure.cpp)]]
- 1 edge to [[_COMMUNITY_Community 225]]

## Top bridge nodes
- [[get_stats()]] - degree 10, connects to 1 community
- [[check_throttle_state()]] - degree 3, connects to 1 community
- [[check_growth_envelope()]] - degree 2, connects to 1 community
- [[check_safety_threshold()]] - degree 2, connects to 1 community
- [[is_exhausted()]] - degree 2, connects to 1 community