import state from '../state.js';
import { fmtDate, escapeHtml } from '../utils/formatting.js';
import { setStatus, setExpired, saveNotes, setRating, showToast } from './actions.js';

function buildGoogleMapsUrl(zip, city) {
  const query = encodeURIComponent(`${zip} ${city} Switzerland`);
  return `https://www.google.com/maps/search/?api=1&query=${query}`;
}

function generateStarsHtml(rating = 0) {
  return [1, 2, 3, 4, 5]
    .map(n => `<span class="star${n <= rating ? ' filled' : ''}" data-rating="${n}">★</span>`)
    .join('');
}

function getFitVerdict(job) {
  const score = job.fit_score !== undefined ? job.fit_score : 0;
  const label = job.fit_label || 'Unknown';
  return {
    score,
    label,
    className: label.toLowerCase().replace(' ', '')
  };
}

function cleanTemplateText(text) {
  if (!text) return '';

  let cleaned = text.replace(/^["']|["']$/g, '');

  cleaned = cleaned.replace(/<br\s*\/?>/gi, '\n');
  cleaned = cleaned.replace(/<\/p>/gi, '\n\n');
  cleaned = cleaned.replace(/<\/div>/gi, '\n');
  cleaned = cleaned.replace(/<li>/gi, '• ');
  cleaned = cleaned.replace(/<\/li>/gi, '\n');
  cleaned = cleaned.replace(/<[^>]+>/g, '');

  const textarea = document.createElement('textarea');
  textarea.innerHTML = cleaned;
  cleaned = textarea.value;

  cleaned = cleaned.replace(/<[^>]+>/g, '');

  cleaned = cleaned
    .replace(/\n\s*\n\s*\n+/g, '\n\n')
    .replace(/\n[ \t]+/g, '\n')
    .replace(/[ \t]+\n/g, '\n')
    .replace(/\n+/g, '\n')
    .replace(/^[\s\n]+|[\s\n]+$/g, '')
    .replace(/\s{2,}/g, ' ')
    .trim();

  return cleaned;
}

function buildHeader(job, city, mapsUrl, displayScore, displayLabel, starsHtml) {
  const zip = escapeHtml(job.zipcode || '');
  const detailUrl = /^https?:\/\//.test(job.detail_url || '') ? job.detail_url : '';
  const appUrl = /^https?:\/\//.test(job.application_url || '') ? job.application_url : '';
  const safeJobUrl = detailUrl || appUrl;
  const jobUrlLabel = job.source === 'linkedin' ? 'View on LinkedIn ↗' : (detailUrl ? 'View on jobs.ch ↗' : 'View Job ↗');

  return `
    <div class="detail-header">
      <div class="fit-badge-col">
        <div class="fit-badge ${escapeHtml(displayLabel.toLowerCase().replace(' ', ''))}">
          <div class="fit-badge-label">${escapeHtml(displayLabel)}</div>
          <div class="fit-badge-score">${displayScore}</div>
          <div class="fit-badge-stars" id="badge-rating-stars">${starsHtml}</div>
        </div>
      </div>

      <div class="header-content">
        <div class="title-row">
          <h1 class="job-title">${escapeHtml(job.title || 'Unknown')}</h1>
          <div style="display:flex;align-items:center;gap:8px;flex-shrink:0">
            ${safeJobUrl ? `<a href="${escapeHtml(safeJobUrl)}" class="view-job-btn" target="_blank" rel="noopener">${jobUrlLabel}</a>` : ''}
            <button class="recheck-btn" id="recheck-btn" title="Re-check this job">🔄 Redo Fit-Check</button>
          </div>
        </div>

        <div class="metadata-row">
          <span class="meta-item company">${escapeHtml(job.company_name || '—')}</span>
          <a href="${mapsUrl}" class="meta-item location" target="_blank" rel="noopener" title="View on Google Maps">
            📍 ${zip} ${escapeHtml(city)} ↗
          </a>
          ${job.pub_date ? `<span class="meta-item date">📅 ${fmtDate(job.pub_date)}</span>` : ''}
          ${job.end_date ? `<span class="meta-item expiry">⏱️ ${fmtDate(job.end_date)}</span>` : ''}
          <span class="meta-item job-id">ID: ${escapeHtml(job.job_id.slice(-8))}</span>
        </div>
      </div>
    </div>`;
}

function buildFitSection(job) {
  if (!job.fit_summary && !job.fit_reasoning) return '';

  return `
    <div class="fit-section">
      <div class="section-header">
        <span class="section-icon">🤖</span>
        <span class="section-title">AI Fit Assessment</span>
      </div>
      ${job.fit_summary ? `<div class="fit-summary">${escapeHtml(job.fit_summary)}</div>` : ''}
      ${job.fit_reasoning ? `<div class="fit-reasoning">${escapeHtml(job.fit_reasoning)}</div>` : ''}
    </div>`;
}

function buildTemplateSection(text) {
  if (!text) return '';
  const safe = escapeHtml(text).replace(/\n/g, '<br>');
  return `
    <div class="template-section">
      <div class="section-header">
        <span class="section-icon">📄</span>
        <span class="section-title">Job Description</span>
      </div>
      <div class="template-text">${safe}</div>
    </div>`;
}

function buildSecondaryInfo(job) {
  return `
    <div class="detail-body">
      <div class="two-col">
        <div class="col-left">
          <div class="section notes-section">
            <div class="st">Notes</div>
            <textarea class="notes-ta" id="notes-input" placeholder="Your private notes...">${escapeHtml(job.notes || '')}</textarea>
            <button class="save-b" id="save-notes-btn">Save Notes</button>
          </div>
        </div>
      </div>
    </div>`;
}

function setupActionBar(status) {
  const actionBar = document.getElementById('action-bar');
  if (!actionBar) return;

  actionBar.style.display = 'flex';
  actionBar.innerHTML = `
    <div class="ab-left">
      <button class="ab ai" id="btn-i" data-status="interested">✦ Star</button>
      <button class="ab aa" id="btn-a" data-status="applied">✓ Applied</button>
      <button class="ab as" id="btn-s" data-status="skipped">✕ Skip</button>
      <button class="ab ae" id="btn-e">🗑️ Delete</button>
    </div>
  `;
}

function setupEventHandlers(status, ratingStars) {
  const bi = document.getElementById('btn-i');
  const ba = document.getElementById('btn-a');
  const bs = document.getElementById('btn-s');
  const be = document.getElementById('btn-e');

  if (status === 'interested') bi.classList.add('act');
  if (status === 'applied') ba.classList.add('act');
  if (status === 'skipped') bs.classList.add('act');

  bi.addEventListener('click', () => setStatus('interested'));
  ba.addEventListener('click', () => setStatus('applied'));
  bs.addEventListener('click', () => setStatus('skipped'));
  be.addEventListener('click', () => setExpired());

  const stars = ratingStars.querySelectorAll('.star');
  stars.forEach(star => {
    star.addEventListener('click', () => {
      const rating = parseInt(star.dataset.rating);
      if (!isNaN(rating)) setRating(rating);
    });
  });
}

function setupRecheckButton() {
  const recheckBtn = document.getElementById('recheck-btn');
  if (!recheckBtn) return;

  recheckBtn.addEventListener('click', async () => {
    if (!state.currentJob || recheckBtn.classList.contains('running')) return;

    recheckBtn.classList.remove('error');
    recheckBtn.removeAttribute('title');
    recheckBtn.disabled = true;
    recheckBtn.classList.add('running');
    recheckBtn.innerHTML = '<span class="spin">⟳</span> Checking...';

    try {
      const response = await fetch(`/api/jobs/${encodeURIComponent(state.currentJob.job_id)}/fitcheck`, {
        method: 'POST'
      });
      const data = await response.json();

      if (!response.ok) {
        const msg = data.error_code === 'rate_limit'      ? 'Rate limit reached — try again later'      :
                    data.error_code === 'no_credits'      ? 'API credits exhausted'                      :
                    data.error_code === 'invalid_api_key' ? 'Invalid API key — check Settings'           :
                    data.error_code === 'unreachable'     ? 'AI provider unreachable — is it running?'   :
                    data.error || 'Fit-check failed';
        throw new Error(msg);
      }

      Object.assign(state.currentJob, data);
      const idx = state.allJobs.findIndex(j => j.job_id === state.currentJob.job_id);
      if (idx !== -1) Object.assign(state.allJobs[idx], data);

      renderDetail();
      import('./job-list.js').then(m => m.renderList());
      showToast('Fit-check complete');
      recheckBtn.disabled = false;
      recheckBtn.classList.remove('running');
      recheckBtn.innerHTML = '🔄 Redo Fit-Check';
    } catch (e) {
      showToast('Fit-check failed: ' + e.message, true);
      recheckBtn.disabled = false;
      recheckBtn.classList.remove('running');
      recheckBtn.innerHTML = '⚠ Redo Fit-Check';
      recheckBtn.classList.add('error');
      recheckBtn.title = e.message;
    }
  });
}

export function renderDetail() {
  const job = state.currentJob;
  if (!job) return;
  const status = job.user_status || 'unseen';

  const city = job.place || '';
  const mapsUrl = buildGoogleMapsUrl(job.zipcode || '', city);

  const fitVerdict = getFitVerdict(job);
  const templateText = cleanTemplateText(job.template_text);
  const starsHtml = generateStarsHtml(job.rating);

  document.getElementById('detail-scroll').innerHTML =
    buildHeader(job, city, mapsUrl, fitVerdict.score, fitVerdict.label, starsHtml) +
    buildFitSection(job) +
    buildTemplateSection(templateText) +
    buildSecondaryInfo(job);

  setupActionBar(status);
  setupEventHandlers(status, document.getElementById('badge-rating-stars'));
  setupRecheckButton();
}
