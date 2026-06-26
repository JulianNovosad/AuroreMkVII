---
type: community
cohesion: 0.13
members: 17
---

# Community 127

**Cohesion:** 0.13 - loosely connected
**Members:** 17 nodes

## Members
- [[.generate_health_report()]] - code - include/aurore/safety_monitor.hpp
- [[.get_avg_latency_ns()]] - code - include/aurore/safety_monitor.hpp
- [[.get_correlated_subsystem()]] - code - include/aurore/safety_monitor.hpp
- [[.is_stalled()]] - code - include/aurore/safety_monitor.hpp
- [[.record_frame_complete()]] - code - include/aurore/safety_monitor.hpp
- [[.record_latency()]] - code - include/aurore/safety_monitor.hpp
- [[.record_stage_latency()]] - code - include/aurore/safety_monitor.hpp
- [[.reset()]] - code - include/aurore/safety_monitor.hpp
- [[.reset_stage_stats()]] - code - include/aurore/safety_monitor.hpp
- [[PipelineStage]] - code
- [[StageLatencyStats]] - code - include/aurore/safety_monitor.hpp
- [[last_latency_ns]] - code - include/aurore/safety_monitor.hpp
- [[max_latency_ns]] - code - include/aurore/safety_monitor.hpp
- [[sample_count]] - code - include/aurore/safety_monitor.hpp
- [[stall_count]] - code - include/aurore/safety_monitor.hpp
- [[stall_threshold_ns]] - code - include/aurore/safety_monitor.hpp
- [[total_latency_ns]] - code - include/aurore/safety_monitor.hpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_127
SORT file.name ASC
```

## Connections to other communities
- 7 edges to [[_COMMUNITY_Community 9 (safetymonitor)]]
- 2 edges to [[_COMMUNITY_Community 2 (main)]]
- 1 edge to [[_COMMUNITY_Community 8 (main)]]

## Top bridge nodes
- [[StageLatencyStats]] - degree 13, connects to 2 communities
- [[.generate_health_report()]] - degree 4, connects to 2 communities
- [[.record_stage_latency()]] - degree 5, connects to 1 community
- [[.get_correlated_subsystem()]] - degree 2, connects to 1 community
- [[.record_frame_complete()]] - degree 2, connects to 1 community