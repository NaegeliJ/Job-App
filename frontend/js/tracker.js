import { GET_URL, UPDATE_URL } from './api.js';
import { escapeHtml } from './utils/formatting.js';
import { isClosedApplication } from './application-status.js';
import { initColumnResize } from './components/column-resize.js';

const STATUS_OPTIONS = [
  { value: 'waiting',         label: 'Waiting' },
  { value: 'first_interview', label: '1st Interview' },
  { value: 'next_round',      label: 'Next Round' },
  { value: 'assessment',      label: 'Assessment' },
  { value: 'offer',           label: 'Offer' },
  { value: 'declined',        label: 'Declined' },
  { value: 'withdrawn',       label: 'Withdrawn' },
  { value: 'ghosted',         label: 'Ghosted' }
];

let applications = [];
let statusFilter = 'all';
let sortMode = 'status';

function showToast(message, isError = false) {
  const toast = document.getElementById('toast');
  if (!toast) return;

  toast.textContent = message;
  toast.style.borderColor = isError ? 'rgba(248,113,113,0.35)' : 'rgba(96,165,250,0.3)';
  toast.style.color = isError ? 'var(--red)' : 'var(--accent)';
  toast.classList.add('show');

  setTimeout(() => toast.classList.remove('show'), 2000);
}

async function persistField(jobId, field, value) {
  try {
    const res = await fetch(UPDATE_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ job_id: jobId, [field]: value })
    });
    if (!res.ok) throw new Error('Request failed');
    showToast('Saved');
  } catch {
    showToast('Save failed', true);
  }
}

function matchesFilter(job) {
  if (statusFilter === 'all') return true;
  if (statusFilter === 'active') return !isClosedApplication(job);
  return job.application_status === statusFilter;
}

// Sort policy: furthest progress on top, closed applications last.
// Independent from STATUS_OPTIONS, which orders the dropdown chronologically.
const STATUS_SORT_ORDER = [
  'offer', 'assessment', 'next_round', 'first_interview', 'waiting',
  'declined', 'withdrawn', 'ghosted'
];

function statusRank(job) {
  const index = STATUS_SORT_ORDER.indexOf(job.application_status);
  return index === -1 ? STATUS_SORT_ORDER.length : index;
}

const SORT_COMPARATORS = {
  status: (a, b) =>
    statusRank(a) - statusRank(b) ||
    (b.last_reaction_at || '').localeCompare(a.last_reaction_at || ''),
  applied_newest: (a, b) => (b.applied_at || '').localeCompare(a.applied_at || ''),
  applied_oldest: (a, b) => (a.applied_at || '9999').localeCompare(b.applied_at || '9999'),
  reaction_newest: (a, b) => (b.last_reaction_at || '').localeCompare(a.last_reaction_at || ''),
  company: (a, b) => (a.company_name || '').localeCompare(b.company_name || '')
};

function sortApplications(jobs) {
  const compare = SORT_COMPARATORS[sortMode] || SORT_COMPARATORS.status;
  return [...jobs].sort(compare);
}

function buildStatusSelect(job) {
  const current = job.application_status || '';
  const options = STATUS_OPTIONS
    .map(o => `<option value="${o.value}"${o.value === current ? ' selected' : ''}>${o.label}</option>`)
    .join('');
  const placeholder = current ? '' : '<option value="" selected disabled>—</option>';
  return `
    <select class="cell-status status-${escapeHtml(current || 'none')}" data-field="application_status">
      ${placeholder}${options}
    </select>`;
}

function buildRowHtml(job) {
  const closed = isClosedApplication(job) ? ' row-closed' : '';
  return `
    <tr class="tracker-row${closed}" data-id="${escapeHtml(job.job_id)}">
      <td>${buildStatusSelect(job)}</td>
      <td class="cell-title">
        <a href="/?job=${encodeURIComponent(job.job_id)}" title="Open in dashboard">${escapeHtml(job.title || 'Unknown')}</a>
      </td>
      <td class="cell-company">${escapeHtml(job.company_name || '—')}</td>
      <td><input type="text" class="cell-text cell-location" data-field="place" maxlength="200" placeholder="—" value="${escapeHtml(job.place || '')}"></td>
      <td><input type="date" class="cell-date" data-field="applied_at" value="${escapeHtml(job.applied_at || '')}"></td>
      <td><input type="text" class="cell-text" data-field="last_reaction" maxlength="500" placeholder="—" value="${escapeHtml(job.last_reaction || '')}"></td>
      <td><input type="date" class="cell-date" data-field="last_reaction_at" value="${escapeHtml(job.last_reaction_at || '')}"></td>
      <td><textarea class="cell-text cell-notes" data-field="notes" rows="1" placeholder="—">${escapeHtml(job.notes || '')}</textarea></td>
    </tr>`;
}

function render() {
  const body = document.getElementById('tracker-body');
  const count = document.getElementById('tracker-count');

  const visible = sortApplications(applications.filter(matchesFilter));
  if (count) count.textContent = `${visible.length} / ${applications.length}`;

  if (visible.length === 0) {
    body.innerHTML = '<tr><td colspan="8" class="tracker-empty">No applications — mark jobs as Applied on the dashboard.</td></tr>';
    return;
  }

  body.innerHTML = visible.map(buildRowHtml).join('');
}

function growNotes(el) {
  el.style.height = 'auto';
  el.style.height = (el.scrollHeight + 2) + 'px'; // +2: box-sizing border-box, scrollHeight excludes the 1px borders
}

function onNotesFocusIn(e) {
  if (e.target.matches('.cell-notes')) growNotes(e.target);
}

function onNotesInput(e) {
  if (e.target.matches('.cell-notes')) growNotes(e.target);
}

function onNotesFocusOut(e) {
  if (e.target.matches('.cell-notes')) e.target.style.height = '';
}

function onFieldChange(e) {
  const input = e.target.closest('[data-field]');
  if (!input) return;

  const row = input.closest('.tracker-row');
  if (!row) return;

  const job = applications.find(j => j.job_id === row.dataset.id);
  if (!job) return;

  const field = input.dataset.field;
  const value = input.value;
  if (job[field] === value) return;

  job[field] = value;
  persistField(job.job_id, field, value);

  // keep value attribute in sync so [value=""] CSS (empty-date hiding) tracks edits
  if (input.type === 'date') input.setAttribute('value', value);

  if (field === 'application_status') render();
}

async function init() {
  try {
    const res = await fetch(GET_URL);
    const jobs = await res.json();
    applications = jobs.filter(j => j.user_status === 'applied');
    render();
  } catch (e) {
    console.error('Init error:', e);
    document.getElementById('tracker-body').innerHTML =
      '<tr><td colspan="8" class="tracker-empty" style="color:var(--red)">Connection failed</td></tr>';
  }
}

document.addEventListener('DOMContentLoaded', () => {
  init();

  initColumnResize(document.querySelector('.tracker-table'));

  const body = document.getElementById('tracker-body');
  body.addEventListener('change', onFieldChange);
  body.addEventListener('focusin', onNotesFocusIn);
  body.addEventListener('input', onNotesInput);
  body.addEventListener('focusout', onNotesFocusOut);

  const filter = document.getElementById('status-filter');
  if (filter) {
    filter.addEventListener('change', () => {
      statusFilter = filter.value;
      render();
    });
  }

  const sort = document.getElementById('sort-select');
  if (sort) {
    sort.addEventListener('change', () => {
      sortMode = sort.value;
      render();
    });
  }
});
