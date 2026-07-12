# TODO

## UX improvements
- [ ] Full DB View / Overview

## Logic
- [x] Periodic auto-scrape + fit-check (Available for testing now)
- [x] Onboarding should test API call before user fills out fields
- [x] Investigate bug .exe not working when downloading directly
- [ ] Restore single job?
- [ ] Onboarding questions not optimal, also tedious to fill out. Maybe voice input or pick questions / answers
- [ ] Onboarding and general logic is currently heavy tech and swiss focussed
- [ ] Bug: after scraping and fitcheck running in background through automode, all skipped jobs reset for some reason. 



## Ideas
- Map view with jobs
- Data sources and more scraping
- Range radius / better location filtering
- Adding a general AI ask me questions logic to ask questions like, does A fit better than B, or maybe even adjust rating due to reason X.
- Reminder system and generally full pipeline from searching to apply overview, reminders and more

## Plan for application Tracker
### What

New page tracking all applications after "apply" click. Replaces Notion table. Big table: one row per application, inline editable.

### Data (main DB, jobs table, new nullable columns)

- application_status TEXT — fixed enum: waiting, first_interview, next_round, assessment, offer, declined, withdrawn, ghosted
- applied_at TEXT — ISO-8601 (2026-07-10), consistent with existing date columns
- last_reaction TEXT — single free-text field
- last_reaction_at TEXT

Migration via existing column_exists() + ALTER TABLE pattern (db.cpp:276-282). Notes: reuse existing notes column.

### Semantics

- Tracker membership = user_status='applied'. Pipeline stage = application_status. Two domains, no enum mixing.
- Dashboard "mark applied": sets user_status='applied', application_status='waiting', applied_at=today only if null (no clobber on re-toggle).
- Un-toggle applied allowed: row leaves tracker, application_status data survives, reappears on re-mark.
- Existing 13 applied jobs: applied_at stays null, user fills inline.

### Manual entries

- If one wants to add a job to the tracker, one shall do it via dasboard. This is connected to the tracker, and the fitcheck may give more insight aswell. 

### API

- Read: reuse GET /r_status='applied'client-side (same pattern as dashboard state.allJobs).
- Write: reuse POST /api/jobs/update — extend field whitelist (routes.cpp:85) wit
- New: manual-add endpoint only.

### Frontend

- tracker.html + tracker.js, sibling of onboarding.html, shares api.js/utils as ES modules. Back-button to dashboard.
- Table columns: title, company, location, status (dropdown), applied date, last reaction + date, notes.
- Inline edit: status saves on change, text fieldsOptimistic updates
- Sort/filter by status. Active applications top; declined/withdrawn/
- Row click: open job detail (link to dashboard with job selected).
- Dashboard header: "Applied (n)" button links to tracker.

### Build order

1. Migration: 4 col
2. Extend /api/jobs
3. tracker.html + tracker.js
4. Dashboard mark-applied logic (waiting + applied_at) + header
button
5. detail.js null guards for manual rows

### Deferred (v2+)

Event-log reaction history, reminders/follow-up nudges, auto-ghosted detection, Notion/C
