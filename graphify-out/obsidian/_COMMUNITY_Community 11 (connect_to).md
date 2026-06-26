---
type: community
cohesion: 0.08
members: 50
---

# Community 11 (connect_to)

**Cohesion:** 0.08 - loosely connected
**Members:** 50 nodes

## Members
- [[.test_ballistic_p_hit_in_range()]] - code - tests/python/test_network_integration.py
- [[.test_gimbal_angles_in_range()]] - code - tests/python/test_network_integration.py
- [[.test_multiple_frames_have_monotonic_timestamps()]] - code - tests/python/test_network_integration.py
- [[.test_receive_telemetry_frame()]] - code - tests/python/test_network_integration.py
- [[.test_send_arm_command()]] - code - tests/python/test_network_integration.py
- [[.test_send_freecam_command()]] - code - tests/python/test_network_integration.py
- [[.test_send_mode_switch_auto()]] - code - tests/python/test_network_integration.py
- [[.test_send_mode_switch_freecam()]] - code - tests/python/test_network_integration.py
- [[.test_telemetry_fcs_state_valid()]] - code - tests/python/test_network_integration.py
- [[.test_telemetry_has_required_fields()]] - code - tests/python/test_network_integration.py
- [[.test_telemetry_rate()]] - code - tests/python/test_network_integration.py
- [[.test_track_state_valid_when_active()]] - code - tests/python/test_network_integration.py
- [[MjpegStreamer()]] - code - src/network/mjpeg_streamer.cpp
- [[Network Integration Tests  Connects to a running aurore binary via TCP and valid]] - rationale - tests/python/test_network_integration.py
- [[Pred]] - code
- [[TestCommandInterface]] - code - tests/python/test_network_integration.py
- [[TestTelemetryStream]] - code - tests/python/test_network_integration.py
- [[accept_loop()_2]] - code - src/network/mjpeg_streamer.cpp
- [[aurore_link_test.cpp]] - code - tests/unit/aurore_link_test.cpp
- [[broadcast()_1]] - code - src/network/mjpeg_streamer.cpp
- [[command_socket()]] - code - tests/python/test_network_integration.py
- [[connect_to()]] - code - tests/unit/aurore_link_test.cpp
- [[connect_to()_1]] - code - tests/unit/test_emergency_inhibit.cpp
- [[encode_loop()]] - code - src/network/mjpeg_streamer.cpp
- [[has_clients()]] - code - src/network/mjpeg_streamer.cpp
- [[main()_13]] - code - tests/unit/aurore_link_test.cpp
- [[main()_52]] - code - tests/unit/test_emergency_inhibit.cpp
- [[mjpeg_streamer.cpp]] - code - src/network/mjpeg_streamer.cpp
- [[push_frame()]] - code - src/network/mjpeg_streamer.cpp
- [[recv_exact()]] - code - tests/python/test_network_integration.py
- [[remove_client()]] - code - src/network/mjpeg_streamer.cpp
- [[socket]] - code
- [[start()_6]] - code - src/network/mjpeg_streamer.cpp
- [[stop()_7]] - code - src/network/mjpeg_streamer.cpp
- [[telemetry_socket()]] - code - tests/python/test_network_integration.py
- [[test_emergency_inhibit.cpp]] - code - tests/unit/test_emergency_inhibit.cpp
- [[test_emergency_stop_callback_fires()]] - code - tests/unit/aurore_link_test.cpp
- [[test_emergency_stop_callback_fires()_1]] - code - tests/unit/test_emergency_inhibit.cpp
- [[test_emergency_stop_message_id()]] - code - tests/unit/test_emergency_inhibit.cpp
- [[test_emergency_stop_no_auth_required()]] - code - tests/unit/aurore_link_test.cpp
- [[test_emergency_stop_no_auth_required()_1]] - code - tests/unit/test_emergency_inhibit.cpp
- [[test_freecam_callback_fires_on_command()]] - code - tests/unit/aurore_link_test.cpp
- [[test_heartbeat_resets_timeout()]] - code - tests/unit/aurore_link_test.cpp
- [[test_heartbeat_timeout_callback_fires()]] - code - tests/unit/aurore_link_test.cpp
- [[test_mode_callback_fires_on_command()]] - code - tests/unit/aurore_link_test.cpp
- [[test_network_integration.py]] - code - tests/python/test_network_integration.py
- [[test_server_starts_and_stops()]] - code - tests/unit/aurore_link_test.cpp
- [[test_telemetry_client_receives_broadcast()]] - code - tests/unit/aurore_link_test.cpp
- [[uchar]] - code
- [[wait_for()]] - code - tests/unit/aurore_link_test.cpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_11_connect_to
SORT file.name ASC
```

## Connections to other communities
- 3 edges to [[_COMMUNITY_Community 2 (main)]]
- 2 edges to [[_COMMUNITY_Community 116]]
- 1 edge to [[_COMMUNITY_Community 8 (main)]]
- 1 edge to [[_COMMUNITY_Community 83]]
- 1 edge to [[_COMMUNITY_Community 177]]
- 1 edge to [[_COMMUNITY_Community 204]]
- 1 edge to [[_COMMUNITY_Community 48]]
- 1 edge to [[_COMMUNITY_Community 25]]
- 1 edge to [[_COMMUNITY_Community 4 (.test_loads_valid_json)]]
- 1 edge to [[_COMMUNITY_Community 402]]

## Top bridge nodes
- [[socket]] - degree 27, connects to 7 communities
- [[aurore_link_test.cpp]] - degree 13, connects to 1 community
- [[test_emergency_inhibit.cpp]] - degree 7, connects to 1 community
- [[broadcast()_1]] - degree 5, connects to 1 community
- [[MjpegStreamer()]] - degree 3, connects to 1 community