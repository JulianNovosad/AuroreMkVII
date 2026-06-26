---
type: community
cohesion: 0.10
members: 21
---

# Community 100

**Cohesion:** 0.10 - loosely connected
**Members:** 21 nodes

## Members
- [[.test_absolute_time_prevents_drift_across_cycles()]] - code - tests/python/test_timing_deep.py
- [[.test_clock_nanosleep_timer_abstime_semantics()]] - code - tests/python/test_timing_deep.py
- [[.test_consecutive_misses_increment()]] - code - tests/python/test_timing_deep.py
- [[.test_consecutive_misses_reset_on_success()]] - code - tests/python/test_timing_deep.py
- [[.test_cycle_count_increments_on_success()]] - code - tests/python/test_timing_deep.py
- [[.test_deadline_miss_count_accumulates()]] - code - tests/python/test_timing_deep.py
- [[.test_jitter_calculation_negative()]] - code - tests/python/test_timing_deep.py
- [[.test_jitter_calculation_positive()]] - code - tests/python/test_timing_deep.py
- [[.test_jitter_calculation_zero()]] - code - tests/python/test_timing_deep.py
- [[.test_missed_deadline_after_large_jitter_resyncs()]] - code - tests/python/test_timing_deep.py
- [[.test_missed_deadline_resets_on_subsequent_success()]] - code - tests/python/test_timing_deep.py
- [[.test_no_drift_accumulation()]] - code - tests/python/test_timing_deep.py
- [[.test_period_accumulation_matches_real_time()]] - code - tests/python/test_timing_deep.py
- [[.test_phase_offset_applied()]] - code - tests/python/test_timing_deep.py
- [[.test_phase_offsets_maintain_pipeline_order()]] - code - tests/python/test_timing_deep.py
- [[.test_wait_advances_by_one_period()]] - code - tests/python/test_timing_deep.py
- [[.test_wait_no_throw_on_normal_operation()]] - code - tests/python/test_timing_deep.py
- [[.test_wait_returns_false_on_deadline_miss()]] - code - tests/python/test_timing_deep.py
- [[.test_wait_returns_true_on_time()]] - code - tests/python/test_timing_deep.py
- [[TestThreadTimingSleep]] - code - tests/python/test_timing_deep.py
- [[ThreadTiming clock_nanosleep behavior, absolute time, period accumulation, drif]] - rationale - tests/python/test_timing_deep.py

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_100
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 99]]

## Top bridge nodes
- [[TestThreadTimingSleep]] - degree 21, connects to 1 community