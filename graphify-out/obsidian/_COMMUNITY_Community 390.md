---
type: community
cohesion: 0.25
members: 8
---

# Community 390

**Cohesion:** 0.25 - loosely connected
**Members:** 8 nodes

## Members
- [[.test_cpu_usage_range()]] - code - tests/python/test_telemetry_types.py
- [[.test_frame_rate_range()]] - code - tests/python/test_telemetry_types.py
- [[.test_inf_frame_rate()]] - code - tests/python/test_telemetry_types.py
- [[.test_jitter_range()]] - code - tests/python/test_telemetry_types.py
- [[.test_nan_cpu_temp()]] - code - tests/python/test_telemetry_types.py
- [[.test_valid_health()]] - code - tests/python/test_telemetry_types.py
- [[SystemHealthDatais_valid system metrics validation.]] - rationale - tests/python/test_telemetry_types.py
- [[TestSystemHealthDataIsValid]] - code - tests/python/test_telemetry_types.py

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_390
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 272]]

## Top bridge nodes
- [[TestSystemHealthDataIsValid]] - degree 8, connects to 1 community