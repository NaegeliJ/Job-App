#!/usr/bin/env node
// Build frontend/data/ch-geo.json from GeoNames Swiss postal data.
//
// Regenerate (LI included — Liechtenstein zips show up in Ostschweiz jobs):
//   curl -L -o /tmp/CH.zip https://download.geonames.org/export/zip/CH.zip
//   curl -L -o /tmp/LI.zip https://download.geonames.org/export/zip/LI.zip
//   unzip -o /tmp/CH.zip -d /tmp && unzip -o /tmp/LI.zip -d /tmp
//   node scripts/build-geo-json.mjs /tmp/CH.txt /tmp/LI.txt
//
// Match-rate check against the real database (writes nothing):
//   node scripts/build-geo-json.mjs /tmp/CH.txt /tmp/LI.txt --dry-run data/jobs_v2.db

import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { normalizePlace, createGeocoder } from '../frontend/js/geocode.js';

const REPO_ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const OUTPUT_PATH = join(REPO_ROOT, 'frontend', 'data', 'ch-geo.json');

// English exonyms and spellings GeoNames' local names don't cover.
// Accent folding already handles zurich/geneve/neuchatel spellings.
// Grown from dry-run misses — rerun with --dry-run after editing.
const ALIASES = {
  geneva: 'geneve',
  lucerne: 'luzern',
  berne: 'bern',
  basle: 'basel'
};

const CANTON_SUFFIX = /\s+(AG|AI|AR|BE|BL|BS|FR|GE|GL|GR|JU|LU|NE|NW|OW|SG|SH|SO|SZ|TG|TI|UR|VD|VS|ZG|ZH)$/;

function round4(value) {
  return Math.round(value * 10000) / 10000;
}

// Swiss German ASCII convention writes ä/ö/ü as ae/oe/ue ("Daettwil").
// normalizePlace folds them to a/o/u instead, so index both spellings.
function keyVariants(rawPart) {
  const transliterated = rawPart.toLowerCase()
    .replace(/ä/g, 'ae').replace(/ö/g, 'oe').replace(/ü/g, 'ue');
  return [normalizePlace(rawPart), normalizePlace(transliterated)];
}

// One GeoNames row serves several lookup keys: "Biel/Bienne" should match
// "biel" and "bienne", "Haag (Rheintal)" plain "haag", "Brugg AG" plain "brugg".
function nameKeys(rawName) {
  // Postal branch suffix ("Aarau 1") is noise, not a distinct place
  const base = rawName.replace(/\s+\d+$/, '');
  const parts = new Set([base, base.replace(CANTON_SUFFIX, ''), base.replace(/\(.*\)/, '')]);
  for (const slashPart of base.split('/')) parts.add(slashPart);

  const keys = new Set();
  for (const part of parts) for (const key of keyVariants(part)) keys.add(key);
  keys.delete('');
  return keys;
}

function buildGeoData(geoNamesPaths) {
  const zip = {};
  const nameRows = {};

  for (const path of geoNamesPaths) {
    for (const line of readFileSync(path, 'utf-8').split('\n')) {
      if (!line.trim()) continue;
      const columns = line.split('\t');
      const [, zipcode, placeName] = columns;
      const coords = [round4(Number(columns[9])), round4(Number(columns[10]))];

      if (!(zipcode in zip)) zip[zipcode] = coords;
      for (const key of nameKeys(placeName)) {
        // Lowest zip wins: it marks the main locality ("Zürich" is 8000
        // city, not 8058 airport, whatever the file order says)
        if (!(key in nameRows) || zipcode < nameRows[key].zipcode) {
          nameRows[key] = { zipcode, coords };
        }
      }
    }
  }

  const name = {};
  for (const [key, row] of Object.entries(nameRows)) name[key] = row.coords;
  return { zip, name, alias: ALIASES };
}

async function dryRun(geoData, databasePath) {
  const { DatabaseSync } = await import('node:sqlite');
  const database = new DatabaseSync(databasePath, { readOnly: true });
  const jobs = database
    .prepare("SELECT zipcode, place FROM jobs WHERE user_status IS NULL OR user_status != 'deleted'")
    .all();
  database.close();

  const geocoder = createGeocoder(geoData);
  const hitsByMethod = { zipcode: 0, name: 0 };
  const misses = new Map();

  for (const job of jobs) {
    const hit = geocoder.locate(job);
    if (hit) {
      hitsByMethod[hit.method] += 1;
    } else {
      const key = `${job.place || '(empty)'} [zip: ${job.zipcode || '-'}]`;
      misses.set(key, (misses.get(key) || 0) + 1);
    }
  }

  const total = jobs.length;
  const located = hitsByMethod.zipcode + hitsByMethod.name;
  console.log(`${total} jobs: ${located} located (${((located / total) * 100).toFixed(1)}%)`);
  console.log(`  by zipcode: ${hitsByMethod.zipcode}, by name: ${hitsByMethod.name}`);
  console.log(`\nMisses (${total - located} jobs, ${misses.size} distinct):`);
  for (const [place, count] of [...misses.entries()].sort((a, b) => b[1] - a[1])) {
    console.log(`  ${String(count).padStart(4)}  ${place}`);
  }
}

const flagIndex = process.argv.indexOf('--dry-run');
const geoNamesPaths = process.argv.slice(2, flagIndex === -1 ? undefined : flagIndex);
if (geoNamesPaths.length === 0) {
  console.error('Usage: node scripts/build-geo-json.mjs <CH.txt> [LI.txt ...] [--dry-run <database>]');
  process.exit(1);
}

const geoData = buildGeoData(geoNamesPaths);

if (flagIndex !== -1) {
  await dryRun(geoData, process.argv[flagIndex + 1]);
} else {
  writeFileSync(OUTPUT_PATH, JSON.stringify(geoData));
  const kilobytes = (JSON.stringify(geoData).length / 1024).toFixed(0);
  console.log(`Wrote ${OUTPUT_PATH} (${kilobytes} KB, ${Object.keys(geoData.zip).length} zips, ${Object.keys(geoData.name).length} names)`);
}
