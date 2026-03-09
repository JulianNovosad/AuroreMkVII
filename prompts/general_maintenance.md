
---

# Full Codebase Maintenance & Spec Conformance Round

You are now running a **full codebase audit + conformance + hardening round**.

Follow this exact workflow:

---

## 1. Read the Specification

* Read `spec.md` (or the entire `specs/` directory if multiple files exist).

---

## 2. Phase 1 – Recon / Audit Batch (Read-Only)

**Goal:** Identify gaps and issues without making edits.

* Create a **batch of parallel sub-agents** (aim for 3).
* Assign each sub-agent **one focused slice** of the spec, e.g.:

  * One major feature
  * One subsystem/module
  * One non-functional requirement (performance, security, portability…)
  * One group of related edge cases
  * One interface/contract
  * One cross-cutting concern (error handling, logging, observability…)

**Audit sub-agent responsibilities:**

1. Read relevant parts of the **codebase** needed for its slice.
2. Classify current implementation status:

   * fully implemented
   * partially implemented
   * missing
   * broken
   * deprecated
   * unclear / ambiguous
3. List **concrete gaps/issues**, including (but not limited to):

   * missing functionality
   * spec mismatches
   * absent error handling
   * missing edge-case coverage
   * performance concerns
   * security vulnerabilities
   * portability issues (especially laptop vs Raspberry Pi 5)
   * maintainability / readability problems
4. Estimate **which files** would likely need to change.

**Return format (structured):**

```text
Feature/Slice: ____________________
Status: fully / partial / missing / broken / deprecated / unclear
Gaps / Issues:
- …
- …
Suspected files to change:
- …
- …
```

---

## 3. Coordinator Synthesis (Main Agent Only)

* Collect and synthesize full audit report.
* Deduplicate overlapping findings.
* Prioritize gaps (severity + dependency order).
* Partition remaining work into **three independent implementation tasks** (target three parallel items).

---

## 4. Phase 2 – Implementation & Test-Hardening Batch

* Dispatch a second wave of **three parallel implementation sub-agents**.
* Each agent gets **exactly one atomic, independent task**.

**Allowed actions (within assigned scope only):**

* Create/edit files
* Add/fix tests
* Fix logic
* Add guards/validation
* Narrow refactorings

**Mandatory rules for implementation agents:**

* Run relevant build/test/lint commands **after every non-trivial change**.
* **Never** merge or commit — only prepare clean changes.

**Return format:**

```text
Task: ______________________________
Files changed: …
Diff summary: …
Tests added/run: …
Verification commands & output:
• command 1 → result
• command 2 → result
```

---

## 5. Final Coordinator Phase (Main Agent Only)

* Collect results from all implementation agents.
* Check for cross-task conflicts → dispatch conflict-resolution agents if needed.
* **Integrate all changes**.
* Run **full project build + full test suite** (pytest, ctest, make, cargo, etc.).
* If anything fails → loop: create fix agents → re-verify.

**Only when everything passes cleanly:**

* Write conventional commit messages (group changes logically).
* Perform the commits.
* Write a **final summary report** (Markdown) containing:

  * What was audited (major areas covered)
  * Major gaps that were closed
  * Tests added / coverage improvements
  * Final build & test suite status
  * Any remaining known issues or recommended follow-up work

---

## Hard Rules – You MUST Follow

* The **main coordinator agent (you)** **does NOT** perform file edits, writes, or commits.
  → All actual code changes **must** come from sub-agents.
* **Maximize parallelism:** create two agents as the task naturally supports (target 2 total across both phases for large codebases/specs).
* Do **not** let planning/discussion consume the whole session — move quickly into execution after initial setup.
* Respect **all** existing rules in `CLAUDE.md` (guards, build-before-commit, conventional commits, verification gate, etc.).

---

**Begin now:**

1. Read `spec.md` (or `specs/` directory).
2. Immediately start dispatching the **Phase 1 recon batch**.

---

