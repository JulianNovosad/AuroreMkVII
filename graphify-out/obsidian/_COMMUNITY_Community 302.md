---
type: community
cohesion: 0.20
members: 10
---

# Community 302

**Cohesion:** 0.20 - loosely connected
**Members:** 10 nodes

## Members
- [[.test_discard_count_tracking()]] - code - tests/python/test_hud_socket_deep.py
- [[.test_fresh_messages_not_discarded()]] - code - tests/python/test_hud_socket_deep.py
- [[.test_message_at_boundary()]] - code - tests/python/test_hud_socket_deep.py
- [[.test_message_fresh_within_timeout()]] - code - tests/python/test_hud_socket_deep.py
- [[.test_message_in_future_is_fresh()]] - code - tests/python/test_hud_socket_deep.py
- [[.test_message_stale_exceeds_timeout()]] - code - tests/python/test_hud_socket_deep.py
- [[.test_timeout_ns_conversion()]] - code - tests/python/test_hud_socket_deep.py
- [[.test_timeout_zero_discards_all()]] - code - tests/python/test_hud_socket_deep.py
- [[PERF-008 Message timestamp validation — discard stale messages.]] - rationale - tests/python/test_hud_socket_deep.py
- [[TestMessageFreshness]] - code - tests/python/test_hud_socket_deep.py

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_302
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 189]]

## Top bridge nodes
- [[TestMessageFreshness]] - degree 10, connects to 1 community