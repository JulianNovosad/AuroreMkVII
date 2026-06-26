---
type: community
cohesion: 0.05
members: 48
---

# Community 14 (gimbalcontroller)

**Cohesion:** 0.05 - loosely connected
**Members:** 48 nodes

## Members
- [[.check_and_clear_limit_violation()]] - code - include/aurore/gimbal_controller.hpp
- [[.current_az()]] - code - include/aurore/gimbal_controller.hpp
- [[.current_el()]] - code - include/aurore/gimbal_controller.hpp
- [[.has_sequence_gap()]] - code - include/aurore/gimbal_controller.hpp
- [[.last_sequence()]] - code - include/aurore/gimbal_controller.hpp
- [[.reset_angles_for_test()]] - code - include/aurore/gimbal_controller.hpp
- [[.reset_rate_limiter()]] - code - include/aurore/gimbal_controller.hpp
- [[.reset_sequence_gap()]] - code - include/aurore/gimbal_controller.hpp
- [[.set_source()]] - code - include/aurore/gimbal_controller.hpp
- [[.source()]] - code - include/aurore/gimbal_controller.hpp
- [[CameraIntrinsics]] - code - include/aurore/gimbal_controller.hpp
- [[GimbalCommand]] - code - include/aurore/gimbal_controller.hpp
- [[GimbalController]] - code - include/aurore/gimbal_controller.hpp
- [[GimbalController()]] - code - src/actuation/gimbal_controller.cpp
- [[GimbalSource]] - code
- [[apply_rate_limit]] - code - include/aurore/gimbal_controller.hpp
- [[apply_rate_limit()]] - code - src/actuation/gimbal_controller.cpp
- [[az_]] - code - include/aurore/gimbal_controller.hpp
- [[az_deg]] - code - include/aurore/gimbal_controller.hpp
- [[az_max_]] - code - include/aurore/gimbal_controller.hpp
- [[az_min_]] - code - include/aurore/gimbal_controller.hpp
- [[cam_]] - code - include/aurore/gimbal_controller.hpp
- [[command_absolute]] - code - include/aurore/gimbal_controller.hpp
- [[command_absolute()]] - code - src/actuation/gimbal_controller.cpp
- [[command_from_pixel]] - code - include/aurore/gimbal_controller.hpp
- [[command_from_pixel()]] - code - src/actuation/gimbal_controller.cpp
- [[cx]] - code - include/aurore/gimbal_controller.hpp
- [[cy]] - code - include/aurore/gimbal_controller.hpp
- [[el_]] - code - include/aurore/gimbal_controller.hpp
- [[el_deg]] - code - include/aurore/gimbal_controller.hpp
- [[el_max_]] - code - include/aurore/gimbal_controller.hpp
- [[el_min_]] - code - include/aurore/gimbal_controller.hpp
- [[focal_length_px]] - code - include/aurore/gimbal_controller.hpp
- [[gimbal_controller.cpp]] - code - src/actuation/gimbal_controller.cpp
- [[last_sequence_num_]] - code - include/aurore/gimbal_controller.hpp
- [[limit_violated_]] - code - include/aurore/gimbal_controller.hpp
- [[pair]] - code
- [[prev_az_cmd_]] - code - include/aurore/gimbal_controller.hpp
- [[prev_az_vel_]] - code - include/aurore/gimbal_controller.hpp
- [[prev_cmd_ns_]] - code - include/aurore/gimbal_controller.hpp
- [[prev_el_cmd_]] - code - include/aurore/gimbal_controller.hpp
- [[prev_el_vel_]] - code - include/aurore/gimbal_controller.hpp
- [[process_command_with_gap_check]] - code - include/aurore/gimbal_controller.hpp
- [[process_command_with_gap_check()]] - code - src/actuation/gimbal_controller.cpp
- [[sequence_gap_detected_]] - code - include/aurore/gimbal_controller.hpp
- [[sequence_num]] - code - include/aurore/gimbal_controller.hpp
- [[set_limits]] - code - include/aurore/gimbal_controller.hpp
- [[set_limits()]] - code - src/actuation/gimbal_controller.cpp

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_14_gimbalcontroller
SORT file.name ASC
```

## Connections to other communities
- 8 edges to [[_COMMUNITY_Community 8 (main)]]
- 1 edge to [[_COMMUNITY_Community 2 (main)]]
- 1 edge to [[_COMMUNITY_Community 116]]

## Top bridge nodes
- [[GimbalController]] - degree 35, connects to 2 communities
- [[GimbalCommand]] - degree 8, connects to 1 community
- [[apply_rate_limit()]] - degree 6, connects to 1 community
- [[CameraIntrinsics]] - degree 6, connects to 1 community
- [[command_absolute()]] - degree 4, connects to 1 community