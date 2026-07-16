# TODO

## UX improvements
- [ ] DB View for mobile
- [ ] Overall simplify UI

## Logic
- [x] Periodic auto-scrape + fit-check (Available for testing now)
- [x] Onboarding should test API call before user fills out fields
- [x] Investigate bug .exe not working when downloading directly
- [ ] Restore single job?
- [ ] Onboarding questions not optimal, also tedious to fill out. Maybe voice input or pick questions / answers
- [ ] Onboarding and general logic is currently heavy tech and swiss focussed
- [ ] Bug: after scraping and fitcheck running in background through automode, all skipped jobs reset for some reason. Not reproducible yet. 
- [ ] Bug: When cleaning up No Go, number shown is wrong. 



## Performance
- [ ] `/api/jobs` sends every column of every job (~13 MB, 2386 rows). Return list columns only; add `/api/jobs/:id/detail` for `fit_reasoning`, `fit_summary`, `template_text`, fetched when a job is opened
- [x] `/api/jobs` includes `user_status='deleted'` rows (1766 of 2386) that are never rendered. Filter server-side, separate endpoint or query param for trash view
- [ ] No index on `jobs.user_status`
- [ ] Static files (html/css/js) are not gzipped — httplib only compresses `set_content` responses, not `set_mount_point` files. Minor; revisit if vendor bundles grow
- [ ] Pi WiFi drops after hours — check `iw dev wlan0 get power_save`, disable persistently. Also rule out 2.4 GHz band-steering; ethernet fixes it for good

## Ideas
- Data sources and more scraping
- Adding a general AI ask me questions logic to ask questions like, does A fit better than B, or maybe even adjust rating due to reason X.
- Reminder system and generally full pipeline from searching to apply overview, reminders and more
