# AuroreMkVII - Development Workflow

## Overview

This document defines the development workflow for AuroreMkVII. It establishes standards for task management, code quality, version control, and release processes.

## Task Management

### Track-Based Development
Work is organized into **tracks** - high-level units of work such as features, bug fixes, or refactoring efforts.

- **Track Registry:** `conductor/tracks.md`
- **Track Plans:** `conductor/tracks/<track_id>/plan.md`
- **Track Specs:** `conductor/tracks/<track_id>/spec.md`

### Task Lifecycle
```
new → in_progress → review → completed
```

1. **New:** Track created, plan defined
2. **In Progress:** Active implementation
3. **Review:** Code review, testing
4. **Completed:** Merged, documented

## Code Quality Requirements

### Test Coverage
- **Required:** >80% line coverage
- **Critical paths:** >90% coverage
- **Measurement:** `gcov`/`lcov` for C++, Jest for JS

### Static Analysis
- **C++:** Zero high-severity clang-tidy warnings
- **JavaScript:** Zero eslint errors
- **Formatting:** All code passes clang-format/prettier

### Code Review Checklist
- [ ] Tests added/updated
- [ ] Static analysis passes
- [ ] Formatting applied
- [ ] Documentation updated
- [ ] Commit message follows convention

## Version Control

### Commit Messages
Use **Conventional Commits** format:
```
type(scope): short description (≤72 chars)

Body explaining WHY (not what). Include verification results.

Fixes: #123
```

**Types:**
- `feat` - New feature
- `fix` - Bug fix
- `refactor` - Code restructuring
- `test` - Test additions
- `docs` - Documentation
- `chore` - Build/config changes
- `perf` - Performance improvements

**Examples:**
```
feat(tracking): add KCF tracker implementation

Initial KCF tracker port from OpenCV contrib. 
WCET measured at 1.2ms on RPi5, within 5ms budget.

Tests: tests/tracking/kcf_test.cpp (8 tests pass)

fix(hud): correct ballistic pipper positioning

Pipper was offset by wrong scale factor. 
Changed mradScale calculation from 4px to 8px per mrad.

Tests: manual verification on mock server
```

### Branch Strategy
- **Main branch:** `main` - always deployable
- **Feature branches:** `feature/<description>`
- **Fix branches:** `fix/<description>`
- **No long-lived branches:** Merge within 1 week

### Git Notes
Task summaries are recorded using Git Notes:
```bash
git notes add -m "Track: AC-130 HUD Redesign - Completed frontend overhaul"
```

## Development Process

### Pre-Development
1. Read track plan: `conductor/tracks/<id>/plan.md`
2. Understand spec: `conductor/tracks/<id>/spec.md`
3. Create branch: `git checkout -b feature/<track-name>`

### Implementation
1. **Write tests first** (TDD for new features)
2. **Implement feature** (minimum viable change)
3. **Run static analysis** (`clang-tidy`, `eslint`)
4. **Format code** (`clang-format`, `prettier`)
5. **Run tests** (`ctest`, `npm test`)

### Pre-Commit Checklist
- [ ] Tests pass
- [ ] Static analysis clean
- [ ] Code formatted
- [ ] Commit message ready

### Commit Frequency
- **After each task:** Small, atomic commits
- **No WIP commits:** Squash before merge
- **No merge commits:** Use rebase

## Build and Test

### C++ (Native Build)
```bash
# Configure
mkdir build-native && cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build . -j$(nproc)

# Test
ctest --output-on-failure

# Coverage
cmake .. -DCMAKE_BUILD_TYPE=Debug -DAURORE_ENABLE_COVERAGE=ON
cmake --build .
ctest
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

### JavaScript (aurore-link)
```bash
cd aurore-link

# Install dependencies
npm install

# Lint
npm run lint

# Start mock server
npm start
```

## Release Process

### Version Numbering
Semantic versioning: `MAJOR.MINOR.PATCH`
- **MAJOR:** Breaking changes
- **MINOR:** New features (backwards compatible)
- **PATCH:** Bug fixes (backwards compatible)

### Release Checklist
- [ ] All tests pass
- [ ] Documentation updated
- [ ] CHANGELOG.md updated
- [ ] Version bumped in CMakeLists.txt
- [ ] Tagged release: `git tag -a v0.1.0 -m "Release v0.1.0"`

## Continuous Integration

### GitHub Actions
- **On push:** Build, test, lint
- **On PR:** Full CI pipeline
- **On tag:** Release build

### Required Checks
- Build succeeds (native and RPi cross-compile)
- All tests pass
- No new clang-tidy warnings
- Coverage ≥80%

## Documentation

### Code Comments
- **Why, not what:** Code explains logic
- **Public APIs:** Brief description
- **Complex algorithms:** Detailed explanation

### README Updates
- Update when behavior changes
- Include build/test instructions
- Document known issues

### API Documentation
- **C++:** Doxygen-style comments
- **JavaScript:** JSDoc for public functions

## Phase Completion Verification

### Protocol
At the end of each phase:
1. **Verify all tasks completed:** Check plan.md
2. **Run full test suite:** Confirm zero failures
3. **Update documentation:** Reflect current state
4. **Create checkpoint commit:** Tag phase completion

### Checkpoint Format
```
chore(phase): Complete Phase 1 - Project Scaffolding

All scaffolding tasks completed:
- conductor/ directory structure created
- product.md, product-guidelines.md, tech-stack.md written
- Code style guides copied
- Initial track generated

Tests: ctest (42 tests pass), npm run lint (0 errors)
```
