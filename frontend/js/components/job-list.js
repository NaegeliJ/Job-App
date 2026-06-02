import state from '../state.js';
import { fmtDate, getStatusIcon, escapeHtml } from '../utils/formatting.js';
import { renderDetail } from './detail.js';

// ============================================================================
// Filter Logic
// ============================================================================

function getFitLabel(job) {
  return (job.fit_label || '').toLowerCase();
}

function matchesFilter(job, filter) {
  if (filter === 'all') return true;
  if (filter === 'unseen') return !job.user_status || job.user_status === 'unseen';

  const fitLabel = getFitLabel(job);
  const filterMap = {
    'strong': () => fitLabel === 'strong',
    'decent': () => fitLabel === 'decent',
    'experimental': () => fitLabel === 'experimental',
    'weak': () => fitLabel === 'weak' || fitLabel === 'no go'
  };

  if (filterMap[filter]) return filterMap[filter]();

  return job.user_status === filter;
}

function matchesSearch(job, query) {
  if (!query) return true;

  const searchFields = [
    job.title,
    job.company_name,
    job.place
  ];

  return searchFields.some(field =>
    (field || '').toLowerCase().includes(query)
  );
}

function filterJobs(jobs, currentFilter, searchQuery) {
  return jobs.filter(job => job.user_status !== 'deleted').filter(job => {
    const passesFilter = matchesFilter(job, currentFilter);
    const passesSearch = matchesSearch(job, searchQuery);
    return passesFilter && passesSearch;
  });
}

// ============================================================================
// Sorting
// ============================================================================

function sortByDate(jobs) {
  return [...jobs].sort((a, b) =>
    (b.pub_date || '').localeCompare(a.pub_date || '')
  );
}

function sortByScore(jobs) {
  return [...jobs].sort((a, b) =>
    (b.fit_score || 0) - (a.fit_score || 0)
  );
}

// ============================================================================
// Job Item Rendering
// ============================================================================

function getFitDisplayInfo(job) {
  const score = job.fit_score || 0;
  const label = job.fit_label || 'Unknown';
  const cssClass = label.toLowerCase().replace(' ', '');

  return { score, label, cssClass };
}

function buildJobItemHtml(job) {
  const isActive = state.currentJob?.job_id === job.job_id;
  const status = job.user_status || 'unseen';
  const fitInfo = getFitDisplayInfo(job);

  return `
    <div
      class="job-item${isActive ? ' active' : ''} status-${status}"
      data-id="${escapeHtml(job.job_id)}"
    >
      <div style="display:flex;justify-content:space-between;align-items:flex-start;gap:4px">
        <div class="ji-title">${escapeHtml(job.title || 'Unknown')}</div>
        ${job.source === 'linkedin' ? '<span class="source-badge source-linkedin" style="flex-shrink:0;margin-top:1px">LI</span>' : ''}
      </div>
      <div class="ji-co">${escapeHtml(job.company_name || '—')}</div>
      <div class="ji-foot">
        <span class="stag ${fitInfo.cssClass}">${escapeHtml(fitInfo.label)} | ${fitInfo.score}</span>
        <div style="display:flex;align-items:center;gap:6px;max-width:55%">
          <span class="ji-meta" style="text-align:right;word-break:break-word">${escapeHtml(job.place || '—')}</span>
          ${getStatusIcon(status)}
        </div>
      </div>
    </div>`;
}

// ============================================================================
// Main Export Functions
// ============================================================================

export function renderList() {
  const jobListElement = document.getElementById('job-list');
  const countElement = document.getElementById('list-count');

  // Filter and sort
  let filteredJobs = filterJobs(
    state.allJobs,
    state.currentFilter,
    state.searchQuery
  );

  if (state.sortMode === 'date') {
    filteredJobs = sortByDate(filteredJobs);
  } else {
    filteredJobs = sortByScore(filteredJobs);
  }

  // Update count
  countElement.textContent = filteredJobs.length;

  // Render empty state or list
  if (filteredJobs.length === 0) {
    jobListElement.innerHTML = '<div class="empty"><div class="empty-t">No jobs</div></div>';
    return;
  }

  jobListElement.innerHTML = filteredJobs
    .map(buildJobItemHtml)
    .join('');
}

export function selectJob(jobId) {
  state.currentJob = state.allJobs.find(job => job.job_id === jobId);
  if (!state.currentJob) return;

  renderList();
  renderDetail();
}
