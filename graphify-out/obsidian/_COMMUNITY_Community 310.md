---
type: community
cohesion: 0.20
members: 10
---

# Community 310

**Cohesion:** 0.20 - loosely connected
**Members:** 10 nodes

## Members
- [[.test_actuation_events()]] - code - tests/python/test_telemetry_types.py
- [[.test_detection_events()]] - code - tests/python/test_telemetry_types.py
- [[.test_dual_stream_events()]] - code - tests/python/test_telemetry_types.py
- [[.test_hardware_events()]] - code - tests/python/test_telemetry_types.py
- [[.test_safety_events()]] - code - tests/python/test_telemetry_types.py
- [[.test_system_events()]] - code - tests/python/test_telemetry_types.py
- [[.test_total_event_count()]] - code - tests/python/test_telemetry_types.py
- [[.test_tracking_events()]] - code - tests/python/test_telemetry_types.py
- [[TelemetryEventId enum all event identifiers.]] - rationale - tests/python/test_telemetry_types.py
- [[TestTelemetryEventId]] - code - tests/python/test_telemetry_types.py

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_310
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 272]]

## Top bridge nodes
- [[TestTelemetryEventId]] - degree 10, connects to 1 community