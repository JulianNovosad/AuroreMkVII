---
type: community
cohesion: 0.25
members: 8
---

# Community 391

**Cohesion:** 0.25 - loosely connected
**Members:** 8 nodes

## Members
- [[.test_block()]] - code - tests/python/test_telemetry_types.py
- [[.test_drop_newest()]] - code - tests/python/test_telemetry_types.py
- [[.test_drop_oldest()]] - code - tests/python/test_telemetry_types.py
- [[.test_drop_oldest_not_block()]] - code - tests/python/test_telemetry_types.py
- [[.test_policy_names()]] - code - tests/python/test_telemetry_types.py
- [[.test_valid_policy_range()]] - code - tests/python/test_telemetry_types.py
- [[BackpressurePolicy enum SEC-010 drop policies.]] - rationale - tests/python/test_telemetry_types.py
- [[TestBackpressurePolicy]] - code - tests/python/test_telemetry_types.py

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_391
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 272]]

## Top bridge nodes
- [[TestBackpressurePolicy]] - degree 8, connects to 1 community