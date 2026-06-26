---
type: community
cohesion: 0.33
members: 6
---

# Community 459

**Cohesion:** 0.33 - loosely connected
**Members:** 6 nodes

## Members
- [[FaultTarget]] - code
- [[FaultType]] - code
- [[inject_fault()]] - code - src/test_infrastructure.cpp
- [[validate_degradation()]] - code - src/test_infrastructure.cpp
- [[validate_fail_safe()]] - code - src/test_infrastructure.cpp
- [[validate_isolation()]] - code - src/test_infrastructure.cpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_459
SORT file.name ASC
```

## Connections to other communities
- 4 edges to [[_COMMUNITY_Community 5 (test_infrastructure.cpp)]]

## Top bridge nodes
- [[inject_fault()]] - degree 3, connects to 1 community
- [[validate_degradation()]] - degree 2, connects to 1 community
- [[validate_fail_safe()]] - degree 2, connects to 1 community
- [[validate_isolation()]] - degree 2, connects to 1 community