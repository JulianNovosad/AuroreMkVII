---
type: community
cohesion: 0.20
members: 10
---

# Community 309

**Cohesion:** 0.20 - loosely connected
**Members:** 10 nodes

## Members
- [[.test_data_len_exceeds_max()]] - code - tests/python/test_telemetry_types.py
- [[.test_entry_size()]] - code - tests/python/test_telemetry_types.py
- [[.test_event_id_ranges()]] - code - tests/python/test_telemetry_types.py
- [[.test_hmac_size()_4]] - code - tests/python/test_telemetry_types.py
- [[.test_invalid_event_id_zero()]] - code - tests/python/test_telemetry_types.py
- [[.test_invalid_severity_too_high()]] - code - tests/python/test_telemetry_types.py
- [[.test_max_data_size()]] - code - tests/python/test_telemetry_types.py
- [[.test_valid_entry()]] - code - tests/python/test_telemetry_types.py
- [[BinaryLogEntry audit log entry with HMAC.]] - rationale - tests/python/test_telemetry_types.py
- [[TestBinaryLogEntry]] - code - tests/python/test_telemetry_types.py

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_309
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 272]]

## Top bridge nodes
- [[TestBinaryLogEntry]] - degree 10, connects to 1 community