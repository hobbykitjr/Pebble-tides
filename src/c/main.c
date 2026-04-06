/**
 * Pixel Tides - Pebble Round 2 (Gabbro) Watchface
 *
 * Pixel art beach scene: orange sun arc, weather icon + temp at top,
 * time/date at horizon with sunrise/sunset flanking, ocean with waves
 * flowing toward shore, sand with tide info at bottom.
 *
 * Target: gabbro (260x260, round, 64-color)
 */

#include <pebble.h>

// ============================================================================
// CONSTANTS
// ============================================================================

#define NUM_WAVES 4
#define WAVE_ANIM_INTERVAL 60
#define WAVE_ANIM_DURATION 5000
#define LOW_BATTERY_THRESHOLD 20

// Layout zones (pct of 260px height)
// On a 260px round screen, center usable area is roughly 40-220 vertically
#define HORIZON_Y_PCT     58   // Horizon / time row — slightly below center
#define TIDE_SHIFT_PX     25   // Max tide shift above horizon for high tide

// Sun arc (invisible path, only the sun dot is drawn)
#define SUN_RADIUS        10
#define SUN_ARC_TOP_Y_PCT 12   // Top of invisible arc
#define SUN_ARC_BOT_Y_PCT 55   // Bottom of arc (at horizon)

// Display modes
#define DISPLAY_MODE_MINIMAL  0
#define DISPLAY_MODE_DETAILED 1

// Persist keys
#define PERSIST_TIDE_HEIGHT    0
#define PERSIST_TIDE_STATE     1
#define PERSIST_SUNRISE_HOUR   2
#define PERSIST_SUNRISE_MIN    3
#define PERSIST_SUNSET_HOUR    4
#define PERSIST_SUNSET_MIN     5
#define PERSIST_NEXT_HIGH_HOUR 6
#define PERSIST_NEXT_HIGH_MIN  7
#define PERSIST_NEXT_LOW_HOUR  8
#define PERSIST_NEXT_LOW_MIN   9
#define PERSIST_DATA_VALID     10
#define PERSIST_DISPLAY_MODE   11
#define PERSIST_TEMPERATURE    12
#define PERSIST_WEATHER_CODE   13
#define PERSIST_PREV_TIDE_HOUR 14
#define PERSIST_PREV_TIDE_MIN  15

// Dev mode
#define DEV_MODE 1
#define NUM_TEST_PRESETS 6

// Weather icon codes (simplified from WMO)
#define WX_CLEAR    0
#define WX_CLOUDY   1
#define WX_OVERCAST 2
#define WX_FOG      3
#define WX_RAIN     4
#define WX_STORM    5
#define WX_SNOW     6
#define WX_WIND     7

// ============================================================================
// COLORS
// ============================================================================
#ifdef PBL_COLOR
  #define COLOR_SKY_DAY       GColorPictonBlue
  #define COLOR_SKY_DAY_LOW   GColorCeleste
  #define COLOR_SKY_DAWN      GColorMelon
  #define COLOR_SKY_DUSK      GColorOrange
  #define COLOR_SKY_NIGHT     GColorOxfordBlue
  #define COLOR_SUN           GColorYellow
  #define COLOR_SUN_ARC       GColorOrange
  #define COLOR_OCEAN_DEEP    GColorCobaltBlue
  #define COLOR_OCEAN_MID     GColorBlue
  #define COLOR_OCEAN_LIGHT   GColorVividCerulean
  #define COLOR_OCEAN_FOAM    GColorCeleste
  #define COLOR_SAND          GColorFromHEX(0xD4B070)
  #define COLOR_SAND_DARK     GColorFromHEX(0xB49650)
  #define COLOR_SAND_WET      GColorFromHEX(0x907040)
  #define COLOR_TEXT           GColorWhite
  #define COLOR_TEXT_SHADOW    GColorBlack
  #define COLOR_TEXT_INFO      GColorCeleste
  #define COLOR_WEATHER_ICON  GColorWhite
#else
  #define COLOR_SKY_DAY       GColorWhite
  #define COLOR_SKY_DAY_LOW   GColorWhite
  #define COLOR_SKY_DAWN      GColorWhite
  #define COLOR_SKY_DUSK      GColorLightGray
  #define COLOR_SKY_NIGHT     GColorBlack
  #define COLOR_SUN           GColorWhite
  #define COLOR_SUN_ARC       GColorLightGray
  #define COLOR_OCEAN_DEEP    GColorBlack
  #define COLOR_OCEAN_MID     GColorBlack
  #define COLOR_OCEAN_LIGHT   GColorDarkGray
  #define COLOR_OCEAN_FOAM    GColorWhite
  #define COLOR_SAND          GColorLightGray
  #define COLOR_SAND_DARK     GColorDarkGray
  #define COLOR_SAND_WET      GColorDarkGray
  #define COLOR_TEXT           GColorBlack
  #define COLOR_TEXT_SHADOW    GColorWhite
  #define COLOR_TEXT_INFO      GColorBlack
  #define COLOR_WEATHER_ICON  GColorBlack
#endif

// ============================================================================
// DATA STRUCTURES
// ============================================================================
typedef struct {
  int base_y;
  int32_t phase;
  int amplitude;
  int speed;        // phase increment per frame
  int y_speed;      // vertical oscillation speed (toward shore)
  GColor color;
} Wave;

typedef struct {
  int sunrise_hour, sunrise_min;
  int sunset_hour, sunset_min;
  int tide_height_pct;  // 0=low, 100=high
  int tide_state;       // 0=falling, 1=rising
  int next_high_hour, next_high_min;
  int next_low_hour, next_low_min;
  int prev_tide_hour, prev_tide_min;  // Time of most recent tide event
  int temperature;      // degrees F
  int weather_code;     // WX_* constant
  bool data_valid;
} TideData;

// ============================================================================
// GLOBALS
// ============================================================================
static Window *s_main_window;
static Layer *s_canvas_layer;
static AppTimer *s_animation_timer;

static int s_battery_level = 100;
static bool s_is_animating = false;
static int s_anim_elapsed = 0;

static Wave s_waves[NUM_WAVES];
static TideData s_tide = {
  .sunrise_hour = 6, .sunrise_min = 15,
  .sunset_hour = 19, .sunset_min = 45,
  .tide_height_pct = 50, .tide_state = 1,
  .next_high_hour = 12, .next_high_min = 0,
  .next_low_hour = 18, .next_low_min = 0,
  .prev_tide_hour = 6, .prev_tide_min = 0,
  .temperature = 72, .weather_code = WX_CLEAR,
  .data_valid = false
};

static char s_time_buffer[8];
static char s_date_buffer[16];
static char s_tide_line1[40];
static char s_tide_line2[40];
static char s_sunrise_buffer[8];
static char s_sunset_buffer[8];
static char s_temp_buffer[8];

static int s_display_mode = DISPLAY_MODE_DETAILED;
static int s_current_hour = 12;
static int s_current_min = 0;

// ============================================================================
// DEV MODE
// ============================================================================
#if DEV_MODE
static int s_test_preset = -1;
static const char *s_preset_names[] = {
  "Dawn Rise", "Morn Hi", "Noon Mid",
  "Dusk Low", "Night Fall", "Night Rise"
};

typedef struct {
  int hour, min;
  int sr_h, sr_m, ss_h, ss_m;
  int tide_pct, tide_state;
  int nh_h, nh_m, nl_h, nl_m;
  int pt_h, pt_m;  // previous tide time
  int temp, wx;
} TestPreset;

static const TestPreset s_test_presets[NUM_TEST_PRESETS] = {
  //                                                     prev    temp wx
  {  6, 15,  6, 0, 19, 45,  25, 1, 10, 30, 16, 45,  4, 0,  58, WX_FOG      }, // low was 4:00
  {  9, 30,  6, 0, 19, 45,  95, 0,  9, 15, 15, 30,  9, 15, 72, WX_CLOUDY   }, // high was 9:15 (15m ago)
  { 12,  0,  6, 0, 19, 45,  50, 0, 18,  0, 15, 30,  9, 30, 85, WX_CLEAR    }, // high was 9:30
  { 19, 30,  6, 0, 19, 45,  10, 1, 22,  0, 19, 15, 19, 15, 68, WX_WIND     }, // low was 19:15 (15m ago)
  { 22,  0,  6, 0, 19, 45,  60, 0,  4, 30, 22, 30, 21, 30, 55, WX_RAIN     }, // high was 21:30 (30m ago)
  {  2, 30,  6, 0, 19, 45,  30, 1,  4, 30,  0, 15,  0, 15, 48, WX_SNOW     }, // low was 0:15
};

static void apply_test_preset(int idx) {
  if (idx < 0 || idx >= NUM_TEST_PRESETS) return;
  const TestPreset *p = &s_test_presets[idx];
  s_current_hour = p->hour; s_current_min = p->min;
  s_tide.sunrise_hour = p->sr_h; s_tide.sunrise_min = p->sr_m;
  s_tide.sunset_hour = p->ss_h; s_tide.sunset_min = p->ss_m;
  s_tide.tide_height_pct = p->tide_pct;
  s_tide.tide_state = p->tide_state;
  s_tide.next_high_hour = p->nh_h; s_tide.next_high_min = p->nh_m;
  s_tide.next_low_hour = p->nl_h; s_tide.next_low_min = p->nl_m;
  s_tide.prev_tide_hour = p->pt_h; s_tide.prev_tide_min = p->pt_m;
  s_tide.temperature = p->temp; s_tide.weather_code = p->wx;
  s_tide.data_valid = true;
  snprintf(s_time_buffer, sizeof(s_time_buffer), "%d:%02d", p->hour, p->min);
  snprintf(s_date_buffer, sizeof(s_date_buffer), "DEV:%s", s_preset_names[idx]);
}
#endif

// ============================================================================
// UTILITY
// ============================================================================
static int get_sun_progress(void) {
  int sr = s_tide.sunrise_hour * 60 + s_tide.sunrise_min;
  int ss = s_tide.sunset_hour * 60 + s_tide.sunset_min;
  int now = s_current_hour * 60 + s_current_min;
  if (now < sr || now > ss) return -1;
  int len = ss - sr;
  if (len <= 0) return 50;
  return ((now - sr) * 100) / len;
}

static bool is_nighttime(void) { return get_sun_progress() < 0; }

static int get_twilight_intensity(void) {
  int p = get_sun_progress();
  if (p < 0) return 0;
  if (p < 15) return (15 - p) * 100 / 15;
  if (p > 85) return (p - 85) * 100 / 15;
  return 0;
}

// Water line Y position (where ocean meets sand)
static int get_water_y(int screen_h) {
  int low_y = (screen_h * HORIZON_Y_PCT) / 100;   // Low tide = horizon
  int high_y = low_y - TIDE_SHIFT_PX;              // High tide = above horizon
  // Interpolate: 0% tide = low_y, 100% = high_y
  return low_y - (s_tide.tide_height_pct * TIDE_SHIFT_PX) / 100;
}

// ============================================================================
// WAVE INIT - waves flow toward shore (vertical oscillation)
// ============================================================================
static void init_waves(int screen_h) {
  int horizon = (screen_h * HORIZON_Y_PCT) / 100;

  // Waves spaced in the ocean zone, oscillate toward shore
  // Front wave (closest to shore, fastest)
  s_waves[0].base_y = horizon - 5;
  s_waves[0].phase = 0;
  s_waves[0].amplitude = 4;
  s_waves[0].speed = 350;
  s_waves[0].color = COLOR_OCEAN_FOAM;

  s_waves[1].base_y = horizon - 16;
  s_waves[1].phase = TRIG_MAX_ANGLE / 4;
  s_waves[1].amplitude = 5;
  s_waves[1].speed = 260;
  s_waves[1].color = COLOR_OCEAN_LIGHT;

  s_waves[2].base_y = horizon - 28;
  s_waves[2].phase = TRIG_MAX_ANGLE / 2;
  s_waves[2].amplitude = 4;
  s_waves[2].speed = 190;
  s_waves[2].color = COLOR_OCEAN_MID;

  // Back wave (farthest, slowest)
  s_waves[3].base_y = horizon - 42;
  s_waves[3].phase = TRIG_MAX_ANGLE * 3 / 4;
  s_waves[3].amplitude = 3;
  s_waves[3].speed = 140;
  s_waves[3].color = COLOR_OCEAN_DEEP;
}

// ============================================================================
// DRAWING: SKY + SUN ARC
// ============================================================================
static void draw_sky(GContext *ctx, GRect b) {
  int horizon = (b.size.h * HORIZON_Y_PCT) / 100;
  bool night = is_nighttime();
  int twi = get_twilight_intensity();

  if (night) {
    graphics_context_set_fill_color(ctx, COLOR_SKY_NIGHT);
    graphics_fill_rect(ctx, GRect(0, 0, b.size.w, horizon), 0, GCornerNone);
    // Stars
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorWhite);
    int stars[][2] = {
      {30,20},{80,15},{130,35},{200,25},{45,50},{160,10},{220,45},
      {100,55},{175,60},{55,35},{240,15},{15,45},{195,50},{70,8},{145,48}
    };
    for (int i = 0; i < 15; i++) {
      if (stars[i][1] < horizon)
        graphics_draw_pixel(ctx, GPoint(stars[i][0], stars[i][1]));
    }
    #endif
  } else {
    GColor upper = (twi > 50) ? COLOR_SKY_DAWN : COLOR_SKY_DAY;
    GColor lower = (twi > 30) ? COLOR_SKY_DUSK : COLOR_SKY_DAY_LOW;
    graphics_context_set_fill_color(ctx, upper);
    graphics_fill_rect(ctx, GRect(0, 0, b.size.w, horizon * 2 / 3), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, lower);
    graphics_fill_rect(ctx, GRect(0, horizon * 2 / 3, b.size.w, horizon / 3), 0, GCornerNone);
  }

  // Sun dot follows an invisible parabolic arc
  // Inset X range for round display: 20%-80% of width
  int prog = get_sun_progress();
  if (prog >= 0) {
    int arc_top = (b.size.h * SUN_ARC_TOP_Y_PCT) / 100;
    int arc_bot = (b.size.h * SUN_ARC_BOT_Y_PCT) / 100;
    int arc_h = arc_bot - arc_top;
    int sun_x = b.size.w * 20 / 100 + (b.size.w * 60 / 100 * prog) / 100;
    int centered = prog - 50;
    int sun_y = arc_bot - (arc_h * (2500 - centered * centered)) / 2500;

    // Sun glow
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorRajah);
    graphics_fill_circle(ctx, GPoint(sun_x, sun_y), SUN_RADIUS + 3);
    #endif

    // Sun body
    graphics_context_set_fill_color(ctx, COLOR_SUN);
    graphics_fill_circle(ctx, GPoint(sun_x, sun_y), SUN_RADIUS);

    // Pixel rays
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorRajah);
    int rd = SUN_RADIUS + 5;
    graphics_fill_rect(ctx, GRect(sun_x-1, sun_y-rd-3, 3, 3), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(sun_x-1, sun_y+rd, 3, 3), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(sun_x-rd-3, sun_y-1, 3, 3), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(sun_x+rd, sun_y-1, 3, 3), 0, GCornerNone);
    #endif
  }
}

// ============================================================================
// DRAWING: WEATHER ICON (pixel art)
// ============================================================================
static void draw_weather_icon(GContext *ctx, GPoint center, int code) {
  int x = center.x, y = center.y;
  graphics_context_set_fill_color(ctx, COLOR_WEATHER_ICON);

  switch (code) {
    case WX_CLOUDY:
    case WX_OVERCAST: {
      // Cloud shape
      graphics_fill_circle(ctx, GPoint(x-4, y), 5);
      graphics_fill_circle(ctx, GPoint(x+4, y-2), 6);
      graphics_fill_circle(ctx, GPoint(x+10, y), 4);
      graphics_fill_rect(ctx, GRect(x-8, y, 22, 6), 0, GCornerNone);
      if (code == WX_OVERCAST) {
        // Second cloud behind
        graphics_context_set_fill_color(ctx, GColorLightGray);
        graphics_fill_circle(ctx, GPoint(x-8, y+4), 4);
        graphics_fill_circle(ctx, GPoint(x+14, y+4), 4);
        graphics_fill_rect(ctx, GRect(x-10, y+4, 26, 4), 0, GCornerNone);
      }
      break;
    }
    case WX_FOG: {
      // Horizontal lines
      graphics_context_set_stroke_color(ctx, COLOR_WEATHER_ICON);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, GPoint(x-10, y-4), GPoint(x+10, y-4));
      graphics_draw_line(ctx, GPoint(x-8, y), GPoint(x+12, y));
      graphics_draw_line(ctx, GPoint(x-10, y+4), GPoint(x+10, y+4));
      break;
    }
    case WX_RAIN: {
      // Cloud + drops
      graphics_fill_circle(ctx, GPoint(x-3, y-4), 4);
      graphics_fill_circle(ctx, GPoint(x+5, y-5), 5);
      graphics_fill_rect(ctx, GRect(x-7, y-4, 16, 4), 0, GCornerNone);
      // Rain drops
      #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx, GColorPictonBlue);
      #endif
      graphics_fill_rect(ctx, GRect(x-5, y+2, 2, 4), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(x+1, y+4, 2, 4), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(x+7, y+2, 2, 4), 0, GCornerNone);
      break;
    }
    case WX_STORM: {
      // Cloud + lightning bolt
      graphics_fill_circle(ctx, GPoint(x-3, y-4), 4);
      graphics_fill_circle(ctx, GPoint(x+5, y-5), 5);
      graphics_fill_rect(ctx, GRect(x-7, y-4, 16, 4), 0, GCornerNone);
      #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx, GColorYellow);
      #endif
      // Lightning zigzag
      graphics_fill_rect(ctx, GRect(x+1, y+1, 3, 3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(x-1, y+4, 3, 3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(x+1, y+7, 3, 3), 0, GCornerNone);
      break;
    }
    case WX_SNOW: {
      // Cloud + snowflakes
      graphics_fill_circle(ctx, GPoint(x-3, y-4), 4);
      graphics_fill_circle(ctx, GPoint(x+5, y-5), 5);
      graphics_fill_rect(ctx, GRect(x-7, y-4, 16, 4), 0, GCornerNone);
      // Snowflakes (small crosses)
      graphics_fill_rect(ctx, GRect(x-5, y+3, 3, 1), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(x-4, y+2, 1, 3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(x+4, y+5, 3, 1), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(x+5, y+4, 1, 3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(x-1, y+6, 3, 1), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(x, y+5, 1, 3), 0, GCornerNone);
      break;
    }
    case WX_WIND: {
      // Wavy lines
      graphics_context_set_stroke_color(ctx, COLOR_WEATHER_ICON);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, GPoint(x-10, y-3), GPoint(x-2, y-3));
      graphics_draw_line(ctx, GPoint(x-2, y-3), GPoint(x+2, y-5));
      graphics_draw_line(ctx, GPoint(x+2, y-5), GPoint(x+10, y-5));
      graphics_draw_line(ctx, GPoint(x-8, y+2), GPoint(x, y+2));
      graphics_draw_line(ctx, GPoint(x, y+2), GPoint(x+4, y));
      graphics_draw_line(ctx, GPoint(x+4, y), GPoint(x+12, y));
      break;
    }
    default:
      // WX_CLEAR: no icon (sun is already drawn on arc)
      break;
  }
}

// ============================================================================
// DRAWING: OCEAN + WAVES (flowing toward shore)
// ============================================================================
static void draw_ocean(GContext *ctx, GRect b) {
  int horizon = (b.size.h * HORIZON_Y_PCT) / 100;
  int water_y = get_water_y(b.size.h);

  // Fill ocean zone from above screen down to water line
  // Use gradient bands
  #ifdef PBL_COLOR
  int ocean_top = horizon - TIDE_SHIFT_PX - 10;  // Always cover area above max tide
  int ocean_h = water_y - ocean_top;
  if (ocean_h > 0) {
    graphics_context_set_fill_color(ctx, COLOR_OCEAN_LIGHT);
    graphics_fill_rect(ctx, GRect(0, ocean_top, b.size.w, ocean_h / 3), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, COLOR_OCEAN_MID);
    graphics_fill_rect(ctx, GRect(0, ocean_top + ocean_h/3, b.size.w, ocean_h/3), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, COLOR_OCEAN_DEEP);
    graphics_fill_rect(ctx, GRect(0, ocean_top + ocean_h*2/3, b.size.w, ocean_h/3+2), 0, GCornerNone);
  }
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  int ot = horizon - TIDE_SHIFT_PX - 10;
  if (water_y > ot)
    graphics_fill_rect(ctx, GRect(0, ot, b.size.w, water_y - ot), 0, GCornerNone);
  #endif
}

static void draw_wave(GContext *ctx, const Wave *wave, GRect b) {
  int water_y = get_water_y(b.size.h);
  int wave_y = wave->base_y;

  // Adjust wave position based on tide
  int tide_shift = (s_tide.tide_height_pct * TIDE_SHIFT_PX) / 100;
  wave_y -= tide_shift;

  // Only draw if wave is above the water line (visible in ocean)
  if (wave_y > water_y) return;

  // Horizontal wave line with vertical oscillation (toward shore = downward)
  int16_t y_osc = (sin_lookup(wave->phase) * wave->amplitude) / TRIG_MAX_RATIO;
  int draw_y = wave_y + y_osc;

  // Draw wave as horizontal band with pixel-art wobble
  graphics_context_set_fill_color(ctx, wave->color);
  int step = 4;
  for (int x = 0; x < b.size.w; x += step) {
    // Small horizontal wobble for pixel texture
    int32_t seg_angle = (wave->phase + (x * TRIG_MAX_ANGLE / b.size.w)) % TRIG_MAX_ANGLE;
    int16_t x_wobble = (sin_lookup(seg_angle) * 2) / TRIG_MAX_RATIO;
    int sy = draw_y + x_wobble;
    graphics_fill_rect(ctx, GRect(x, sy, step, 5), 0, GCornerNone);
  }

  // Foam on front wave
  #ifdef PBL_COLOR
  if (wave->color.argb == COLOR_OCEAN_FOAM.argb) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int x = 0; x < b.size.w; x += step * 3) {
      int32_t seg_angle = (wave->phase + (x * TRIG_MAX_ANGLE / b.size.w)) % TRIG_MAX_ANGLE;
      int16_t x_wobble = (sin_lookup(seg_angle) * 2) / TRIG_MAX_RATIO;
      graphics_fill_rect(ctx, GRect(x, draw_y + x_wobble - 1, step, 2), 0, GCornerNone);
    }
  }
  #endif
}

// ============================================================================
// DRAWING: SAND
// ============================================================================
static void draw_sand(GContext *ctx, GRect b) {
  int water_y = get_water_y(b.size.h);
  int sand_y = water_y;  // Sand starts where water ends

  graphics_context_set_fill_color(ctx, COLOR_SAND);
  graphics_fill_rect(ctx, GRect(0, sand_y, b.size.w, b.size.h - sand_y), 0, GCornerNone);

  #ifdef PBL_COLOR
  // Wet sand strip
  graphics_context_set_fill_color(ctx, COLOR_SAND_WET);
  graphics_fill_rect(ctx, GRect(0, sand_y, b.size.w, 6), 0, GCornerNone);

  // Texture dots
  graphics_context_set_fill_color(ctx, COLOR_SAND_DARK);
  int dots[][2] = {
    {25,10},{60,16},{100,8},{140,20},{180,12},{220,18},
    {40,26},{80,30},{130,24},{170,32},{210,28},{50,38}
  };
  for (int i = 0; i < 12; i++) {
    int dy = sand_y + 6 + dots[i][1];
    if (dots[i][0] < b.size.w && dy < b.size.h)
      graphics_fill_rect(ctx, GRect(dots[i][0], dy, 2, 2), 0, GCornerNone);
  }
  #endif
}

// ============================================================================
// DRAWING: TEXT HELPERS
// ============================================================================
static void draw_text_shadow(GContext *ctx, const char *text, GFont font,
                              GRect box, GTextAlignment align) {
  graphics_context_set_text_color(ctx, COLOR_TEXT_SHADOW);
  graphics_draw_text(ctx, text, font,
    GRect(box.origin.x+1, box.origin.y+1, box.size.w, box.size.h),
    GTextOverflowModeTrailingEllipsis, align, NULL);
  graphics_context_set_text_color(ctx, COLOR_TEXT);
  graphics_draw_text(ctx, text, font, box,
    GTextOverflowModeTrailingEllipsis, align, NULL);
}

// ============================================================================
// DRAWING: HUD (temp, weather, time, date, sun times, tide info)
// ============================================================================
static void draw_hud(GContext *ctx, GRect b) {
  int horizon = (b.size.h * HORIZON_Y_PCT) / 100;
  int water_y = get_water_y(b.size.h);

  // -- TEMP + WEATHER ICON centered at top of sky --
  GFont temp_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  snprintf(s_temp_buffer, sizeof(s_temp_buffer), "%d°", s_tide.temperature);

  // Measure temp text width to center temp+icon as a group
  int temp_y = 42;  // Fixed Y near top of visible round area
  int icon_present = (s_tide.weather_code != WX_CLEAR) ? 1 : 0;

  if (icon_present) {
    // Temp text on left of center, icon on right
    draw_text_shadow(ctx, s_temp_buffer, temp_font,
      GRect(b.size.w/2 - 50, temp_y, 50, 28), GTextAlignmentRight);
    draw_weather_icon(ctx, GPoint(b.size.w/2 + 14, temp_y + 12), s_tide.weather_code);
  } else {
    // No icon: just center the temp
    draw_text_shadow(ctx, s_temp_buffer, temp_font,
      GRect(0, temp_y, b.size.w, 28), GTextAlignmentCenter);
  }

  // -- TIME centered at horizon --
  GFont time_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  int time_y = horizon - 50;
  draw_text_shadow(ctx, s_time_buffer, time_font,
    GRect(0, time_y, b.size.w, 50), GTextAlignmentCenter);

  // -- DATE below time --
  GFont date_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  draw_text_shadow(ctx, s_date_buffer, date_font,
    GRect(0, time_y + 44, b.size.w, 22), GTextAlignmentCenter);

  // -- SUNRISE left, SUNSET right, flanking time --
  // Inset more for round display (bezel clips edges)
  GFont sun_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  snprintf(s_sunrise_buffer, sizeof(s_sunrise_buffer), "%d:%02d",
           s_tide.sunrise_hour, s_tide.sunrise_min);
  snprintf(s_sunset_buffer, sizeof(s_sunset_buffer), "%d:%02d",
           s_tide.sunset_hour, s_tide.sunset_min);

  int sun_y = time_y + 14;
  // Left side — inset 35px from edge for round bezel
  draw_text_shadow(ctx, s_sunrise_buffer, sun_font,
    GRect(35, sun_y, 50, 18), GTextAlignmentLeft);
  // Right side — inset 35px
  draw_text_shadow(ctx, s_sunset_buffer, sun_font,
    GRect(b.size.w - 85, sun_y, 50, 18), GTextAlignmentRight);

  // -- TIDE INFO on sand --
  // Logic:
  //   tide_state 1 = rising: prev was high→low, next is high
  //   tide_state 0 = falling: prev was low→high, next is low
  //
  // Top line: the most imminent/recent tide event
  //   - If next event is within 60min: "High Tide in 45m"
  //   - If prev event was within 60min: "High Tide 30m ago"
  //   - Otherwise: "High Tide 4:26pm"  (next event with timestamp)
  // Bottom line: always the opposite tide type with timestamp

  GFont tide_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  if (s_tide.data_valid) {
    int now_mins = s_current_hour * 60 + s_current_min;

    // Determine next and following tide
    int next_h, next_m, follow_h, follow_m;
    const char *next_label, *follow_label;

    if (s_tide.tide_state == 1) {
      // Rising → next is high, following is low
      next_h = s_tide.next_high_hour; next_m = s_tide.next_high_min;
      follow_h = s_tide.next_low_hour; follow_m = s_tide.next_low_min;
      next_label = "High Tide";
      follow_label = "Low Tide";
    } else {
      // Falling → next is low, following is high
      next_h = s_tide.next_low_hour; next_m = s_tide.next_low_min;
      follow_h = s_tide.next_high_hour; follow_m = s_tide.next_high_min;
      next_label = "Low Tide";
      follow_label = "High Tide";
    }

    int next_mins = next_h * 60 + next_m;
    int prev_mins = s_tide.prev_tide_hour * 60 + s_tide.prev_tide_min;

    // Handle day wrap (next tide might be tomorrow)
    int mins_until_next = next_mins - now_mins;
    if (mins_until_next < 0) mins_until_next += 24 * 60;

    int mins_since_prev = now_mins - prev_mins;
    if (mins_since_prev < 0) mins_since_prev += 24 * 60;

    // Previous tide label (opposite of next)
    const char *prev_label = (s_tide.tide_state == 1) ? "Low Tide" : "High Tide";

    // -- TOP LINE --
    if (mins_since_prev <= 60) {
      // Recent event: "High Tide 30m ago"
      if (mins_since_prev == 0) {
        snprintf(s_tide_line1, sizeof(s_tide_line1), "%s now!", prev_label);
      } else if (mins_since_prev < 60) {
        snprintf(s_tide_line1, sizeof(s_tide_line1), "%s %dm ago", prev_label, mins_since_prev);
      } else {
        snprintf(s_tide_line1, sizeof(s_tide_line1), "%s 1hr ago", prev_label);
      }
      // Bottom line: next tide with timestamp
      snprintf(s_tide_line2, sizeof(s_tide_line2), "%s %d:%02d",
               next_label, next_h, next_m);
    } else if (mins_until_next <= 60) {
      // Approaching event: "High Tide in 45m"
      if (mins_until_next < 60) {
        snprintf(s_tide_line1, sizeof(s_tide_line1), "%s in %dm", next_label, mins_until_next);
      } else {
        snprintf(s_tide_line1, sizeof(s_tide_line1), "%s in 1hr", next_label);
      }
      // Bottom line: following tide with timestamp
      snprintf(s_tide_line2, sizeof(s_tide_line2), "%s %d:%02d",
               follow_label, follow_h, follow_m);
    } else {
      // More than 1hr from both: show next tide with timestamp
      snprintf(s_tide_line1, sizeof(s_tide_line1), "%s %d:%02d",
               next_label, next_h, next_m);
      // Bottom line: following tide with timestamp
      snprintf(s_tide_line2, sizeof(s_tide_line2), "%s %d:%02d",
               follow_label, follow_h, follow_m);
    }
  } else {
    snprintf(s_tide_line1, sizeof(s_tide_line1), "Loading...");
    s_tide_line2[0] = '\0';
  }

  // Position in the sand area
  int info_y = water_y + 6;
  if (info_y + 34 > b.size.h) info_y = b.size.h - 36;

  // Draw line 1 (top)
  graphics_context_set_text_color(ctx, COLOR_TEXT_SHADOW);
  graphics_draw_text(ctx, s_tide_line1, tide_font,
    GRect(1, info_y+1, b.size.w, 18),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, COLOR_TEXT_INFO);
  graphics_draw_text(ctx, s_tide_line1, tide_font,
    GRect(0, info_y, b.size.w, 18),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Draw line 2 (bottom)
  if (s_tide_line2[0] != '\0') {
    graphics_context_set_text_color(ctx, COLOR_TEXT_SHADOW);
    graphics_draw_text(ctx, s_tide_line2, tide_font,
      GRect(1, info_y+15, b.size.w, 18),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, COLOR_TEXT_INFO);
    graphics_draw_text(ctx, s_tide_line2, tide_font,
      GRect(0, info_y+14, b.size.w, 18),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

// ============================================================================
// MAIN CANVAS
// ============================================================================
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);

  draw_sky(ctx, b);
  draw_ocean(ctx, b);

  // Waves back to front
  for (int i = NUM_WAVES - 1; i >= 0; i--) {
    draw_wave(ctx, &s_waves[i], b);
  }

  draw_sand(ctx, b);
  draw_hud(ctx, b);
}

// ============================================================================
// UPDATE
// ============================================================================
static void update_waves(void) {
  for (int i = 0; i < NUM_WAVES; i++) {
    s_waves[i].phase = (s_waves[i].phase + s_waves[i].speed) % TRIG_MAX_ANGLE;
  }
}

static void update_time(void) {
  time_t temp = time(NULL);
  struct tm *t = localtime(&temp);
  if (!t) return;
  s_current_hour = t->tm_hour;
  s_current_min = t->tm_min;
  strftime(s_time_buffer, sizeof(s_time_buffer),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  if (!clock_is_24h_style() && s_time_buffer[0] == '0')
    memmove(s_time_buffer, &s_time_buffer[1], sizeof(s_time_buffer) - 1);
  strftime(s_date_buffer, sizeof(s_date_buffer), "%a, %b %d", t);
}

// ============================================================================
// ANIMATION
// ============================================================================
static void animation_timer_callback(void *data) {
  update_waves();
  s_anim_elapsed += WAVE_ANIM_INTERVAL;
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);

  #if DEV_MODE
  bool should_stop = false;
  #else
  bool should_stop = (s_anim_elapsed >= WAVE_ANIM_DURATION);
  #endif
  if (should_stop || s_battery_level <= LOW_BATTERY_THRESHOLD) {
    s_is_animating = false;
    s_animation_timer = NULL;
    return;
  }
  s_animation_timer = app_timer_register(WAVE_ANIM_INTERVAL, animation_timer_callback, NULL);
}

static void start_wave_animation(void) {
  if (s_is_animating) return;
  s_is_animating = true;
  s_anim_elapsed = 0;
  s_animation_timer = app_timer_register(WAVE_ANIM_INTERVAL, animation_timer_callback, NULL);
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  #if DEV_MODE
  if (s_test_preset >= 0) {
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
    return;
  }
  #endif
  update_time();
  if (tick_time->tm_min % 5 == 0) start_wave_animation();
  else if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
  if (tick_time->tm_min % 30 == 0) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_uint8(iter, MESSAGE_KEY_REQUEST_DATA, 1);
      app_message_outbox_send();
    }
  }
}

static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  #if DEV_MODE
  if (s_test_preset >= 0 || !s_tide.data_valid) {
    s_test_preset++;
    if (s_test_preset >= NUM_TEST_PRESETS) s_test_preset = 0;
    apply_test_preset(s_test_preset);
    APP_LOG(APP_LOG_LEVEL_INFO, "DEV preset %d: %s", s_test_preset, s_preset_names[s_test_preset]);
  }
  #endif
  start_wave_animation();
}

// ============================================================================
// APPMESSAGE
// ============================================================================
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *t;
  #if DEV_MODE
  if (s_test_preset >= 0) {
    t = dict_find(iterator, MESSAGE_KEY_DISPLAY_MODE);
    if (t) { s_display_mode = (int)t->value->int32; }
    APP_LOG(APP_LOG_LEVEL_INFO, "DEV: ignoring live data (preset %d)", s_test_preset);
    return;
  }
  #endif
  t = dict_find(iterator, MESSAGE_KEY_TIDE_HEIGHT);
  if (t) s_tide.tide_height_pct = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_TIDE_STATE);
  if (t) s_tide.tide_state = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_SUNRISE_HOUR);
  if (t) s_tide.sunrise_hour = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_SUNRISE_MIN);
  if (t) s_tide.sunrise_min = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_SUNSET_HOUR);
  if (t) s_tide.sunset_hour = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_SUNSET_MIN);
  if (t) s_tide.sunset_min = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_NEXT_HIGH_HOUR);
  if (t) s_tide.next_high_hour = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_NEXT_HIGH_MIN);
  if (t) s_tide.next_high_min = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_NEXT_LOW_HOUR);
  if (t) s_tide.next_low_hour = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_NEXT_LOW_MIN);
  if (t) s_tide.next_low_min = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_PREV_TIDE_HOUR);
  if (t) s_tide.prev_tide_hour = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_PREV_TIDE_MIN);
  if (t) s_tide.prev_tide_min = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_DISPLAY_MODE);
  if (t) s_display_mode = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
  if (t) s_tide.temperature = (int)t->value->int32;
  t = dict_find(iterator, MESSAGE_KEY_WEATHER_CODE);
  if (t) s_tide.weather_code = (int)t->value->int32;

  s_tide.data_valid = true;
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);

  persist_write_int(PERSIST_TIDE_HEIGHT, s_tide.tide_height_pct);
  persist_write_int(PERSIST_TIDE_STATE, s_tide.tide_state);
  persist_write_int(PERSIST_SUNRISE_HOUR, s_tide.sunrise_hour);
  persist_write_int(PERSIST_SUNRISE_MIN, s_tide.sunrise_min);
  persist_write_int(PERSIST_SUNSET_HOUR, s_tide.sunset_hour);
  persist_write_int(PERSIST_SUNSET_MIN, s_tide.sunset_min);
  persist_write_int(PERSIST_NEXT_HIGH_HOUR, s_tide.next_high_hour);
  persist_write_int(PERSIST_NEXT_HIGH_MIN, s_tide.next_high_min);
  persist_write_int(PERSIST_NEXT_LOW_HOUR, s_tide.next_low_hour);
  persist_write_int(PERSIST_NEXT_LOW_MIN, s_tide.next_low_min);
  persist_write_int(PERSIST_PREV_TIDE_HOUR, s_tide.prev_tide_hour);
  persist_write_int(PERSIST_PREV_TIDE_MIN, s_tide.prev_tide_min);
  persist_write_int(PERSIST_TEMPERATURE, s_tide.temperature);
  persist_write_int(PERSIST_WEATHER_CODE, s_tide.weather_code);
  persist_write_bool(PERSIST_DATA_VALID, true);
}

static void inbox_dropped_callback(AppMessageResult reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Msg dropped: %d", (int)reason);
}
static void outbox_failed_callback(DictionaryIterator *it, AppMessageResult r, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox fail: %d", (int)r);
}
static void outbox_sent_callback(DictionaryIterator *it, void *ctx) {}

// ============================================================================
// PERSIST
// ============================================================================
static void load_persisted_data(void) {
  if (persist_exists(PERSIST_DATA_VALID) && persist_read_bool(PERSIST_DATA_VALID)) {
    s_tide.tide_height_pct = persist_read_int(PERSIST_TIDE_HEIGHT);
    s_tide.tide_state = persist_read_int(PERSIST_TIDE_STATE);
    s_tide.sunrise_hour = persist_read_int(PERSIST_SUNRISE_HOUR);
    s_tide.sunrise_min = persist_read_int(PERSIST_SUNRISE_MIN);
    s_tide.sunset_hour = persist_read_int(PERSIST_SUNSET_HOUR);
    s_tide.sunset_min = persist_read_int(PERSIST_SUNSET_MIN);
    s_tide.next_high_hour = persist_read_int(PERSIST_NEXT_HIGH_HOUR);
    s_tide.next_high_min = persist_read_int(PERSIST_NEXT_HIGH_MIN);
    s_tide.next_low_hour = persist_read_int(PERSIST_NEXT_LOW_HOUR);
    s_tide.next_low_min = persist_read_int(PERSIST_NEXT_LOW_MIN);
    if (persist_exists(PERSIST_PREV_TIDE_HOUR)) {
      s_tide.prev_tide_hour = persist_read_int(PERSIST_PREV_TIDE_HOUR);
      s_tide.prev_tide_min = persist_read_int(PERSIST_PREV_TIDE_MIN);
    }
    s_tide.data_valid = true;
  }
  if (persist_exists(PERSIST_TEMPERATURE))
    s_tide.temperature = persist_read_int(PERSIST_TEMPERATURE);
  if (persist_exists(PERSIST_WEATHER_CODE))
    s_tide.weather_code = persist_read_int(PERSIST_WEATHER_CODE);
  if (persist_exists(PERSIST_DISPLAY_MODE))
    s_display_mode = persist_read_int(PERSIST_DISPLAY_MODE);
}

// ============================================================================
// WINDOW
// ============================================================================
static void main_window_load(Window *window) {
  Layer *wl = window_get_root_layer(window);
  GRect b = layer_get_bounds(wl);
  s_canvas_layer = layer_create(b);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(wl, s_canvas_layer);
  init_waves(b.size.h);
  s_battery_level = battery_state_service_peek().charge_percent;
  update_time();
  #if DEV_MODE
  if (!s_tide.data_valid) {
    s_test_preset = 0;
    apply_test_preset(0);
    s_display_mode = DISPLAY_MODE_DETAILED;
    APP_LOG(APP_LOG_LEVEL_INFO, "DEV: preset 0, tap to cycle");
  }
  #endif
  start_wave_animation();
}

static void main_window_unload(Window *window) {
  if (s_animation_timer) { app_timer_cancel(s_animation_timer); s_animation_timer = NULL; }
  if (s_canvas_layer) { layer_destroy(s_canvas_layer); s_canvas_layer = NULL; }
}

// ============================================================================
// LIFECYCLE
// ============================================================================
static void init(void) {
  load_persisted_data();
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load = main_window_load, .unload = main_window_unload
  });
  window_stack_push(s_main_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_callback);
  accel_tap_service_subscribe(tap_handler);
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_open(1024, 64);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  accel_tap_service_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
