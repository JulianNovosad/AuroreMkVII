---
type: community
cohesion: 0.18
members: 11
---

# Community 256

**Cohesion:** 0.18 - loosely connected
**Members:** 11 nodes

## Members
- [[TestResultAggregator]] - code - include/aurore/test_infrastructure.hpp
- [[TestTier]] - code
- [[TierResult]] - code
- [[get_tier_result]] - code - include/aurore/test_infrastructure.hpp
- [[get_tier_result()]] - code - src/test_infrastructure.cpp
- [[print_summary]] - code - include/aurore/test_infrastructure.hpp
- [[record_result]] - code - include/aurore/test_infrastructure.hpp
- [[record_result()]] - code - src/test_infrastructure.cpp
- [[reset_3]] - code - include/aurore/test_infrastructure.hpp
- [[should_block_merge]] - code - include/aurore/test_infrastructure.hpp
- [[tier_results_]] - code - include/aurore/test_infrastructure.hpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_256
SORT file.name ASC
```

## Connections to other communities
- 2 edges to [[_COMMUNITY_Community 5 (test_infrastructure.cpp)]]
- 1 edge to [[_COMMUNITY_Community 2 (main)]]
- 1 edge to [[_COMMUNITY_Community 6 (timestamps_)]]

## Top bridge nodes
- [[TestResultAggregator]] - degree 9, connects to 2 communities
- [[get_tier_result()]] - degree 3, connects to 1 community
- [[record_result()]] - degree 2, connects to 1 community