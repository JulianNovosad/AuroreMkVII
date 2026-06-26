---
type: community
cohesion: 0.25
members: 8
---

# Community 360

**Cohesion:** 0.25 - loosely connected
**Members:** 8 nodes

## Members
- [[AuroreLinkConfig]] - code - include/aurore/aurore_link_server.hpp
- [[command_port]] - code - include/aurore/aurore_link_server.hpp
- [[ethernet_interface]] - code - include/aurore/aurore_link_server.hpp
- [[hmac_key]] - code - include/aurore/aurore_link_server.hpp
- [[max_clients]] - code - include/aurore/aurore_link_server.hpp
- [[session_timeout_s]] - code - include/aurore/aurore_link_server.hpp
- [[telemetry_port]] - code - include/aurore/aurore_link_server.hpp
- [[video_port]] - code - include/aurore/aurore_link_server.hpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_360
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 19 (reserved)]]
- 1 edge to [[_COMMUNITY_Community 8 (main)]]
- 1 edge to [[_COMMUNITY_Community 10 (aurorelinkserver)]]
- 1 edge to [[_COMMUNITY_Community 48]]

## Top bridge nodes
- [[AuroreLinkConfig]] - degree 11, connects to 4 communities