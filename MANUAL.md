# Job Radar — Manual

## Onboarding

Runs automatically on first launch. Nine questions build your `user_profile.md`:

1. CV — drop a PDF (text-selectable only) or paste plain text
2. Career goal (3–5 years)
3. What drives you
4. Hard no-gos
5. Tech you want to build vs. tolerate
6. Preferred company type and region
7. Hard constraints (salary floor, language, etc.)
8. Work style
9. Anything else the LLM should know

Profile is editable any time via **Profile** (person icon in header). Changes take effect on the next fit-check.

---

## First-time setup

This system uses LLMs, which needs proper instructions and thus, some tuning. 

1. Scrape a small batch (1 query, `rows` set to 5–10).
2. Fit-check those jobs. Read the reasoning.
3. If scores feel off — adjust your profile or LLM settings, clear fit data, recheck.
4. Once results are solid, the system is fully ready. 

---

## Action bar (header)

| Button | What it does |
|--------|--------------|
| **Scrape** | Fetches new listings from jobs.ch per your queries, then auto-fetches full posting text |
| **Fit-Check** | Scores every unscored job via LLM. Progress bar fills left→right. Fatal errors (bad key, no credits, rate limit) stop the batch and turn the button red — fix in Settings, then retry |
| **Add Job manually** | Paste any job posting text. App extracts fields and fit-checks automatically |
| **⊞ Filter** | Filter list by fit label or status: All / Strong / Decent / Exp / Weak / New / Starred / Applied |
| **⇅ Score** | Toggle sort: fit score (default) ↔ date scraped |
| **🗑 Cleanup** | Bulk soft-delete: all Skipped, all No Go, or unseen jobs older than 30 days |

Search bar (top center) filters by title and company in real time.

---

## Job detail

Click any job to open the detail panel.

**Fit results**

| Field | Meaning |
|-------|---------|
| Label | Strong / Decent / Experimental / Weak / No Go |
| Score | Weighted 0–100 |
| Summary | One-line verdict |
| Reasoning | Full dimension-by-dimension breakdown |

**Actions**

| Control | Behavior |
|---------|----------|
| ✦ Star | Mark as interested |
| ✓ Applied | Mark as applied |
| ✕ Skip | Dismiss |
| ★ Rating | 1–5 stars, saved immediately |
| Notes | Free text, saved on blur |
| **Recheck** | Re-runs fit-check for this job. Red border = last run failed |
| **Expired** | Marks job as expired and hides it |

Clicking a status button a second time toggles it off.

---

## Profile

Open via the person icon in the header. Plain Markdown — write exactly what you want the LLM to know about you. Save with the **Save** button. No restart needed.

---

## Settings

Open via the gear icon. Two sections, both hot-reload (no restart):

**AI provider**

| Field | Notes |
|-------|-------|
| Provider | `ollama_local` · `ollama_cloud` · `openrouter` · `deepinfra` · `mistral` · `custom` |
| Endpoint | Full HTTP URL to the chat completions endpoint |
| Model | Model identifier as the provider expects it |
| API key | Leave empty for local Ollama |

**Scrape & fit-check**

| Field | Notes |
|-------|-------|
| Queries | One search string per line, sent to jobs.ch |
| Rows | Max listings fetched per query |
| Limit | Max jobs per batch fit-check call |
| Max tokens | Cap on LLM response length |
| Temperature / top_p / top_k | Sampling parameters |

---

## Admin console

`Ctrl+\` toggles the console. All commands accept partial job IDs (last 8 characters). Type `help` for the full reference.

**Delete / restore**

| Command | Effect |
|---------|--------|
| `delete:job <id>` | Soft-delete one job (hidden, restorable) |
| `delete:status <status>` | Soft-delete all jobs with given status |
| `delete:label <label>` | Soft-delete all with given fit label |
| `restore:all` | Restore all soft-deleted jobs to unseen |
| `purge:job <id>` | Permanent hard-delete of one job |
| `purge:label <label>` | Permanent hard-delete of all with given fit label |

**Fit-check**

| Command | Effect |
|---------|--------|
| `fitcheck:clear <id>` | Clear fit data for one job |
| `fitcheck:clear-all` | Clear fit data for all jobs |
| `fitcheck:recheck <id>` | Clear + recheck one job |
| `fitcheck:recheck-all` | Clear all fit data and queue full batch recheck |

---

## Keyboard shortcuts

| Shortcut             | Action |
|----------------------|--------|
| `Ctrl+\` (US layout) | Toggle admin console |
| `Ctrl+K`             | Focus search bar |
| `Escape`             | Close open modal / dropdown |
