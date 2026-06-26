---
type: community
cohesion: 0.25
members: 19
---

# Community 112

**Cohesion:** 0.25 - loosely connected
**Members:** 19 nodes

## Members
- [[.build_modbus_response()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_corrupt_crc()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_crc_byte_order()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_deterministic_crc_validation()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_distance_50000mm_50m()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_empty_response()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_long_response()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_max_distance()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_minimal_valid_distance()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_response_reproducibility()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_short_response()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_valid_response()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_wrong_byte_count()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_wrong_function_code()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_wrong_slave_address()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.test_zero_distance()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[.validate_modbus_response()]] - code - tests/python/test_laser_rangefinder_deep.py
- [[LaserRangefinderreader_loop_modbus response validation.]] - rationale - tests/python/test_laser_rangefinder_deep.py
- [[TestModbusResponseParser]] - code - tests/python/test_laser_rangefinder_deep.py

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_112
SORT file.name ASC
```

## Connections to other communities
- 4 edges to [[_COMMUNITY_Community 191]]
- 1 edge to [[_COMMUNITY_Community 380]]

## Top bridge nodes
- [[TestModbusResponseParser]] - degree 20, connects to 2 communities
- [[.validate_modbus_response()]] - degree 15, connects to 1 community
- [[.build_modbus_response()]] - degree 14, connects to 1 community
- [[.test_crc_byte_order()]] - degree 3, connects to 1 community