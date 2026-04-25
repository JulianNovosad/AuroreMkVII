# Regression Test Policy

## Aurore MkVII - Regression Test Requirements

Every bug discovered in production or testing MUST result in a new regression test.

### Naming Convention

```
test_regression_{issue_id}_{short_description}.cpp
```

### Test Requirements

1. Test MUST reproduce exact failure condition
2. Test name MUST include issue ID (GitHub issue number)
3. Test MUST be in `tests/unit/regression/`
4. Test deletion PROHIBITED without senior approval + issue closure

### Running Regression Tests

```bash
# Run all regression tests
ctest -L regression

# Run specific regression test
ctest -N | grep regression_XXX
```

### CI Enforcement

- Regression tests run on every commit
- Coverage regression blocks merge
- Regression test deletion blocks merge