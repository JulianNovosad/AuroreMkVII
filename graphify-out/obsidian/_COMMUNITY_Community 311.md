---
type: community
cohesion: 0.20
members: 10
---

# Community 311

**Cohesion:** 0.20 - loosely connected
**Members:** 10 nodes

## Members
- [[.test_backpressure_above_hwm()]] - code - tests/python/test_telemetry_writer_deep.py
- [[.test_backpressure_active_flag()]] - code - tests/python/test_telemetry_writer_deep.py
- [[.test_backpressure_at_exactly_hwm()]] - code - tests/python/test_telemetry_writer_deep.py
- [[.test_backpressure_clears_below_hwm()]] - code - tests/python/test_telemetry_writer_deep.py
- [[.test_high_water_mark_computation()]] - code - tests/python/test_telemetry_writer_deep.py
- [[.test_high_water_mark_never_decreases_2()]] - code - tests/python/test_telemetry_writer_deep.py
- [[.test_high_water_mark_tracking()]] - code - tests/python/test_telemetry_writer_deep.py
- [[.test_no_backpressure_below_hwm()]] - code - tests/python/test_telemetry_writer_deep.py
- [[High-water mark detection and backpressure state.]] - rationale - tests/python/test_telemetry_writer_deep.py
- [[TestQueueHighWaterMark]] - code - tests/python/test_telemetry_writer_deep.py

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_311
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 137]]

## Top bridge nodes
- [[TestQueueHighWaterMark]] - degree 10, connects to 1 community