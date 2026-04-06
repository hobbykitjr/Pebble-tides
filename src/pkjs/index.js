/**
 * Pixel Tides - PebbleKit JS
 *
 * Handles:
 * - Clay settings (zip code / NOAA station ID)
 * - Geocoding zip code to lat/lng
 * - NOAA Tides & Currents API for tide data
 * - Sunrise-Sunset API for sun times
 * - Sends all data to watch via AppMessage
 */

// Default zip code (updated via Clay settings)
var settings = {
  zipCode: '08226',  // Ocean City, NJ (default - has nearby NOAA tide station)
  stationId: '',      // Optional NOAA station override
  displayMode: 0      // 0 = minimal, 1 = detailed
};

// ============================================================================
// HTTP HELPER
// ============================================================================
function xhrRequest(url, type, callback, errorCallback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    if (xhr.status === 200) {
      callback(this.responseText);
    } else {
      console.log('HTTP error: ' + xhr.status + ' for ' + url);
      if (errorCallback) errorCallback('HTTP ' + xhr.status);
    }
  };
  xhr.onerror = function () {
    console.log('XHR error for ' + url);
    if (errorCallback) errorCallback('XHR error');
  };
  xhr.open(type, url);
  xhr.send();
}

// ============================================================================
// GEOCODING: ZIP CODE -> LAT/LNG
// ============================================================================

// Check if input looks like a US ZIP code (5 digits or 5+4)
function isUSZip(str) {
  return /^\d{5}(-\d{4})?$/.test(str.trim());
}

function geocodeZip(zipCode, callback) {
  if (isUSZip(zipCode)) {
    // For US ZIP codes, use zippopotam.us directly (reliable for ZIPs)
    console.log('Geocoding US ZIP: ' + zipCode);
    geocodeWithZippopotamus(zipCode, callback);
  } else {
    // For non-ZIP input (city name, etc.), try Open-Meteo
    geocodeWithOpenMeteo(zipCode, callback);
  }
}

function geocodeWithZippopotamus(zipCode, callback) {
  var url = 'https://api.zippopotam.us/us/' + zipCode.trim();
  xhrRequest(url, 'GET', function (responseText) {
    try {
      var json = JSON.parse(responseText);
      var lat = parseFloat(json.places[0].latitude);
      var lng = parseFloat(json.places[0].longitude);
      var name = json.places[0]['place name'] + ', ' + json.places[0]['state abbreviation'];
      console.log('Zippopotam.us resolved: ' + name + ' (' + lat + ', ' + lng + ')');
      callback(lat, lng, name);
    } catch (e) {
      console.log('Zippopotam.us failed, trying Open-Meteo: ' + e);
      geocodeWithOpenMeteo(zipCode, callback);
    }
  }, function () {
    console.log('Zippopotam.us error, trying Open-Meteo');
    geocodeWithOpenMeteo(zipCode, callback);
  });
}

function geocodeWithOpenMeteo(query, callback) {
  var url = 'https://geocoding-api.open-meteo.com/v1/search?name=' +
            encodeURIComponent(query) + '&count=1&language=en&format=json';

  xhrRequest(url, 'GET', function (responseText) {
    try {
      var json = JSON.parse(responseText);
      if (json.results && json.results.length > 0) {
        var result = json.results[0];
        callback(result.latitude, result.longitude, result.name);
      } else {
        console.log('Open-Meteo: no results for ' + query);
      }
    } catch (e) {
      console.log('Open-Meteo geocode error: ' + e);
    }
  });
}

// ============================================================================
// NOAA TIDES API
// ============================================================================

// Find nearest NOAA tide station to given coordinates
function findNearestStation(lat, lng, callback) {
  // If user provided a station ID, use it directly
  if (settings.stationId && settings.stationId.length > 0) {
    callback(settings.stationId);
    return;
  }

  // NOAA station list endpoint
  var url = 'https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json' +
            '?type=tidepredictions&units=english';

  xhrRequest(url, 'GET', function (responseText) {
    try {
      var json = JSON.parse(responseText);
      var stations = json.stations;
      var nearest = null;
      var nearestDist = Infinity;

      for (var i = 0; i < stations.length; i++) {
        var s = stations[i];
        var slat = parseFloat(s.lat);
        var slng = parseFloat(s.lng);
        // Simple distance (good enough for finding nearest)
        var dlat = slat - lat;
        var dlng = slng - lng;
        var dist = dlat * dlat + dlng * dlng;
        if (dist < nearestDist) {
          nearestDist = dist;
          nearest = s.id;
        }
      }

      if (nearest) {
        console.log('Nearest NOAA station: ' + nearest);
        callback(nearest);
      } else {
        console.log('No NOAA stations found');
      }
    } catch (e) {
      console.log('Station search error: ' + e);
    }
  });
}

function fetchTideData(stationId, callback) {
  // Get today's date in YYYYMMDD format
  var now = new Date();
  var today = now.getFullYear() +
              ('0' + (now.getMonth() + 1)).slice(-2) +
              ('0' + now.getDate()).slice(-2);

  // Get tomorrow too for next high/low
  var tomorrow = new Date(now);
  tomorrow.setDate(tomorrow.getDate() + 1);
  var tomorrowStr = tomorrow.getFullYear() +
                    ('0' + (tomorrow.getMonth() + 1)).slice(-2) +
                    ('0' + tomorrow.getDate()).slice(-2);

  // NOAA Tide Predictions API
  var url = 'https://api.tidesandcurrents.noaa.gov/api/prod/datagetter' +
            '?begin_date=' + today +
            '&end_date=' + tomorrowStr +
            '&station=' + stationId +
            '&product=predictions' +
            '&datum=MLLW' +
            '&time_zone=lst_ldt' +
            '&interval=hilo' +
            '&units=english' +
            '&format=json';

  xhrRequest(url, 'GET', function (responseText) {
    try {
      var json = JSON.parse(responseText);
      if (json.predictions && json.predictions.length > 0) {
        callback(json.predictions);
      } else {
        console.log('No tide predictions in response');
      }
    } catch (e) {
      console.log('Tide data parse error: ' + e);
    }
  });
}

function processTideData(predictions) {
  var now = new Date();
  var nowMins = now.getHours() * 60 + now.getMinutes();

  // Find the surrounding high/low tides
  var prevTide = null;
  var nextTide = null;
  var nextHigh = null;
  var nextLow = null;

  for (var i = 0; i < predictions.length; i++) {
    var p = predictions[i];
    // Parse "YYYY-MM-DD HH:MM" format
    var parts = p.t.split(' ');
    var timeParts = parts[1].split(':');
    var dateParts = parts[0].split('-');

    var predDate = new Date(
      parseInt(dateParts[0]),
      parseInt(dateParts[1]) - 1,
      parseInt(dateParts[2]),
      parseInt(timeParts[0]),
      parseInt(timeParts[1])
    );

    var predMins = predDate.getHours() * 60 + predDate.getMinutes();
    var isToday = predDate.getDate() === now.getDate();

    if (predDate <= now) {
      prevTide = {
        type: p.type,  // "H" or "L"
        time: predDate,
        height: parseFloat(p.v)
      };
    } else if (!nextTide) {
      nextTide = {
        type: p.type,
        time: predDate,
        height: parseFloat(p.v)
      };
    }

    // Track next high and low specifically
    if (predDate > now) {
      if (p.type === 'H' && !nextHigh) {
        nextHigh = { hour: predDate.getHours(), min: predDate.getMinutes() };
      }
      if (p.type === 'L' && !nextLow) {
        nextLow = { hour: predDate.getHours(), min: predDate.getMinutes() };
      }
    }
  }

  // Calculate current tide height percentage (0-100)
  var tideHeightPct = 50;  // Default midpoint
  var tideState = 1;  // Default rising

  if (prevTide && nextTide) {
    var totalTime = nextTide.time - prevTide.time;
    var elapsed = now - prevTide.time;
    var progress = totalTime > 0 ? elapsed / totalTime : 0.5;

    // Sinusoidal interpolation for natural tide curve
    var sinProgress = (1 - Math.cos(progress * Math.PI)) / 2;

    if (prevTide.type === 'L') {
      // Rising: low -> high
      tideHeightPct = Math.round(sinProgress * 100);
      tideState = 1;
    } else {
      // Falling: high -> low
      tideHeightPct = Math.round((1 - sinProgress) * 100);
      tideState = 0;
    }
  }

  return {
    tideHeightPct: tideHeightPct,
    tideState: tideState,
    nextHigh: nextHigh || { hour: 0, min: 0 },
    nextLow: nextLow || { hour: 0, min: 0 }
  };
}

// ============================================================================
// SUNRISE/SUNSET API
// ============================================================================
// Parse "h:mm:ss AM/PM" format from sunrise-sunset.org (formatted=1)
function parseSunTime(timeStr) {
  // e.g. "6:23:45 AM" or "7:45:12 PM"
  var parts = timeStr.trim().split(' ');
  var timeParts = parts[0].split(':');
  var hour = parseInt(timeParts[0]);
  var min = parseInt(timeParts[1]);
  var ampm = parts[1];

  if (ampm === 'PM' && hour !== 12) hour += 12;
  if (ampm === 'AM' && hour === 12) hour = 0;

  return { hour: hour, min: min };
}

function fetchSunriseSunset(lat, lng, callback) {
  // formatted=1 returns times in location's local timezone as "h:mm:ss AM/PM"
  var url = 'https://api.sunrise-sunset.org/json?lat=' + lat +
            '&lng=' + lng + '&formatted=1&date=today';

  xhrRequest(url, 'GET', function (responseText) {
    try {
      var json = JSON.parse(responseText);
      if (json.status === 'OK') {
        var sunrise = parseSunTime(json.results.sunrise);
        var sunset = parseSunTime(json.results.sunset);

        console.log('Sun times (local): rise=' + sunrise.hour + ':' +
                    sunrise.min + ' set=' + sunset.hour + ':' + sunset.min);

        callback({
          sunriseHour: sunrise.hour,
          sunriseMin: sunrise.min,
          sunsetHour: sunset.hour,
          sunsetMin: sunset.min
        });
      }
    } catch (e) {
      console.log('Sunrise/sunset parse error: ' + e);
    }
  });
}

// ============================================================================
// MAIN DATA FETCH ORCHESTRATOR
// ============================================================================
function fetchAllData() {
  console.log('Fetching data for zip: ' + settings.zipCode);

  geocodeZip(settings.zipCode, function (lat, lng, placeName) {
    console.log('Geocoded to: ' + lat + ', ' + lng + ' (' + placeName + ')');

    // Fetch sunrise/sunset
    fetchSunriseSunset(lat, lng, function (sunData) {
      console.log('Sunrise: ' + sunData.sunriseHour + ':' + sunData.sunriseMin);
      console.log('Sunset: ' + sunData.sunsetHour + ':' + sunData.sunsetMin);

      // Fetch tide data
      findNearestStation(lat, lng, function (stationId) {
        fetchTideData(stationId, function (predictions) {
          var tideResult = processTideData(predictions);
          console.log('Tide height: ' + tideResult.tideHeightPct + '%, ' +
                      (tideResult.tideState ? 'rising' : 'falling'));

          // Send everything to the watch
          var message = {
            'TIDE_HEIGHT': tideResult.tideHeightPct,
            'TIDE_STATE': tideResult.tideState,
            'SUNRISE_HOUR': sunData.sunriseHour,
            'SUNRISE_MIN': sunData.sunriseMin,
            'SUNSET_HOUR': sunData.sunsetHour,
            'SUNSET_MIN': sunData.sunsetMin,
            'NEXT_HIGH_HOUR': tideResult.nextHigh.hour,
            'NEXT_HIGH_MIN': tideResult.nextHigh.min,
            'NEXT_LOW_HOUR': tideResult.nextLow.hour,
            'NEXT_LOW_MIN': tideResult.nextLow.min,
            'DISPLAY_MODE': settings.displayMode
          };

          Pebble.sendAppMessage(message,
            function () { console.log('Data sent to watch successfully'); },
            function (e) { console.log('Error sending data to watch'); }
          );
        });
      });
    });
  });
}

// ============================================================================
// SETTINGS (Clay)
// ============================================================================
function loadSettings() {
  try {
    var saved = localStorage.getItem('settings');
    if (saved) {
      var parsed = JSON.parse(saved);
      if (parsed.zipCode) settings.zipCode = parsed.zipCode;
      if (parsed.stationId) settings.stationId = parsed.stationId;
      if (parsed.displayMode !== undefined) settings.displayMode = parsed.displayMode;
    }
  } catch (e) {
    console.log('Error loading settings: ' + e);
  }
}

function saveSettings() {
  localStorage.setItem('settings', JSON.stringify(settings));
}

// Clay configuration page
Pebble.addEventListener('showConfiguration', function () {
  var clayUrl = 'https://hobbykitjr.github.io/Pebble-tides/config/index.html';

  // Append current settings as URL params
  var url = clayUrl + '?zip=' + encodeURIComponent(settings.zipCode) +
            '&station=' + encodeURIComponent(settings.stationId || '') +
            '&mode=' + settings.displayMode;

  console.log('Opening config: ' + url);
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (e && e.response) {
    try {
      var config = JSON.parse(decodeURIComponent(e.response));
      if (config.zipCode) settings.zipCode = config.zipCode;
      if (config.stationId !== undefined) settings.stationId = config.stationId;
      if (config.displayMode !== undefined) settings.displayMode = parseInt(config.displayMode);
      saveSettings();
      console.log('Settings updated, zip: ' + settings.zipCode);

      // Check if dev mode values were sent
      if (config.devMode) {
        console.log('Dev mode: sending manual test values');
        var devMessage = {
          'TIDE_HEIGHT': config.tideHeight,
          'TIDE_STATE': config.tideState,
          'SUNRISE_HOUR': config.sunriseHour,
          'SUNRISE_MIN': config.sunriseMin,
          'SUNSET_HOUR': config.sunsetHour,
          'SUNSET_MIN': config.sunsetMin,
          'NEXT_HIGH_HOUR': config.nextHighHour,
          'NEXT_HIGH_MIN': config.nextHighMin,
          'NEXT_LOW_HOUR': config.nextLowHour,
          'NEXT_LOW_MIN': config.nextLowMin,
          'DISPLAY_MODE': settings.displayMode
        };
        Pebble.sendAppMessage(devMessage,
          function () { console.log('Dev values sent to watch'); },
          function (e) { console.log('Error sending dev values'); }
        );
      } else {
        // Normal: fetch new data with updated location
        fetchAllData();
      }
    } catch (err) {
      console.log('Config parse error: ' + err);
    }
  }
});

// ============================================================================
// EVENT LISTENERS
// ============================================================================
Pebble.addEventListener('ready', function () {
  console.log('PebbleKit JS ready!');
  loadSettings();
  fetchAllData();
});

Pebble.addEventListener('appmessage', function (e) {
  console.log('AppMessage received from watch');
  if (e.payload['REQUEST_DATA']) {
    fetchAllData();
  }
});
