---
name: automation-testing
display_name: Automation Test Running
description: Enumerate and execute Unreal Automation Tests (unit, functional, Revvy's
  own plugin tests) in-editor and read per-test pass/fail back as JSON. Use when the
  user asks to "run tests", "verify", "self-check", or after editing plugin/editor
  code that has test coverage.
unreal_classes:
  - FAutomationTestFramework
keywords:
  - test
  - tests
  - run tests
  - automation
  - unit test
  - functional test
  - verify
  - self-check
  - regression
  - pass
  - fail
related_skills:
  - pie-testing
---

# Automation Test Running Skill

`run_automation_tests` drives the same test registry Session Frontend shows, asynchronously (the editor never freezes; latent tests get real frames).

## The loop

```text
1. run_automation_tests action=list, filter=Revvy.        # discover (substring, case-insensitive)
2. run_automation_tests action=run,  filter=Revvy.Tools.  # filter is REQUIRED for run
3. run_automation_tests action=status                     # poll until status=idle
4. run_automation_tests action=last_result                # per-test verdicts
```

`last_result` entries: `{name, full_path, state: passed|failed|skipped|timeout, duration_seconds, errors[], warnings[]}` plus `counts` and an `all_passed` flag.

## Filters worth knowing

| Filter | Covers |
|--------|--------|
| `Revvy.` | Every Revvy plugin self-test (graph readback, CDO audit, checkpoints, idempotency, tool surface) |
| `Revvy.Tools.` | Contract tests for the MCP tool surface itself |
| `Project.Functional Tests.` | The project's functional test maps (start their own PIE; raise `timeout_seconds`) |

## Guards you will hit (by design)

- `MISSING_FILTER` — `run` without a filter is refused; running every registered test is never intended.
- `TOO_MANY_TESTS` — filter matched more than `max_tests` (default 50). Narrow it or raise the cap deliberately.
- `PIE_ACTIVE` — stop the play session first (`pie_control action=stop`); tests that need PIE start their own.
- `FRAMEWORK_BUSY` / `TESTS_ALREADY_RUNNING` — one run at a time; poll `status`.
- Non-editor-context and RequiresUser tests are recorded as `skipped`, not run.

## Gotchas

- **After editing C++**, the running editor still hosts the OLD binary — `build_project` + Live Coding (or an editor restart) must land the change before tests can exercise it.
- **Timeouts end the whole run** (a hung latent chain cannot be resumed safely): per-test default 120s, total default 900s. Raise `timeout_seconds` for functional-test maps.
- **cancel stops after the current test** and keeps partial results in `last_result`.
- Save your work before broad filters — tests may create transient objects or open PIE.
