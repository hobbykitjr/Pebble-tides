/**
 * Pixel Tides - PebbleKit JS
 *
 * Handles:
 * - Clay settings (zip code / NOAA station ID)
 * - Geocoding zip code to lat/lng
 * - NOAA Tides & Currents API for tide data
 * - Open-Meteo for weather + sunrise/sunset (timezone-aware)
 * - Sends all data to watch via AppMessage
 */

// Default zip code (updated via Clay settings)
var settings = {
  zipCode: '08226',  // Ocean City, NJ (default - has nearby NOAA tide station)
  stationId: '',      // Optional NOAA station override
  displayMode: 1,     // 0=low, 1=med, 2=high
  largeFont: 0,       // 0=off, 1=on (bigger tide/sun text, 1 row)
  devMode: 0          // 0=off, 1=on (enables preset cycling via tap)
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

  // Previous tide time (for "X ago" display)
  var prevTideTime = { hour: 0, min: 0 };
  if (prevTide) {
    prevTideTime = { hour: prevTide.time.getHours(), min: prevTide.time.getMinutes() };
  }

  return {
    tideHeightPct: tideHeightPct,
    tideState: tideState,
    nextHigh: nextHigh || { hour: 0, min: 0 },
    nextLow: nextLow || { hour: 0, min: 0 },
    prevTideTime: prevTideTime
  };
}

// ============================================================================
// WEATHER + SUNRISE/SUNSET API (Open-Meteo, free, no key)
// ============================================================================
function wmoToSimpleCode(wmo) {
  // Map WMO weather codes to our simplified icons
  if (wmo === 0) return 0;          // Clear
  if (wmo <= 2) return 1;           // Cloudy (few/scattered)
  if (wmo === 3) return 2;          // Overcast
  if (wmo <= 48) return 3;          // Fog
  if (wmo <= 65) return 4;          // Rain (drizzle, rain)
  if (wmo <= 67) return 4;          // Freezing rain
  if (wmo <= 77) return 6;          // Snow
  if (wmo <= 82) return 4;          // Showers
  if (wmo <= 86) return 6;          // Snow showers
  if (wmo >= 95) return 5;          // Thunderstorm
  return 1;                          // Default: cloudy
}

function fetchWeatherAndSun(lat, lng, callback) {
  // Combined call: current weather + daily sunrise/sunset
  // timezone=auto uses lat/lng to return correct local times
  var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat +
            '&longitude=' + lng +
            '&current=temperature_2m,weather_code,wind_speed_10m,uv_index' +
            '&daily=sunrise,sunset,uv_index_max' +
            '&temperature_unit=fahrenheit' +
            '&wind_speed_unit=mph' +
            '&timezone=auto' +
            '&forecast_days=1';

  xhrRequest(url, 'GET', function (responseText) {
    try {
      var json = JSON.parse(responseText);
      var temp = Math.round(json.current.temperature_2m);
      var wxCode = wmoToSimpleCode(json.current.weather_code);
      var wind = json.current.wind_speed_10m;

      // Override to "windy" if wind > 20mph and not raining/storming
      if (wind > 20 && wxCode < 4) wxCode = 7;

      // UV index: use current if daytime, daily max as fallback
      var uvIndex = Math.round(json.current.uv_index || 0);
      var uvMax = Math.round(json.daily.uv_index_max[0] || 0);

      // Parse sunrise/sunset from ISO local time strings (e.g. "2026-04-07T06:31")
      var srParts = json.daily.sunrise[0].split('T')[1].split(':');
      var ssParts = json.daily.sunset[0].split('T')[1].split(':');

      console.log('Weather: ' + temp + 'F, code=' + wxCode + ', wind=' + wind +
                  'mph, UV=' + uvIndex + ' (max ' + uvMax + ')');
      console.log('Sun (local): rise=' + srParts[0] + ':' + srParts[1] +
                  ' set=' + ssParts[0] + ':' + ssParts[1]);

      callback({
        temperature: temp, weatherCode: wxCode,
        sunriseHour: parseInt(srParts[0]), sunriseMin: parseInt(srParts[1]),
        sunsetHour: parseInt(ssParts[0]), sunsetMin: parseInt(ssParts[1]),
        uvIndex: uvIndex > 0 ? uvIndex : uvMax
      });
    } catch (e) {
      console.log('Weather/sun parse error: ' + e);
      callback(null);
    }
  }, function () {
    callback(null);
  });
}

// ============================================================================
// MAIN DATA FETCH ORCHESTRATOR
// ============================================================================
function fetchAllData() {
  console.log('Fetching data for zip: ' + settings.zipCode);

  geocodeZip(settings.zipCode, function (lat, lng, placeName) {
    console.log('Geocoded to: ' + lat + ', ' + lng + ' (' + placeName + ')');

    // Fetch weather+sun and tides in parallel
    var wxSunData = null, tideResult = null;
    var pending = 2;

    function trySend() {
      pending--;
      if (pending > 0) return;

      // Build message — only include fields we have good data for
      var message = {
        'TIDE_HEIGHT': tideResult ? tideResult.tideHeightPct : 50,
        'TIDE_STATE': tideResult ? tideResult.tideState : 1,
        'NEXT_HIGH_HOUR': tideResult ? tideResult.nextHigh.hour : 0,
        'NEXT_HIGH_MIN': tideResult ? tideResult.nextHigh.min : 0,
        'NEXT_LOW_HOUR': tideResult ? tideResult.nextLow.hour : 0,
        'NEXT_LOW_MIN': tideResult ? tideResult.nextLow.min : 0,
        'PREV_TIDE_HOUR': tideResult ? tideResult.prevTideTime.hour : 0,
        'PREV_TIDE_MIN': tideResult ? tideResult.prevTideTime.min : 0,
        'DISPLAY_MODE': settings.displayMode,
        'TOWN_NAME': placeName || ''
      };

      // Only include weather/sun if API succeeded (don't overwrite with 0)
      if (wxSunData) {
        message['TEMPERATURE'] = wxSunData.temperature;
        message['WEATHER_CODE'] = wxSunData.weatherCode;
        message['SUNRISE_HOUR'] = wxSunData.sunriseHour;
        message['SUNRISE_MIN'] = wxSunData.sunriseMin;
        message['SUNSET_HOUR'] = wxSunData.sunsetHour;
        message['SUNSET_MIN'] = wxSunData.sunsetMin;
        message['UV_INDEX'] = wxSunData.uvIndex;
      }

      Pebble.sendAppMessage(message,
        function () { console.log('All data sent to watch'); },
        function (e) { console.log('Error sending data'); }
      );
    }

    // 1. Weather + sunrise/sunset (combined Open-Meteo call)
    fetchWeatherAndSun(lat, lng, function (data) {
      wxSunData = data;  // null on failure
      trySend();
    });

    // 2. Tides
    findNearestStation(lat, lng, function (stationId) {
      fetchTideData(stationId, function (predictions) {
        tideResult = processTideData(predictions);
        console.log('Tide: ' + tideResult.tideHeightPct + '% ' +
                    (tideResult.tideState ? 'rising' : 'falling'));
        trySend();
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
      if (parsed.largeFont !== undefined) settings.largeFont = parsed.largeFont;
      if (parsed.devMode !== undefined) settings.devMode = parsed.devMode;
    }
  } catch (e) {
    console.log('Error loading settings: ' + e);
  }
}

function saveSettings() {
  localStorage.setItem('settings', JSON.stringify(settings));
}

// Config page hosted on GitHub Pages
Pebble.addEventListener('showConfiguration', function () {
  var url = 'https://hobbykitjr.github.io/Pebble-tides/config/index.html' +
    '?zip=' + encodeURIComponent(settings.zipCode) +
    '&station=' + encodeURIComponent(settings.stationId || '') +
    '&mode=' + settings.displayMode +
    '&lgfont=' + settings.largeFont +
    '&dev=' + settings.devMode;
  console.log('Opening config: ' + url);
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function (e) {
  console.log('webviewclosed fired! response: ' + (e ? JSON.stringify(e.response) : 'null'));
  if (e && e.response && e.response.length > 0) {
    try {
      // CloudPebble may return "response=ENCODED_JSON" — strip prefix
      var rawResponse = e.response;
      if (rawResponse.indexOf('response=') === 0) {
        rawResponse = rawResponse.substring(9);
      }
      var config = JSON.parse(decodeURIComponent(rawResponse));
      if (config.zipCode) settings.zipCode = config.zipCode;
      if (config.stationId !== undefined) settings.stationId = config.stationId;
      if (config.displayMode !== undefined) settings.displayMode = parseInt(config.displayMode);
      if (config.largeFont !== undefined) settings.largeFont = parseInt(config.largeFont);
      if (config.devMode !== undefined) settings.devMode = parseInt(config.devMode);
      saveSettings();
      console.log('Settings updated: zip=' + settings.zipCode + ' mode=' + settings.displayMode + ' lgfont=' + settings.largeFont + ' dev=' + settings.devMode);

      // Send display mode + dev mode immediately, then fetch fresh data
      Pebble.sendAppMessage({'DISPLAY_MODE': settings.displayMode, 'LARGE_FONT': settings.largeFont, 'DEV_MODE': settings.devMode},
        function() { console.log('Settings sent'); },
        function() { console.log('Settings send failed'); }
      );
      fetchAllData();
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
