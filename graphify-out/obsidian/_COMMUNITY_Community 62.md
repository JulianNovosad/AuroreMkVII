---
type: community
cohesion: 0.07
members: 29
---

# Community 62

**Cohesion:** 0.07 - loosely connected
**Members:** 29 nodes

## Members
- [[.test_binary_entry_data_payload()]] - code - tests/python/test_security_deep.py
- [[.test_binary_entry_fixed_fields()]] - code - tests/python/test_security_deep.py
- [[.test_binary_entry_hmac_field()]] - code - tests/python/test_security_deep.py
- [[.test_binary_entry_layout_data_len()]] - code - tests/python/test_security_deep.py
- [[.test_binary_entry_layout_event_id()]] - code - tests/python/test_security_deep.py
- [[.test_binary_entry_layout_severity()]] - code - tests/python/test_security_deep.py
- [[.test_binary_entry_layout_timestamp()]] - code - tests/python/test_security_deep.py
- [[.test_binary_entry_size_constant()]] - code - tests/python/test_security_deep.py
- [[.test_binary_entry_struct_size_verification()]] - code - tests/python/test_security_deep.py
- [[.test_critical_event_hmac_strict()]] - code - tests/python/test_security_deep.py
- [[.test_different_keys_produce_different_signatures()]] - code - tests/python/test_security_deep.py
- [[.test_entry_with_max_data()]] - code - tests/python/test_security_deep.py
- [[.test_entry_with_zero_data()]] - code - tests/python/test_security_deep.py
- [[.test_event_id_ranges_valid()]] - code - tests/python/test_security_deep.py
- [[.test_full_binary_log_entry_format()]] - code - tests/python/test_security_deep.py
- [[.test_high_volume_hmac_verification()]] - code - tests/python/test_security_deep.py
- [[.test_hmac_signing_of_log_entry()]] - code - tests/python/test_security_deep.py
- [[.test_hmac_size_constant()]] - code - tests/python/test_security_deep.py
- [[.test_hmac_tampered_entry_detected()]] - code - tests/python/test_security_deep.py
- [[.test_log_entry_serialization_deserialization()]] - code - tests/python/test_security_deep.py
- [[.test_max_data_size_constant()]] - code - tests/python/test_security_deep.py
- [[.test_minimal_log_entry()]] - code - tests/python/test_security_deep.py
- [[.test_multiple_event_ids_logged()]] - code - tests/python/test_security_deep.py
- [[.test_severity_levels_in_entries()]] - code - tests/python/test_security_deep.py
- [[.test_severity_name_mapping()]] - code - tests/python/test_security_deep.py
- [[.test_telemetry_config_defaults()]] - code - tests/python/test_security_deep.py
- [[.test_telemetry_event_count()]] - code - tests/python/test_security_deep.py
- [[TelemetryWriter binary log entry format, severity, HMAC signing.]] - rationale - tests/python/test_security_deep.py
- [[TestTelemetryWriterSecurity]] - code - tests/python/test_security_deep.py

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Community_62
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Community 1 (.__init__)]]

## Top bridge nodes
- [[TestTelemetryWriterSecurity]] - degree 29, connects to 1 community