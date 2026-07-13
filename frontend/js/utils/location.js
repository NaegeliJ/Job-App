// Location filter utilities

/**
 * Geocode a place name using Nominatim (OpenStreetMap) - free, no API key
 */
export async function geocodePlace(placeName) {
  const url = `https://nominatim.openstreetmap.org/search?q=${encodeURIComponent(placeName)}&format=json&limit=1`;
  const res = await fetch(url, { headers: { 'Accept-Language': 'de' } });
  const data = await res.json();
  if (!data || data.length === 0) throw new Error(`Ort "${placeName}" nicht gefunden`);
  return { lat: parseFloat(data[0].lat), lon: parseFloat(data[0].lon), display: data[0].display_name };
}

/**
 * Haversine distance in km between two lat/lon points
 */
export function distanceKm(lat1, lon1, lat2, lon2) {
  const R = 6371;
  const dLat = (lat2 - lat1) * Math.PI / 180;
  const dLon = (lon2 - lon1) * Math.PI / 180;
  const a = Math.sin(dLat/2)**2 + Math.cos(lat1*Math.PI/180) * Math.cos(lat2*Math.PI/180) * Math.sin(dLon/2)**2;
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
}

/**
 * Load location filter config from localStorage
 */
export function loadLocationFilter() {
  try {
    const raw = localStorage.getItem('location_filter');
    return raw ? JSON.parse(raw) : null;
  } catch { return null; }
}

/**
 * Save location filter config to localStorage
 */
export function saveLocationFilter(config) {
  if (!config) { localStorage.removeItem('location_filter'); return; }
  localStorage.setItem('location_filter', JSON.stringify(config));
}

/**
 * Check if a job place string is within radius of center.
 * Returns: { within: bool, distance: number|null, reason: string }
 */
export async function checkJobLocation(jobPlace, filter) {
  if (!filter || !filter.enabled || !filter.lat) return { within: true, distance: null, reason: '' };
  if (!jobPlace || jobPlace === '—') return { within: true, distance: null, reason: 'no location' };

  try {
    const url = `https://nominatim.openstreetmap.org/search?q=${encodeURIComponent(jobPlace + ', Switzerland')}&format=json&limit=1`;
    const res = await fetch(url, { headers: { 'Accept-Language': 'de' } });
    const data = await res.json();
    if (!data || data.length === 0) return { within: true, distance: null, reason: 'geocode failed' };

    const jobLat = parseFloat(data[0].lat);
    const jobLon = parseFloat(data[0].lon);
    const dist = distanceKm(filter.lat, filter.lon, jobLat, jobLon);

    return {
      within: dist <= filter.radiusKm,
      distance: Math.round(dist),
      reason: `${Math.round(dist)} km from ${filter.place}`
    };
  } catch {
    return { within: true, distance: null, reason: 'geocode error' };
  }
}
