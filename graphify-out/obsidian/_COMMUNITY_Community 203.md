---
type: community
cohesion: 0.15
members: 13
---

# Community 203

**Cohesion:** 0.15 - loosely connected
**Members:** 13 nodes

## Members
- [[QueueStressTest()]] - code - src/test_infrastructure.cpp
- [[TelemetryConfig]] - code - include/aurore/telemetry_writer.hpp
- [[backpressure_policy]] - code - include/aurore/telemetry_writer.hpp
- [[enable_console]] - code - include/aurore/telemetry_writer.hpp
- [[enable_csv]] - code - include/aurore/telemetry_writer.hpp
- [[enable_json]] - code - include/aurore/telemetry_writer.hpp
- [[hmac_key_2]] - code - include/aurore/telemetry_writer.hpp
- [[log_dir]] - code - include/aurore/telemetry_writer.hpp
- [[max_file_size_mb]] - code - include/aurore/telemetry_writer.hpp
- [[max_queue_size_1]] - code - include/aurore/telemetry_writer.hpp
- [[max_sessions]] - code - include/aurore/telemetry_writer.hpp
- [[queue_high_water_pct]] - code - include/aurore/telemetry_writer.hpp
- [[session_prefix]] - code - include/aurore/telemetry_writer.hpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_203
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 8 (main)]]
- 1 edge to [[_COMMUNITY_Community 2 (main)]]
- 1 edge to [[_COMMUNITY_Community 31]]
- 1 edge to [[_COMMUNITY_Community 160]]
- 1 edge to [[_COMMUNITY_Community 224]]
- 1 edge to [[_COMMUNITY_Community 5 (test_infrastructure.cpp)]]

## Top bridge nodes
- [[TelemetryConfig]] - degree 15, connects to 4 communities
- [[backpressure_policy]] - degree 3, connects to 1 community
- [[QueueStressTest()]] - degree 2, connects to 1 community