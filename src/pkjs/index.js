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
  displayMode: 1      // 0=low, 1=med, 2=high
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
// WEATHER API (Open-Meteo, free, no key)
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

function fetchWeather(lat, lng, callback) {
  var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat +
            '&longitude=' + lng +
            '&current=temperature_2m,weather_code,wind_speed_10m' +
            '&temperature_unit=fahrenheit' +
            '&wind_speed_unit=mph';

  xhrRequest(url, 'GET', function (responseText) {
    try {
      var json = JSON.parse(responseText);
      var temp = Math.round(json.current.temperature_2m);
      var wxCode = wmoToSimpleCode(json.current.weather_code);
      var wind = json.current.wind_speed_10m;

      // Override to "windy" if wind > 20mph and not raining/storming
      if (wind > 20 && wxCode < 4) wxCode = 7;

      console.log('Weather: ' + temp + 'F, code=' + wxCode + ', wind=' + wind + 'mph');
      callback({ temperature: temp, weatherCode: wxCode });
    } catch (e) {
      console.log('Weather parse error: ' + e);
      callback({ temperature: 0, weatherCode: 0 });
    }
  }, function () {
    callback({ temperature: 0, weatherCode: 0 });
  });
}

// ============================================================================
// MAIN DATA FETCH ORCHESTRATOR
// ============================================================================
function fetchAllData() {
  console.log('Fetching data for zip: ' + settings.zipCode);

  geocodeZip(settings.zipCode, function (lat, lng, placeName) {
    console.log('Geocoded to: ' + lat + ', ' + lng + ' (' + placeName + ')');

    // Fetch all three data sources in parallel
    var sunData = null, tideResult = null, wxData = null;
    var pending = 3;

    function trySend() {
      pending--;
      if (pending > 0) return;

      // All data ready, send to watch
      var message = {
        'TIDE_HEIGHT': tideResult ? tideResult.tideHeightPct : 50,
        'TIDE_STATE': tideResult ? tideResult.tideState : 1,
        'SUNRISE_HOUR': sunData ? sunData.sunriseHour : 6,
        'SUNRISE_MIN': sunData ? sunData.sunriseMin : 0,
        'SUNSET_HOUR': sunData ? sunData.sunsetHour : 19,
        'SUNSET_MIN': sunData ? sunData.sunsetMin : 0,
        'NEXT_HIGH_HOUR': tideResult ? tideResult.nextHigh.hour : 0,
        'NEXT_HIGH_MIN': tideResult ? tideResult.nextHigh.min : 0,
        'NEXT_LOW_HOUR': tideResult ? tideResult.nextLow.hour : 0,
        'NEXT_LOW_MIN': tideResult ? tideResult.nextLow.min : 0,
        'PREV_TIDE_HOUR': tideResult ? tideResult.prevTideTime.hour : 0,
        'PREV_TIDE_MIN': tideResult ? tideResult.prevTideTime.min : 0,
        'DISPLAY_MODE': settings.displayMode,
        'TEMPERATURE': wxData ? wxData.temperature : 0,
        'WEATHER_CODE': wxData ? wxData.weatherCode : 0,
        'TOWN_NAME': placeName || ''
      };

      Pebble.sendAppMessage(message,
        function () { console.log('All data sent to watch'); },
        function (e) { console.log('Error sending data'); }
      );
    }

    // 1. Sunrise/sunset
    fetchSunriseSunset(lat, lng, function (data) {
      sunData = data;
      console.log('Sun: rise=' + data.sunriseHour + ':' + data.sunriseMin +
                  ' set=' + data.sunsetHour + ':' + data.sunsetMin);
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

    // 3. Weather
    fetchWeather(lat, lng, function (data) {
      wxData = data;
      trySend();
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

// Config page embedded as data URI to avoid cross-origin issues with CloudPebble
function buildConfigUrl() {
  var zip = settings.zipCode || '08226';
  var station = settings.stationId || '';
  var mode = settings.displayMode || 1;

  // Inline HTML config page
  var html = '<!DOCTYPE html><html><head>' +
    '<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<title>Pixel Tides</title><style>' +
    '*{box-sizing:border-box;margin:0;padding:0}' +
    'body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#eee;min-height:100vh;padding:20px}' +
    '.hdr{text-align:center;padding:20px 0}.hdr h1{font-size:24px;color:#4fc3f7}' +
    '.hdr p{color:#888;font-size:14px;margin-top:4px}' +
    '.card{background:#16213e;border-radius:12px;padding:20px;margin:16px 0}' +
    '.card h2{font-size:16px;color:#4fc3f7;margin-bottom:12px}' +
    'label{display:block;font-size:14px;color:#aaa;margin-bottom:6px}' +
    'input[type=text]{width:100%;padding:12px;font-size:16px;background:#0f3460;border:1px solid #4fc3f7;border-radius:8px;color:#fff;margin-bottom:12px}' +
    '.hint{font-size:12px;color:#666;margin-top:-8px;margin-bottom:12px}' +
    '.btn{display:block;width:100%;padding:14px;font-size:16px;font-weight:bold;border:none;border-radius:8px;cursor:pointer;margin-top:20px}' +
    '.btn-s{background:#4fc3f7;color:#1a1a2e}.btn-c{background:#333;color:#aaa;margin-top:10px}' +
    '.tg{display:flex;border-radius:8px;overflow:hidden;border:1px solid #4fc3f7;margin-bottom:12px}' +
    '.tg label{flex:1;text-align:center;padding:12px;font-size:14px;font-weight:bold;color:#aaa;background:#0f3460;cursor:pointer;margin-bottom:0}' +
    '.tg input[type=radio]{display:none}.tg input[type=radio]:checked+label{background:#4fc3f7;color:#1a1a2e}' +
    '</style></head><body>' +
    '<div class="hdr"><div style="font-size:32px">&#x1F30A;</div><h1>Pixel Tides</h1><p>Configure your tide location</p></div>' +
    '<div class="card"><h2>Location</h2><label for="z">ZIP Code</label>' +
    '<input type="text" id="z" placeholder="e.g. 08226" maxlength="10" value="' + zip + '">' +
    '<p class="hint">US ZIP code for nearest NOAA tide station</p></div>' +
    '<div class="card"><h2>Detail Level</h2><div class="tg">' +
    '<input type="radio" name="m" id="m0" value="0"' + (mode==0?' checked':'') + '><label for="m0">Low</label>' +
    '<input type="radio" name="m" id="m1" value="1"' + (mode==1?' checked':'') + '><label for="m1">Med</label>' +
    '<input type="radio" name="m" id="m2" value="2"' + (mode==2?' checked':'') + '><label for="m2">High</label>' +
    '</div><p class="hint">Low: time + weather. Med: + tide info. High: + sunrise/sunset, town, battery %.</p></div>' +
    '<div class="card"><h2>Advanced (Optional)</h2><label for="s">NOAA Station ID</label>' +
    '<input type="text" id="s" placeholder="e.g. 8534720" value="' + station + '">' +
    '<p class="hint">Override auto-detection</p></div>' +
    '<button class="btn btn-s" id="sv">Save Settings</button>' +
    '<button class="btn btn-c" id="cn">Cancel</button>' +
    '<script>' +
    'document.getElementById("sv").addEventListener("click",function(){' +
    'var md=document.querySelector("input[name=m]:checked");' +
    'var r={zipCode:document.getElementById("z").value.trim()||"08226",' +
    'stationId:document.getElementById("s").value.trim(),' +
    'displayMode:md?md.value:"1"};' +
    'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(r));' +
    '});' +
    'document.getElementById("cn").addEventListener("click",function(){' +
    'document.location="pebblejs://close";' +
    '});' +
    '</script></body></html>';

  return 'data:text/html,' + encodeURIComponent(html);
}

Pebble.addEventListener('showConfiguration', function () {
  var url = buildConfigUrl();
  console.log('Opening inline config page');
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
      console.log('Settings updated: zip=' + settings.zipCode + ' mode=' + settings.displayMode);

      // Send display mode immediately, then fetch fresh data
      Pebble.sendAppMessage({'DISPLAY_MODE': settings.displayMode},
        function() { console.log('Display mode sent'); },
        function() { console.log('Display mode send failed'); }
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
