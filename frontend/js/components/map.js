import state from '../state.js';
import { escapeHtml } from '../utils/formatting.js';
import { getVisibleJobs, selectJob } from './job-list.js';

const CACHE_KEY = 'job_map_geocode_cache_v1';
const SWITZERLAND_CENTER = [47.3769, 8.5417];
const GEOCODE_DELAY_MS = 1100;

let map = null;
let markerLayer = null;
let lastRenderToken = 0;

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function loadCache() {
  try { return JSON.parse(localStorage.getItem(CACHE_KEY) || '{}'); }
  catch { return {}; }
}

function saveCache(cache) {
  try { localStorage.setItem(CACHE_KEY, JSON.stringify(cache)); }
  catch { /* ignore quota/private mode */ }
}

function normalizePlace(place) {
  return (place || '').replace(/\s+/g, ' ').trim();
}

function placeKey(place) {
  return normalizePlace(place).toLowerCase();
}

function jobLocationText(job) {
  const parts = [job.zipcode, job.place].filter(Boolean).join(' ');
  return normalizePlace(parts || job.place || '');
}

function groupJobsByPlace(jobs) {
  const groups = new Map();
  for (const job of jobs) {
    const place = jobLocationText(job);
    if (!place || place === '—') continue;
    const key = placeKey(place);
    if (!groups.has(key)) groups.set(key, { place, jobs: [] });
    groups.get(key).jobs.push(job);
  }
  return [...groups.values()];
}

async function geocodePlaceCached(place, cache) {
  const key = placeKey(place);
  if (cache[key]) return cache[key];

  const hasCountry = /\b(switzerland|schweiz|suisse|svizzera|germany|deutschland|austria|österreich)\b/i.test(place);
  const query = hasCountry ? place : `${place}, Switzerland`;
  const url = `https://nominatim.openstreetmap.org/search?format=json&limit=1&q=${encodeURIComponent(query)}`;
  const response = await fetch(url, { headers: { 'Accept-Language': 'de' } });
  if (!response.ok) throw new Error(`Geocoding failed: ${response.status}`);
  const data = await response.json();
  if (!data || data.length === 0) {
    cache[key] = null;
    saveCache(cache);
    return null;
  }

  const result = {
    lat: parseFloat(data[0].lat),
    lon: parseFloat(data[0].lon),
    display: data[0].display_name
  };
  cache[key] = result;
  saveCache(cache);
  return result;
}

function setMapStatus(message, isError = false) {
  const el = document.getElementById('map-status');
  if (!el) return;
  el.textContent = message || '';
  el.style.display = message ? 'block' : 'none';
  el.classList.toggle('error', Boolean(isError));
}

function setSubtitle(message) {
  const el = document.getElementById('map-subtitle');
  if (el) el.textContent = message;
}

function ensureMap() {
  if (!window.L) throw new Error('Leaflet konnte nicht geladen werden.');
  if (map) return map;

  map = L.map('job-map', { scrollWheelZoom: true }).setView(SWITZERLAND_CENTER, 9);
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; OpenStreetMap contributors'
  }).addTo(map);
  markerLayer = L.layerGroup().addTo(map);
  return map;
}

function fitMarkers(bounds) {
  if (bounds.length === 0) {
    map.setView(SWITZERLAND_CENTER, 9);
  } else if (bounds.length === 1) {
    map.setView(bounds[0], 12);
  } else {
    map.fitBounds(bounds, { padding: [28, 28], maxZoom: 12 });
  }
}

function buildPopupHtml(group) {
  const rows = group.jobs
    .slice(0, 8)
    .map(job => `
      <button class="map-popup-job" data-job-id="${escapeHtml(job.job_id)}">
        <strong>${escapeHtml(job.title || 'Unknown')}</strong>
        <span>${escapeHtml(job.company_name || '—')}</span>
      </button>`)
    .join('');
  const more = group.jobs.length > 8 ? `<div class="map-popup-more">+${group.jobs.length - 8} weitere Jobs an diesem Ort</div>` : '';
  return `<div class="map-popup"><div class="map-popup-place">${escapeHtml(group.place)}</div>${rows}${more}</div>`;
}

function bindPopupSelection() {
  const container = document.getElementById('job-map');
  if (!container || container.dataset.selectionBound === '1') return;
  container.dataset.selectionBound = '1';
  container.addEventListener('click', event => {
    const button = event.target.closest('.map-popup-job');
    if (!button) return;
    const jobId = button.dataset.jobId;
    if (!jobId) return;
    selectJob(jobId);
    closeJobMap();
    if (window.innerWidth <= 1024) document.querySelector('.main')?.classList.add('detail-open');
  });
}

async function renderMarkers() {
  const token = ++lastRenderToken;
  const jobs = getVisibleJobs().filter(job => job.user_status !== 'deleted');
  const groups = groupJobsByPlace(jobs);
  const cache = loadCache();
  const bounds = [];
  let geocoded = 0;
  let skipped = 0;

  markerLayer.clearLayers();
  setSubtitle(`${jobs.length} sichtbare Jobs · ${groups.length} Orte`);

  if (groups.length === 0) {
    setMapStatus('Keine sichtbaren Jobs mit Standort gefunden.');
    fitMarkers(bounds);
    return;
  }

  setMapStatus(`Standorte werden geladen… 0/${groups.length}`);

  for (let i = 0; i < groups.length; i++) {
    if (token !== lastRenderToken) return;
    const group = groups[i];
    try {
      const cachedBefore = Object.prototype.hasOwnProperty.call(cache, placeKey(group.place));
      const geo = await geocodePlaceCached(group.place, cache);
      if (!geo) {
        skipped += 1;
      } else {
        const marker = L.marker([geo.lat, geo.lon]).bindPopup(buildPopupHtml(group), { maxWidth: 320 });
        marker.addTo(markerLayer);
        bounds.push([geo.lat, geo.lon]);
        geocoded += 1;
      }
      if (!cachedBefore && i < groups.length - 1) await sleep(GEOCODE_DELAY_MS);
    } catch (error) {
      console.warn('Map geocode error:', group.place, error);
      skipped += 1;
    }
    setMapStatus(`Standorte werden geladen… ${i + 1}/${groups.length}`);
  }

  if (token !== lastRenderToken) return;
  fitMarkers(bounds);
  setMapStatus(skipped ? `${geocoded} Orte angezeigt, ${skipped} konnten nicht gefunden werden.` : '');
}

export async function openJobMap() {
  const overlay = document.getElementById('map-overlay');
  if (!overlay) return;
  overlay.classList.add('open');
  setMapStatus('Karte wird geladen…');

  try {
    ensureMap();
    bindPopupSelection();
    setTimeout(() => map.invalidateSize(), 80);
    await renderMarkers();
  } catch (error) {
    console.error('Map error:', error);
    setMapStatus(error.message || 'Karte konnte nicht geladen werden.', true);
  }
}

export function closeJobMap() {
  const overlay = document.getElementById('map-overlay');
  if (overlay) overlay.classList.remove('open');
}
