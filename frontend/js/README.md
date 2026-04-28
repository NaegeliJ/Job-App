# JavaScript Module Structure

> **⚠️ For up-to-date API references**, see `../../AGENTS.md`.

## Directory Structure

```
js/
├── api.js                    # API endpoints
├── state.js                  # Global application state
├── main.js                   # Entry point & initialization
├── utils/
│   ├── formatting.js         # escapeHtml(), fmtDate(), getStatusIcon()
│   └── confirm.js            # confirmDialog() — custom modal, replaces window.confirm()
└── components/
    ├── header.js             # Search, filters, stats
    ├── job-list.js           # Job list rendering & selection
    ├── detail.js             # Job detail panel
    ├── actions.js            # User actions & API calls
    ├── modal.js              # Settings modal
    └── console.js            # Dev console (Ctrl+\ toggle)
```

## Module Dependencies

| Module | Imports | Exports |
|---|---|---|
| `api.js` | — | API URLs (`GET_URL`, `UPDATE_URL`, `SCRAPE_URL`, `DETAILS_URL`, `CONFIG_URL`, `PROFILE_GET_URL`, `PROFILE_SAVE_URL`, `FITCHECK_URL`, `FITCHECK_PROGRESS_URL`, `IMPORT_TEXT_URL`, `VERSION_URL`) |
| `state.js` | — | `state` object |
| `utils/formatting.js` | — | `escapeHtml()`, `fmtDate()`, `getStatusIcon()` |
| `utils/confirm.js` | — | `confirmDialog(message)` → Promise\<boolean\> |
| `header.js` | `state`, `renderList` | `setConnectionStatus`, `onSearch`, `clearSearch`, `updateStats`, `setFilter`, `toggleSort` |
| `job-list.js` | `state`, `fmtDate`, `getStatusIcon`, `renderDetail` | `renderList`, `selectJob` |
| `detail.js` | `state`, `fmtDate`, `escapeHtml`, `setStatus`, `setExpired`, `saveNotes`, `setRating`, `showToast` | `renderDetail` |
| `actions.js` | `state`, API URLs, `renderDetail`, `renderList`, `updateStats`, `setConnectionStatus` | `setStatus`, `setExpired`, `setRating`, `hoverStar`, `unhoverStar`, `saveNotes`, `showToast`, `scrapeJobs`, `triggerFitCheck`, `openProfile`, `closeProfile`, `saveProfile`, `openOnboarding`, `importJobFromText`, `saveImportUrl`, `openImportModal`, `closeImportModal`, `bulkDeleteByStatus`, `bulkDeleteByFitLabel` |
| `modal.js` | `state`, config URLs, `showToast` | `openSettings`, `closeSettings`, `closeSettingsOnBg`, `saveSettings`, `renderConfigForm` |
| `console.js` | `state` | `initConsole`, `toggleConsole` |
| `main.js` | All component functions | `init()`, `bindEvents()` |

## Usage

```html
<script type="module" src="/js/main.js"></script>
```

`main.js` initializes on `DOMContentLoaded`, sets up keyboard shortcuts, and binds all UI events.

## State

```javascript
const state = {
  allJobs: [],
  currentFilter: 'all',
  currentJob: null,
  searchQuery: '',
  sortMode: 'score',
  _cfgRaw: null,
  _modalMousedownTarget: null,
  _profileModalMousedownTarget: null,
  _importModalMousedownTarget: null
};
```

Last verified: 2026-04-28
