# TODO

## UX improvements
- [x] Scrape button is finished before detail fetch is

## Logic
- [ ] Periodic auto-scrape + fit-check
  Plan: background `std::jthread` + `condition_variable`, wakes on interval or config change.
  Sequence: scrape jobs → fetch details (poll until done) → batch fitcheck.
  Config fields (in ConfigV2 + config_v2.json): `periodic_enabled`, `periodic_interval_minutes`, `periodic_run_fitcheck`.
  New files: `include/scheduler.h`, `src/scheduler.cpp`.
  Expose state via `GET /api/scheduler/status`, manual trigger via `POST /api/scheduler/trigger`.
  UI: enable toggle + interval input + last/next run display + "Run now" button.
  Prerequisite: main refactor (steps 8–9 below).
- [ ] Onboarding and general logic is currently heavy tech and swiss focussed
- [x] `fitcheck_progress` race — no `compare_exchange` guard like detail-fetch has; two concurrent
  `/api/fitcheck` calls clobber counters. Fix separately.
