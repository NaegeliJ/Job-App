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
// Two-click Clean Up
// ============================================================================

function isOlderThan30Days(scrapedAt) {
  if (!scrapedAt) return false;
  const cutoff = new Date();
  cutoff.setDate(cutoff.getDate() - 30);
  return new Date(scrapedAt) < cutoff;
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

function getCleanupCandidates() {
  const active = state.allJobs.filter(j => j.user_status !== 'deleted');
  const ids = new Set();
  const counts = {
    skipped: 0,
    noGo: 0,
    oldUnseen: 0
  };

  active.forEach(job => {
    const isSkipped = job.user_status === 'skipped';
    const isNoGo = (job.fit_label || '').toLowerCase() === 'no go';
    const isOldUnseen = (!job.user_status || job.user_status === 'unseen') && isOlderThan30Days(job.scraped_at);

    if (isSkipped) counts.skipped += 1;
    if (isNoGo) counts.noGo += 1;
    if (isOldUnseen) counts.oldUnseen += 1;
    if (isSkipped || isNoGo || isOldUnseen) ids.add(job.job_id);
  });

  return { total: ids.size, counts };
}

function updateCleanupButton(btn, armed = false) {
  const { total, counts } = getCleanupCandidates();
  btn.disabled = false;
  btn.classList.toggle('confirming', armed);
  btn.classList.remove('running', 'done', 'error');
  btn.title = total > 0
    ? `Skipped: ${counts.skipped} · No Go: ${counts.noGo} · Old unseen: ${counts.oldUnseen}`
    : 'Nothing to clean up';

  if (total === 0) {
    btn.textContent = '✓ Clean';
    btn.disabled = true;
    btn.classList.remove('confirming');
    return;
  }

  btn.textContent = armed ? `⚠ Confirm cleanup (${total})` : `🗑 Clean up (${total})`;
}

async function runCleanup() {
  // Keep the old cleanup semantics, just without a dropdown:
  // skipped jobs, No Go jobs, and unseen jobs older than 30 days.
  const actions = [
    { status: 'skipped', older_than_days: 0 },
    { fit_label: 'no go' },
    { status: 'unseen', older_than_days: 30 }
  ];

  let deleted = 0;
  for (const body of actions) {
    deleted += await bulkDeleteRequest(body);
  }

  state.allJobs = await fetch('/api/jobs').then(r => r.json());
  renderList();
  updateStats();
  return deleted;
}

export function updateBulkDeleteMenu() {
  const btn = document.getElementById('bulk-delete-btn');
  if (btn) updateCleanupButton(btn, btn.classList.contains('confirming'));
}

export function initBulkDeleteDropdown() {
  const btn = document.getElementById('bulk-delete-btn');
  if (!btn) return;

  let confirmTimer = null;

  const reset = () => {
    if (confirmTimer) clearTimeout(confirmTimer);
    confirmTimer = null;
    updateCleanupButton(btn, false);
  };

  updateCleanupButton(btn, false);

  btn.addEventListener('click', async (e) => {
    e.stopPropagation();
    if (btn.disabled || btn.classList.contains('running')) return;

    if (!btn.classList.contains('confirming')) {
      updateCleanupButton(btn, true);
      btn.classList.add('pulse-confirm');
      setTimeout(() => btn.classList.remove('pulse-confirm'), 650);
      confirmTimer = setTimeout(reset, 4500);
      return;
    }

    if (confirmTimer) clearTimeout(confirmTimer);
    confirmTimer = null;
    btn.classList.remove('confirming');
    btn.classList.add('running');
    btn.disabled = true;
    btn.textContent = '⟳ Cleaning...';

    try {
      const deleted = await runCleanup();
      btn.classList.remove('running');
      btn.classList.add('done');
      btn.textContent = `✓ Cleaned ${deleted}`;
      toastBulk(`Cleaned up ${deleted} jobs`);
      setTimeout(() => updateCleanupButton(btn, false), 1400);
    } catch (e) {
      btn.classList.remove('running');
      btn.classList.add('error');
      btn.disabled = false;
      btn.textContent = '⚠ Retry cleanup';
      toastBulk(e.message, true);
    }
  });

  document.addEventListener('click', reset);
}
