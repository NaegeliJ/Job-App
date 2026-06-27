# TODO

## UX improvements
- [x] Scrape button is finished before detail fetch is
- [x] Cleanup button is bigger than the rest
- [x] detail fetch progress and scrape progress have different visible look

## Logic
- [] No-Go label can have higher points than weak
- [] Periodic auto-scrape + fit-check
  Plan: background `std::jthread` + `condition_variable`, wakes on interval or config change.
  Sequence: scrape jobs → fetch details (poll until done) → batch fitcheck.
  Config fields (in ConfigV2 + config_v2.json): `periodic_enabled`, `periodic_interval_minutes`, `periodic_run_fitcheck`.
  New files: `include/scheduler.h`, `src/scheduler.cpp`.
  Expose state via `GET /api/scheduler/status`, manual trigger via `POST /api/scheduler/trigger`.
  UI: enable toggle + interval input + last/next run display + "Run now" button.
  Prerequisite: main refactor.

## Misc
- [] Plan main refactor
