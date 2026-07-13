import { GET_URL } from './api.js';
import { escapeHtml } from './utils/formatting.js';
import { isClosedApplication } from './application-status.js';
import { createGeocoder } from './geocode.js';

// Marker colors follow the sidebar fit-tag scheme (sidebar.css .stag)
const LABEL_COLORS = {
  'strong': '#2dd4bf',
  'decent': '#34d399',
  'experimental': '#fbbf24',
  'weak': '#f97316',
  'no go': '#991b1b'
};
const UNKNOWN_COLOR = '#8a8a96';

const HOME_STORAGE_KEY = 'map-home';
const SWITZERLAND_CENTER = [46.8, 8.2];

let map;
let clusterGroup;
let homeMarker = null;
let homeCircle = null;

let entries = [];          // { job, position: {lat, lon} | null }
let currentFilter = 'all';
let home = null;           // { lat, lon, radiusKm }
let setHomeArmed = false;

// ── Filtering (same semantics as the dashboard fit-label filters) ──

function matchesFilter(job) {
  if (currentFilter === 'all') return true;
  const fitLabel = (job.fit_label || '').toLowerCase();
  if (currentFilter === 'weak') return fitLabel === 'weak' || fitLabel === 'no go';
  if (currentFilter === 'unseen') return !job.user_status || job.user_status === 'unseen';
  if (currentFilter === 'interested' || currentFilter === 'applied') {
    return job.user_status === currentFilter;
  }
  return fitLabel === currentFilter;
}

function haversineKilometers(a, b) {
  const earthRadius = 6371;
  const rad = Math.PI / 180;
  const dLat = (b.lat - a.lat) * rad;
  const dLon = (b.lon - a.lon) * rad;
  const h = Math.sin(dLat / 2) ** 2 +
    Math.cos(a.lat * rad) * Math.cos(b.lat * rad) * Math.sin(dLon / 2) ** 2;
  return 2 * earthRadius * Math.asin(Math.sqrt(h));
}

function withinRadius(position) {
  if (!home) return true;
  return haversineKilometers(home, position) <= home.radiusKm;
}

// ── Rendering ───────────────────────────────────────────────────────

function popupHtml(job) {
  const label = job.fit_label || 'Unknown';
  const score = job.fit_score != null ? ` | ${job.fit_score}` : '';
  return `
    <div class="map-popup-title">
      <a href="/?job=${encodeURIComponent(job.job_id)}">${escapeHtml(job.title || 'Unknown')}</a>
    </div>
    <div class="map-popup-company">${escapeHtml(job.company_name || '—')}</div>
    <div class="map-popup-meta">${escapeHtml(label)}${score} · ${escapeHtml(job.place || '')}</div>`;
}

function buildMarker(entry) {
  const color = LABEL_COLORS[(entry.job.fit_label || '').toLowerCase()] || UNKNOWN_COLOR;
  return L.circleMarker([entry.position.lat, entry.position.lon], {
    radius: 7,
    fillColor: color,
    fillOpacity: 0.85,
    color: '#141416',
    weight: 1
  }).bindPopup(popupHtml(entry.job));
}

function renderMarkers() {
  clusterGroup.clearLayers();

  const filtered = entries.filter(e => matchesFilter(e.job));
  const shown = filtered.filter(e => e.position && withinRadius(e.position));
  clusterGroup.addLayers(shown.map(buildMarker));

  document.getElementById('map-count').textContent =
    `${shown.length} / ${entries.length} jobs`;

  renderUnlocated(filtered.filter(e => !e.position));
}

function renderUnlocated(unlocated) {
  const drawer = document.getElementById('unlocated-drawer');
  drawer.hidden = unlocated.length === 0;
  document.getElementById('unlocated-count').textContent = unlocated.length;

  document.getElementById('unlocated-list').innerHTML = unlocated.map(e => `
    <div class="unlocated-item">
      <a href="/?job=${encodeURIComponent(e.job.job_id)}">${escapeHtml(e.job.title || 'Unknown')}</a>
      — ${escapeHtml(e.job.company_name || '—')} · ${escapeHtml(e.job.place || 'no location')}
    </div>`).join('');
}

// ── Home position & radius ──────────────────────────────────────────

function saveHome() {
  if (home) localStorage.setItem(HOME_STORAGE_KEY, JSON.stringify(home));
  else localStorage.removeItem(HOME_STORAGE_KEY);
}

function renderHome() {
  if (homeMarker) { homeMarker.remove(); homeMarker = null; }
  if (homeCircle) { homeCircle.remove(); homeCircle = null; }

  const radiusLabel = document.getElementById('radius-label');
  radiusLabel.hidden = !home;
  if (!home) return;

  document.getElementById('radius-slider').value = home.radiusKm;
  document.getElementById('radius-value').textContent = `${home.radiusKm} km`;

  homeCircle = L.circle([home.lat, home.lon], {
    radius: home.radiusKm * 1000,
    color: '#2e8cff',
    weight: 1,
    fillOpacity: 0.04
  }).addTo(map);

  homeMarker = L.marker([home.lat, home.lon], { title: 'Home' })
    .addTo(map)
    .bindPopup('<button class="tool-btn" id="remove-home-btn">Remove home</button>');
}

function setHome(lat, lon) {
  const radiusKm = home ? home.radiusKm : Number(document.getElementById('radius-slider').value);
  home = { lat, lon, radiusKm };
  saveHome();
  renderHome();
  renderMarkers();
}

function removeHome() {
  home = null;
  saveHome();
  renderHome();
  renderMarkers();
}

function armSetHome(armed) {
  setHomeArmed = armed;
  const button = document.getElementById('set-home-btn');
  button.classList.toggle('armed', armed);
  button.textContent = armed ? '⌂ Click the map…' : '⌂ Set home';
}

function geolocate() {
  if (!navigator.geolocation) {
    alert('Geolocation not supported by this browser.');
    return;
  }
  navigator.geolocation.getCurrentPosition(
    pos => setHome(pos.coords.latitude, pos.coords.longitude),
    // Insecure contexts (LAN IP over http) block geolocation — localhost works
    () => alert('Location unavailable. On a LAN address the browser blocks geolocation — use "Set home" and click the map instead.')
  );
}

// ── Setup ───────────────────────────────────────────────────────────

function initMap() {
  map = L.map('map').setView(SWITZERLAND_CENTER, 8);

  L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
  }).addTo(map);

  // Same-zip jobs share exact coordinates — spiderfy is what makes them reachable
  clusterGroup = L.markerClusterGroup({
    maxClusterRadius: 40,
    spiderfyOnMaxZoom: true,
    showCoverageOnHover: false
  });
  map.addLayer(clusterGroup);

  map.on('click', e => {
    if (!setHomeArmed) return;
    armSetHome(false);
    setHome(e.latlng.lat, e.latlng.lng);
  });

  // Popup content is injected by Leaflet, so bind the remove button lazily
  map.on('popupopen', e => {
    const button = e.popup.getElement().querySelector('#remove-home-btn');
    if (button) button.addEventListener('click', removeHome);
  });
}

function initControls() {
  document.getElementById('map-filters').addEventListener('click', e => {
    const chip = e.target.closest('.chip');
    if (!chip) return;
    currentFilter = chip.dataset.filter;
    document.querySelectorAll('.chip').forEach(c => c.classList.remove('active'));
    chip.classList.add('active');
    renderMarkers();
  });

  document.getElementById('set-home-btn').addEventListener('click', () => armSetHome(!setHomeArmed));
  document.getElementById('geolocate-btn').addEventListener('click', geolocate);

  document.getElementById('radius-slider').addEventListener('input', e => {
    if (!home) return;
    home.radiusKm = Number(e.target.value);
    document.getElementById('radius-value').textContent = `${home.radiusKm} km`;
    saveHome();
    renderHome();
    renderMarkers();
  });

  document.getElementById('unlocated-toggle').addEventListener('click', () => {
    const list = document.getElementById('unlocated-list');
    list.hidden = !list.hidden;
  });
}

async function init() {
  initMap();
  initControls();

  try {
    const [jobs, geoData] = await Promise.all([
      fetch(GET_URL).then(r => r.json()),
      fetch('/data/ch-geo.json').then(r => r.json())
    ]);

    const geocoder = createGeocoder(geoData);
    // Same visibility rule as the dashboard: deleted and closed applications stay off the map
    entries = jobs
      .filter(job => job.user_status !== 'deleted' && !isClosedApplication(job))
      .map(job => ({ job, position: geocoder.locate(job) }));

    try {
      const stored = JSON.parse(localStorage.getItem(HOME_STORAGE_KEY));
      if (typeof stored?.lat === 'number' && typeof stored?.lon === 'number') home = stored;
    } catch { /* corrupt entry — start without home */ }

    renderHome();
    renderMarkers();
  } catch (error) {
    console.error('Map init error:', error);
    document.getElementById('map-count').textContent = 'Connection failed';
  }
}

document.addEventListener('DOMContentLoaded', init);
