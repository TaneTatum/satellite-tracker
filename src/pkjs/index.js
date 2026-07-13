/**
 * PebbleKit JS — Satellite Tracker
 *
 * Fetches TLE data from CelesTrak for the configured NORAD ID, propagates it
 * with satellite.js (SGP4/SDP4), and sends the watch one window of minute-
 * by-minute positions spanning PAST_MIN minutes before "now" through
 * FUTURE_MIN minutes after, plus NowIndex marking where "now" falls in that
 * array. The watch steps a pointer through the window locally once a minute
 * (past history is already precomputed, not accumulated live) and only asks
 * for a refill roughly once per window (~every FUTURE_MIN minutes).
 */

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);
var satellite = require('satellite.js');

var PAST_MIN = 45;
var FUTURE_MIN = 45;
var TLE_STALE_MS = 20 * 60 * 60 * 1000; // refetch at most ~once/day
var DEFAULT_NORAD_ID = 25544; // ISS (ZARYA)

var xhrRequest = function (url, type, callback, onError) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    if (xhr.status >= 200 && xhr.status < 300) {
      callback(xhr.responseText);
    } else if (onError) {
      onError(new Error('HTTP ' + xhr.status));
    }
  };
  xhr.onerror = function () {
    if (onError) onError(new Error('network error'));
  };
  xhr.open(type, url);
  xhr.send();
};

function packInt16Array(values) {
  var bytes = [];
  for (var i = 0; i < values.length; i++) {
    var v = values[i] & 0xFFFF;
    bytes.push(v & 0xFF, (v >> 8) & 0xFF);
  }
  return bytes;
}

function isTLEStale(noradId) {
  var cachedId = localStorage.getItem('tleNoradId');
  var fetchedAt = parseInt(localStorage.getItem('tleFetchedAt') || '0', 10);
  return (cachedId !== String(noradId)) ||
         (!fetchedAt) ||
         (Date.now() - fetchedAt > TLE_STALE_MS);
}

function fetchTLE(noradId, cb) {
  var url = 'https://celestrak.org/NORAD/elements/gp.php?CATNR=' + noradId + '&FORMAT=3LE';
  xhrRequest(url, 'GET', function (text) {
    var lines = text.trim().split('\n').map(function (l) { return l.trim(); });
    if (lines.length < 3) { cb(new Error('bad TLE response')); return; }
    var name = lines[0], line1 = lines[1], line2 = lines[2];
    localStorage.setItem('tleName', name);
    localStorage.setItem('tleLine1', line1);
    localStorage.setItem('tleLine2', line2);
    localStorage.setItem('tleNoradId', String(noradId));
    localStorage.setItem('tleFetchedAt', String(Date.now()));
    cb(null, { name: name, line1: line1, line2: line2 });
  }, function (err) {
    cb(err);
  });
}

function computeBatch(tle) {
  var satrec = satellite.twoline2satrec(tle.line1, tle.line2);
  var nowMs = Date.now();

  function sampleAt(offsetMin) {
    var date = new Date(nowMs + offsetMin * 60000);
    var pv = satellite.propagate(satrec, date);
    if (!pv || !pv.position) return null; // decayed/invalid TLE for this offset
    var gmst = satellite.gstime(date);
    var geo = satellite.eciToGeodetic(pv.position, gmst);
    return {
      lat: Math.round(satellite.degreesLat(geo.latitude) * 100),
      lon: Math.round(satellite.degreesLong(geo.longitude) * 100),
      alt: Math.max(0, Math.round(geo.height))
    };
  }

  // Walk outward from "now" on each side so a TLE that can't quite cover
  // the full window still keeps as much of the near-term history/forecast
  // as possible, dropping only the far edge.
  var pastRev = []; // nearest-to-now first (offset -1, -2, ...)
  var p;
  for (p = 1; p <= PAST_MIN; p++) {
    var ps = sampleAt(-p);
    if (!ps) break;
    pastRev.push(ps);
  }
  var pastSamples = pastRev.reverse(); // chronological: oldest (-PAST_MIN) ... -1

  var futureSamples = [];
  var f;
  for (f = 0; f <= FUTURE_MIN; f++) {
    var fs = sampleAt(f);
    if (!fs) break;
    futureSamples.push(fs);
  }

  var nowIndex = pastSamples.length;
  var all = pastSamples.concat(futureSamples);

  return {
    name: tle.name.substring(0, 19),
    startEpoch: Math.floor((nowMs - pastSamples.length * 60000) / 1000),
    nowIndex: nowIndex,
    lats: all.map(function (s) { return s.lat; }),
    lons: all.map(function (s) { return s.lon; }),
    alts: all.map(function (s) { return s.alt; })
  };
}

function sendBatch(batch) {
  var dict = {
    'SatName': batch.name,
    'BatchStartEpoch': batch.startEpoch,
    'BatchCount': batch.lats.length,
    'NowIndex': batch.nowIndex,
    'LatArray': packInt16Array(batch.lats),
    'LonArray': packInt16Array(batch.lons),
    'AltArray': packInt16Array(batch.alts)
  };
  Pebble.sendAppMessage(dict,
    function () { console.log('Position window sent (' + batch.lats.length + ' entries, now @ ' + batch.nowIndex + ')'); },
    function () { console.log('Position window send failed'); }
  );
}

function computeAndSendBatch(noradId) {
  function withTLE(tle) {
    try {
      sendBatch(computeBatch(tle));
    } catch (e) {
      console.log('Propagation error: ' + e.message);
    }
  }

  if (isTLEStale(noradId)) {
    fetchTLE(noradId, function (err, tle) {
      if (err) {
        console.log('TLE fetch error: ' + err.message);
        return;
      }
      withTLE(tle);
    });
  } else {
    withTLE({
      name: localStorage.getItem('tleName'),
      line1: localStorage.getItem('tleLine1'),
      line2: localStorage.getItem('tleLine2')
    });
  }
}

Pebble.addEventListener('ready', function () {
  console.log('PebbleKit JS ready!');
  var noradId = parseInt(localStorage.getItem('lastNoradId') || String(DEFAULT_NORAD_ID), 10);
  computeAndSendBatch(noradId);
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload['REQUEST_POSITIONS']) {
    var noradId = parseInt(localStorage.getItem('lastNoradId') || String(DEFAULT_NORAD_ID), 10);
    if (e.payload['NoradId'] !== undefined) {
      var incomingId = e.payload['NoradId'];
      if (String(incomingId) !== localStorage.getItem('lastNoradId')) {
        localStorage.setItem('lastNoradId', String(incomingId));
        localStorage.removeItem('tleFetchedAt'); // force refetch — ID changed
      }
      noradId = incomingId;
    }
    computeAndSendBatch(noradId);
  }
});
