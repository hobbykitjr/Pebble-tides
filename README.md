# Pixel Tides

A pixel art beach watchface for the **Pebble Round 2** (gabbro, 260x260) with real-time tide data and sunrise/sunset tracking.

## Features

- **Pixel art tropical beach scene** - Sky, ocean with layered waves, sandy beach
- **Real tide data** - Water level rises and falls based on NOAA tide predictions
- **Sunrise/sunset tracking** - Sun moves across a sky arc matching real sun times; sky colors shift at dawn/dusk; stars at night
- **Wave animation** - Brief animated waves every 5 minutes and on wrist tap (battery-efficient)
- **Configurable location** - Set your ZIP code in the settings page for local tide/sun data
- **Offline caching** - Last-known data persisted on watch for use when phone is disconnected

## Data Sources

- **Tides**: [NOAA Tides & Currents API](https://tidesandcurrents.noaa.gov/) (US stations, free)
- **Sunrise/Sunset**: [sunrise-sunset.org API](https://sunrise-sunset.org/api) (free)
- **Geocoding**: [Open-Meteo Geocoding](https://open-meteo.com/) (free)

## Settings

Open the watchface settings from the Pebble app on your phone:

- **ZIP Code** - Your US ZIP code (finds nearest NOAA tide station automatically)
- **NOAA Station ID** - (Optional) Override with a specific station ID from [tidesandcurrents.noaa.gov](https://tidesandcurrents.noaa.gov/)

## Building

### With Pebble SDK (local)
```bash
pebble build
pebble install --emulator gabbro
```

### With GitHub Actions (cloud)
Push to the `main` branch. The CI workflow builds a `.pbw` artifact automatically.

### With CloudPebble
Import this repo at [cloudpebble.repebble.com](https://cloudpebble.repebble.com).

## Project Structure

```
pebble-tides/
  src/c/main.c           # Watchface C code (drawing, data handling)
  src/pkjs/index.js      # Phone-side JS (API calls, settings)
  config/index.html      # Settings page (hosted via GitHub Pages)
  package.json           # Pebble project config
  wscript                # Build script
  .github/workflows/     # CI build pipeline
  .claude/skills/        # Pebble watchface agent skill
```

## Version Roadmap

- **v1.0** - Pixel beach scene with tide/sunrise data (current)
- **v2.0** - Weather-reactive sky (rain, clouds, storms)

## License

MIT
