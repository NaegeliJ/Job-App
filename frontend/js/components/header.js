import state from '../state.js';
import { renderList } from './job-list.js';
import { isClosedApplication } from '../application-status.js';

// ============================================================================
// Connection Status
// ============================================================================

export function setConnectionStatus(status) {
  const dot = document.getElementById('status-dot');
  const label = document.getElementById('status-label');
  
  if (!dot || !label) return;
  
  const statusMap = {
    connected: { className: 'status-dot connected', text: 'Live' },
    error: { className: 'status-dot error', text: 'Offline' },
    loading: { className: 'status-dot', text: '...' }
  };
  
  const config = statusMap[status] || statusMap.loading;
  dot.className = config.className;
  label.textContent = config.text;
}

// ============================================================================
// Search
// ============================================================================

export function onSearch() {
  const searchInput = document.getElementById('search-input');
  const clearButton = document.getElementById('search-clear');
  
  if (!searchInput) return;
  
  state.searchQuery = searchInput.value.toLowerCase();
  
  if (clearButton) {
    clearButton.style.display = state.searchQuery ? 'block' : 'none';
  }
  
  renderList();
}

export function clearSearch() {
  const searchInput = document.getElementById('search-input');
  const clearButton = document.getElementById('search-clear');
  
  if (searchInput) searchInput.value = '';
  if (clearButton) clearButton.style.display = 'none';
  
  state.searchQuery = '';
  renderList();
}

// ============================================================================
// Stats Update
// ============================================================================

function countByFitLabel(jobs, label) {
  return jobs.filter(job => job.fit_label === label).length;
}

function countByUserStatus(jobs, status) {
  if (status === 'unseen') {
    return jobs.filter(job => !job.user_status || job.user_status === 'unseen').length;
  }
  return jobs.filter(job => job.user_status === status).length;
}

function countWeakJobs(jobs) {
  return jobs.filter(job => job.fit_label === 'Weak' || job.fit_label === 'No Go').length;
}

export function updateStats() {
  // Same visibility rule as the job list: closed applications only count in the tracker
  const jobs = state.allJobs.filter(j => j.user_status !== 'deleted' && !isClosedApplication(j));
  const total = jobs.length;
  
  // Fit verdict counts
  const counts = {
    strong: countByFitLabel(jobs, 'Strong'),
    decent: countByFitLabel(jobs, 'Decent'),
    experimental: countByFitLabel(jobs, 'Experimental'),
    weak: countWeakJobs(jobs),
    unseen: countByUserStatus(jobs, 'unseen'),
    interested: countByUserStatus(jobs, 'interested'),
    applied: countByUserStatus(jobs, 'applied')
  };
  
  // Update filter buttons
  const buttons = {
    'filter-all': `All (${total})`,
    'filter-strong': `Strong (${counts.strong})`,
    'filter-decent': `Decent (${counts.decent})`,
    'filter-experimental': `Exp (${counts.experimental})`,
    'filter-weak': `Weak (${counts.weak})`,
    'filter-unseen': `New (${counts.unseen})`,
    'filter-interested': `Starred (${counts.interested})`,
    'filter-applied': `Applied (${counts.applied})`
  };
  
  Object.entries(buttons).forEach(([id, text]) => {
    const btn = document.getElementById(id);
    if (btn) btn.textContent = text;
  });

  // Tracker keeps closed applications, so its count includes them (unlike counts.applied)
  const allApplications = state.allJobs.filter(j => j.user_status === 'applied').length;
  const trackerBtn = document.getElementById('tracker-btn');
  if (trackerBtn) trackerBtn.textContent = `📋 Applied (${allApplications})`;
}

// ============================================================================
// Filter & Sort
// ============================================================================

const filterLabels = {
  all: 'ALL', strong: 'STRONG', decent: 'DECENT',
  experimental: 'EXP', weak: 'WEAK', unseen: 'NEW',
  interested: 'STARRED', applied: 'APPLIED'
};

export function setFilter(button, filterName) {
  state.currentFilter = filterName;

  document.querySelectorAll('.filter-btn').forEach(btn => btn.classList.remove('active'));
  button.classList.add('active');

  const dropdownBtn = document.getElementById('filter-dropdown-btn');
  if (dropdownBtn) {
    dropdownBtn.textContent = `⊞ ${filterLabels[filterName] ?? filterName.toUpperCase()}`;
    dropdownBtn.setAttribute('aria-expanded', 'false');
  }

  const menu = document.getElementById('filter-dropdown-menu');
  if (menu) menu.classList.remove('open');

  renderList();
}

export function toggleSort() {
  const sortButton = document.getElementById('sort-btn');
  if (!sortButton) return;

  // Toggle sort mode
  state.sortMode = state.sortMode === 'score' ? 'date' : 'score';

  // Update button appearance
  const isDateSort = state.sortMode === 'date';
  sortButton.textContent = isDateSort ? '⇅ DATE' : '⇅ SCORE';
  sortButton.style.color = isDateSort ? 'var(--accent)' : 'var(--text3)';
  sortButton.style.borderColor = isDateSort ? 'rgba(96,165,250,0.4)' : 'var(--border2)';

  renderList();
}

// ============================================================================
// Clean Up (bulk delete popover)
// ============================================================================

function isOlderThan30Days(scrapedAt) {
  if (!scrapedAt) return false;
  const cutoff = new Date();
  cutoff.setDate(cutoff.getDate() - 30);
  return new Date(scrapedAt) < cutoff;
}

// Single source of truth: each category knows how to match a job locally (for
// the counts we show) and how to ask the server to delete it. Keeps the
// displayed breakdown and the actual deletion from drifting apart.
// How long the "✓ Cleaned N" confirmation stays before the button resets.
const CLEANUP_DONE_FLASH_MS = 1400;

const CLEANUP_CATEGORIES = [
  {
    key: 'skipped',
    label: 'Skipped',
    matches: job => job.user_status === 'skipped',
    requestBody: { status: 'skipped', older_than_days: 0 }
  },
  {
    key: 'noGo',
    label: 'No Go',
    matches: job => (job.fit_label || '').toLowerCase() === 'no go',
    requestBody: { fit_label: 'no go' }
  },
  {
    key: 'oldUnseen',
    label: 'Unseen > 30 days',
    matches: job => (!job.user_status || job.user_status === 'unseen') && isOlderThan30Days(job.scraped_at),
    requestBody: { status: 'unseen', older_than_days: 30 }
  }
];

function cleanupElements() {
  return {
    button: document.getElementById('bulk-delete-btn'),
    menu: document.getElementById('bulk-delete-menu')
  };
}

// Distinct jobs matched by any of the given categories (one job may match several).
function countDistinct(categories) {
  const active = state.allJobs.filter(job => job.user_status !== 'deleted');
  const ids = new Set();
  active.forEach(job => {
    if (categories.some(category => category.matches(job))) ids.add(job.job_id);
  });
  return ids.size;
}

async function bulkDeleteRequest(body) {
  const res = await fetch('/api/jobs/bulk', {
    method: 'DELETE',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  if (!res.ok) {
    const data = await res.json().catch(() => ({}));
    throw new Error(data.detail || 'Bulk delete failed');
  }
  const data = await res.json();
  return data.deleted || 0;
}

function toastBulk(message, isError = false) {
  const toast = document.getElementById('toast');
  if (!toast) return;
  toast.textContent = message;
  toast.style.borderColor = isError ? 'rgba(248,113,113,0.35)' : 'rgba(96,165,250,0.3)';
  toast.style.color = isError ? 'var(--red)' : 'var(--accent)';
  toast.classList.add('show');
  setTimeout(() => toast.classList.remove('show'), 2000);
}

function closeMenu() {
  const { button, menu } = cleanupElements();
  if (menu) menu.classList.remove('open');
  if (button) button.setAttribute('aria-expanded', 'false');
}

function selectedCategories(menu) {
  return CLEANUP_CATEGORIES.filter(category =>
    menu.querySelector(`input[data-key="${category.key}"]`)?.checked
  );
}

// The confirm button always shows how many distinct jobs the current selection deletes.
function refreshConfirmButton(menu) {
  const confirmButton = menu.querySelector('.cleanup-confirm');
  if (!confirmButton) return;
  const count = countDistinct(selectedCategories(menu));
  confirmButton.textContent = `Delete ${count}`;
  confirmButton.disabled = count === 0;
}

function renderMenu(menu) {
  const available = CLEANUP_CATEGORIES
    .map(category => ({ category, count: countDistinct([category]) }))
    .filter(entry => entry.count > 0);

  if (available.length === 0) {
    menu.innerHTML = '<span class="bulk-del-empty">Nothing to clean up</span>';
    return;
  }

  const rows = available.map(({ category, count }) => `
    <label class="cleanup-row">
      <input type="checkbox" data-key="${category.key}" checked>
      <span>${category.label}</span>
      <span class="cleanup-count">${count}</span>
    </label>`).join('');

  menu.innerHTML = `${rows}
    <div class="cleanup-foot">
      <button class="cleanup-confirm"></button>
      <button class="cleanup-cancel">Cancel</button>
    </div>`;

  refreshConfirmButton(menu);
  menu.querySelectorAll('input[data-key]').forEach(input =>
    input.addEventListener('change', () => refreshConfirmButton(menu))
  );
  menu.querySelector('.cleanup-cancel').addEventListener('click', closeMenu);
  menu.querySelector('.cleanup-confirm').addEventListener('click', () => runCleanup(menu));
}

async function runCleanup(menu) {
  const categories = selectedCategories(menu);
  if (categories.length === 0) return;

  const { button } = cleanupElements();
  closeMenu();
  button.classList.remove('done', 'error');
  button.classList.add('running');
  button.disabled = true;
  button.textContent = '⟳ Cleaning...';

  try {
    let deleted = 0;
    for (const category of categories) {
      deleted += await bulkDeleteRequest(category.requestBody);
    }
    state.allJobs = await fetch('/api/jobs').then(r => r.json());
    renderList();
    updateStats();
    toastBulk(`Cleaned up ${deleted} jobs`);
    button.classList.remove('running');
    button.classList.add('done');
    button.textContent = `✓ Cleaned ${deleted}`;
    setTimeout(updateCleanupButton, CLEANUP_DONE_FLASH_MS);
  } catch (e) {
    button.classList.remove('running');
    button.classList.add('error');
    button.disabled = false;
    button.textContent = '⚠ Retry';
    toastBulk(e.message, true);
  }
}

function updateCleanupButton() {
  const { button } = cleanupElements();
  if (!button) return;
  const total = countDistinct(CLEANUP_CATEGORIES);
  button.classList.remove('running', 'done', 'error');
  button.disabled = total === 0;
  button.title = total > 0 ? 'Review and delete skipped, No Go and old unseen jobs' : 'Nothing to clean up';
  button.textContent = total > 0 ? `🗑 Clean up (${total})` : '✓ Clean';
}

// Kept name for callers (console.js) that refresh the header after data changes.
export function updateBulkDeleteMenu() {
  updateCleanupButton();
  const { menu } = cleanupElements();
  if (menu && menu.classList.contains('open')) renderMenu(menu);
}

export function initBulkDeleteDropdown() {
  const { button, menu } = cleanupElements();
  if (!button || !menu) return;

  updateCleanupButton();

  button.addEventListener('click', e => {
    e.stopPropagation();
    if (button.classList.contains('running')) return;
    const isOpen = menu.classList.toggle('open');
    button.setAttribute('aria-expanded', String(isOpen));
    if (isOpen) renderMenu(menu);
  });

  // Clicks inside the popover must not reach the close-on-outside handler.
  menu.addEventListener('click', e => e.stopPropagation());
  document.addEventListener('click', closeMenu);
}