---
source_file: "src/safety/interlock_controller.cpp"
type: "code"
community: "Community 120"
location: "L191"
tags:
  - graphify/code
  - graphify/EXTRACTED
  - community/Community_120
---

# run_self_test()

## Connections
- [[.get_state()]] - `calls` [INFERRED]
- [[SelfTestResult]] - `references` [EXTRACTED]
- [[force_state()]] - `calls` [EXTRACTED]
- [[get_timestamp()]] - `calls` [INFERRED]
- [[interlock_controller.cpp]] - `contains` [EXTRACTED]
- [[monitor_thread_func()]] - `calls` [EXTRACTED]
- [[watchdog_feed()]] - `calls` [EXTRACTED]

#graphify/code #graphify/EXTRACTED #community/Community_120