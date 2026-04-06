/**
 * Pixel Tides - Pebble Round 2 (Gabbro) Watchface
 *
 * Pixel art beach scene with real tide data and sunrise/sunset tracking.
 * Water level rises/falls with tides, sun moves across sky arc.
 *
 * Target: gabbro (260x260, round, 64-color)
 * Battery-efficient: MINUTE_UNIT updates, brief wave animation on tap.
 */

#include <pebble.h>

// ============================================================================
// CONSTANTS
// ============================================================================

// Wave animation
#define NUM_WAVES 4
#define WAVE_ANIM_INTERVAL 60
#define WAVE_ANIM_DURATION 5000
#define LOW_BATTERY_THRESHOLD 20

// Scene layout proportions (relative to screen height 260)
// These are base values; tide shifts the water level
#define SKY_ZONE_END_PCT      45   // Sky ends at 45% of screen
#define OCEAN_ZONE_END_PCT    78   // Ocean ends at 78%
#define SAND_ZONE_START_PCT   78   // Sand starts at 78%

// Sun arc
#define SUN_RADIUS 12
#define SUN_ARC_TOP_Y_PCT     8    // Highest point of sun arc
#define SUN_ARC_HORIZON_Y_PCT 45   // Horizon line for sunrise/set

// Tide range in pixels (how much water level shifts)
#define TIDE_SHIFT_MAX 20

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

// Dev/test mode - cycle through preset scenarios with up/down buttons
#define DEV_MODE 1
#define NUM_TEST_PRESETS 6

// Palm tree
#define PALM_TRUNK_X_PCT  18
#define PALM_TOP_Y_PCT    15

// ============================================================================
// COLOR DEFINITIONS
// ============================================================================
#ifdef PBL_COLOR
  // Sky colors for different times of day
  #define COLOR_SKY_DAY       GColorPictonBlue
  #define COLOR_SKY_DAY_LOW   GColorCeleste
  #define COLOR_SKY_DAWN      GColorMelon
  #define COLOR_SKY_DUSK      GColorOrange
  #define COLOR_SKY_NIGHT     GColorOxfordBlue

  // Sun
  #define COLOR_SUN           GColorYellow
  #define COLOR_SUN_GLOW      GColorRajah

  // Ocean
  #define COLOR_OCEAN_DEEP    GColorCobaltBlue
  #define COLOR_OCEAN_MID     GColorBlue
  #define COLOR_OCEAN_LIGHT   GColorVividCerulean
  #define COLOR_OCEAN_FOAM    GColorCeleste

  // Sand
  #define COLOR_SAND          GColorFromHEX(0xD4B070)
  #define COLOR_SAND_DARK     GColorFromHEX(0xB49650)
  #define COLOR_SAND_WET      GColorFromHEX(0x907040)

  // Vegetation
  #define COLOR_PALM_TRUNK    GColorFromHEX(0x8B6914)
  #define COLOR_PALM_LEAVES   GColorIslamicGreen

  // Text
  #define COLOR_TEXT           GColorWhite
  #define COLOR_TEXT_SHADOW    GColorBlack
  #define COLOR_TEXT_INFO      GColorCeleste
#else
  #define COLOR_SKY_DAY       GColorWhite
  #define COLOR_SKY_DAY_LOW   GColorWhite
  #define COLOR_SKY_DAWN      GColorWhite
  #define COLOR_SKY_DUSK      GColorLightGray
  #define COLOR_SKY_NIGHT     GColorBlack
  #define COLOR_SUN           GColorWhite
  #define COLOR_SUN_GLOW      GColorWhite
  #define COLOR_OCEAN_DEEP    GColorBlack
  #define COLOR_OCEAN_MID     GColorBlack
  #define COLOR_OCEAN_LIGHT   GColorDarkGray
  #define COLOR_OCEAN_FOAM    GColorWhite
  #define COLOR_SAND          GColorLightGray
  #define COLOR_SAND_DARK     GColorDarkGray
  #define COLOR_SAND_WET      GColorDarkGray
  #define COLOR_PALM_TRUNK    GColorDarkGray
  #define COLOR_PALM_LEAVES   GColorBlack
  #define COLOR_TEXT           GColorBlack
  #define COLOR_TEXT_SHADOW    GColorWhite
  #define COLOR_TEXT_INFO      GColorBlack
#endif

// ============================================================================
// DATA STRUCTURES
// ============================================================================
typedef struct {
  int base_y;
  int32_t phase;
  int amplitude;
  int speed;
  GColor color;
} Wave;

typedef struct {
  int sunrise_hour;
  int sunrise_min;
  int sunset_hour;
  int sunset_min;
  int tide_height_pct;  // 0-100, where 0=low tide, 100=high tide
  int tide_state;       // 0=falling, 1=rising
  int next_high_hour;
  int next_high_min;
  int next_low_hour;
  int next_low_min;
  bool data_valid;
} TideData;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
static Window *s_main_window;
static Layer *s_canvas_layer;
static AppTimer *s_animation_timer;

static int s_battery_level = 100;
static bool s_is_animating = false;
static int s_anim_elapsed = 0;

static Wave s_waves[NUM_WAVES];
static TideData s_tide = {
  .sunrise_hour = 6, .sunrise_min = 30,
  .sunset_hour = 19, .sunset_min = 30,
  .tide_height_pct = 50,
  .tide_state = 1,
  .next_high_hour = 12, .next_high_min = 0,
  .next_low_hour = 18, .next_low_min = 0,
  .data_valid = false
};

static char s_time_buffer[8];
static char s_date_buffer[16];
static char s_tide_info_buffer[32];
static char s_sun_info_buffer[32];

static int s_display_mode = DISPLAY_MODE_MINIMAL;

static int s_current_hour = 12;
static int s_current_min = 0;

#if DEV_MODE
static int s_test_preset = -1;  // -1 = live data, 0+ = test preset index
static const char *s_preset_names[] = {
  "Dawn Rise",
  "Morning Hi",
  "Noon Mid",
  "Dusk Low",
  "Night Fall",
  "Night Rise"
};

typedef struct {
  int hour;
  int min;
  int sunrise_hour;
  int sunrise_min;
  int sunset_hour;
  int sunset_min;
  int tide_pct;
  int tide_state;  // 0=falling, 1=rising
  int next_high_hour;
  int next_high_min;
  int next_low_hour;
  int next_low_min;
} TestPreset;

static const TestPreset s_test_presets[NUM_TEST_PRESETS] = {
  // Dawn + Rising tide (25%)
  { 6, 15,   6, 0,  19, 45,   25, 1,   10, 30,  16, 45 },
  // Morning + High tide (95%)
  { 9, 30,   6, 0,  19, 45,   95, 0,   9, 15,   15, 30 },
  // Noon + Mid tide falling (50%)
  { 12, 0,   6, 0,  19, 45,   50, 0,   18, 0,   15, 30 },
  // Dusk + Low tide (10%)
  { 19, 30,  6, 0,  19, 45,   10, 1,   22, 0,   19, 15 },
  // Night + Falling tide (60%)
  { 22, 0,   6, 0,  19, 45,   60, 0,   4, 30,   22, 30 },
  // Late night + Rising tide (30%)
  { 2, 30,   6, 0,  19, 45,   30, 1,   4, 30,   0, 15 },
};

static void apply_test_preset(int index) {
  if (index < 0 || index >= NUM_TEST_PRESETS) return;
  const TestPreset *p = &s_test_presets[index];

  s_current_hour = p->hour;
  s_current_min = p->min;
  s_tide.sunrise_hour = p->sunrise_hour;
  s_tide.sunrise_min = p->sunrise_min;
  s_tide.sunset_hour = p->sunset_hour;
  s_tide.sunset_min = p->sunset_min;
  s_tide.tide_height_pct = p->tide_pct;
  s_tide.tide_state = p->tide_state;
  s_tide.next_high_hour = p->next_high_hour;
  s_tide.next_high_min = p->next_high_min;
  s_tide.next_low_hour = p->next_low_hour;
  s_tide.next_low_min = p->next_low_min;
  s_tide.data_valid = true;

  // Override time buffer to show test time
  snprintf(s_time_buffer, sizeof(s_time_buffer), "%d:%02d", p->hour, p->min);
  snprintf(s_date_buffer, sizeof(s_date_buffer), "DEV: %s", s_preset_names[index]);
}
#endif

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Get sun progress: 0 = sunrise, 50 = solar noon, 100 = sunset
// Returns -1 if before sunrise or after sunset (nighttime)
static int get_sun_progress(void) {
  int sunrise_mins = s_tide.sunrise_hour * 60 + s_tide.sunrise_min;
  int sunset_mins = s_tide.sunset_hour * 60 + s_tide.sunset_min;
  int now_mins = s_current_hour * 60 + s_current_min;

  if (now_mins < sunrise_mins || now_mins > sunset_mins) {
    return -1;  // Night
  }

  int day_length = sunset_mins - sunrise_mins;
  if (day_length <= 0) return 50;

  return ((now_mins - sunrise_mins) * 100) / day_length;
}

// Get how close we are to dawn/dusk for color blending
// Returns 0-100 where 0 = full day color, 100 = full dawn/dusk color
static int get_twilight_intensity(void) {
  int progress = get_sun_progress();
  if (progress < 0) return 0;  // Night, handled separately

  // Dawn: 0-15% of day
  if (progress < 15) {
    return (15 - progress) * 100 / 15;
  }
  // Dusk: 85-100% of day
  if (progress > 85) {
    return (progress - 85) * 100 / 15;
  }
  return 0;
}

static bool is_nighttime(void) {
  return get_sun_progress() < 0;
}

// Calculate tide-adjusted water level offset in pixels
static int get_tide_offset(void) {
  // tide_height_pct: 0 = low tide (water down), 100 = high tide (water up)
  // Returns negative = water higher, positive = water lower
  return TIDE_SHIFT_MAX - (s_tide.tide_height_pct * TIDE_SHIFT_MAX * 2) / 100;
}

// ============================================================================
// WAVE INITIALIZATION
// ============================================================================
static void init_waves(int screen_h) {
  int ocean_base = (screen_h * OCEAN_ZONE_END_PCT) / 100;
  int ocean_start = (screen_h * SKY_ZONE_END_PCT) / 100;
  int ocean_range = ocean_base - ocean_start;

  // Wave 0 - Back wave (farthest, darkest, slowest)
  s_waves[0].base_y = ocean_start + ocean_range * 20 / 100;
  s_waves[0].phase = 0;
  s_waves[0].amplitude = 3;
  s_waves[0].speed = 180;
  s_waves[0].color = COLOR_OCEAN_DEEP;

  // Wave 1 - Mid-back wave
  s_waves[1].base_y = ocean_start + ocean_range * 45 / 100;
  s_waves[1].phase = TRIG_MAX_ANGLE / 4;
  s_waves[1].amplitude = 4;
  s_waves[1].speed = 250;
  s_waves[1].color = COLOR_OCEAN_MID;

  // Wave 2 - Mid-front wave
  s_waves[2].base_y = ocean_start + ocean_range * 65 / 100;
  s_waves[2].phase = TRIG_MAX_ANGLE / 2;
  s_waves[2].amplitude = 5;
  s_waves[2].speed = 320;
  s_waves[2].color = COLOR_OCEAN_LIGHT;

  // Wave 3 - Front wave (closest, lightest, fastest) - foam
  s_waves[3].base_y = ocean_start + ocean_range * 88 / 100;
  s_waves[3].phase = TRIG_MAX_ANGLE * 3 / 4;
  s_waves[3].amplitude = 4;
  s_waves[3].speed = 400;
  s_waves[3].color = COLOR_OCEAN_FOAM;
}

// ============================================================================
// DRAWING FUNCTIONS
// ============================================================================

static void draw_sky(GContext *ctx, GRect bounds) {
  int sky_end = (bounds.size.h * SKY_ZONE_END_PCT) / 100;
  bool night = is_nighttime();
  int twilight = get_twilight_intensity();

  if (night) {
    // Night sky
    graphics_context_set_fill_color(ctx, COLOR_SKY_NIGHT);
    graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, sky_end), 0, GCornerNone);

    // Pixel stars
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorWhite);
    int star_positions[][2] = {
      {30, 20}, {80, 15}, {130, 35}, {200, 25}, {45, 50},
      {160, 10}, {220, 45}, {100, 55}, {175, 60}, {55, 35},
      {240, 15}, {15, 45}, {195, 50}, {70, 8}, {145, 48}
    };
    for (int i = 0; i < 15; i++) {
      if (star_positions[i][0] < bounds.size.w &&
          star_positions[i][1] < sky_end) {
        graphics_draw_pixel(ctx, GPoint(star_positions[i][0], star_positions[i][1]));
        // Some stars are 2x2 for pixel effect
        if (i % 3 == 0) {
          graphics_draw_pixel(ctx, GPoint(star_positions[i][0] + 1, star_positions[i][1]));
          graphics_draw_pixel(ctx, GPoint(star_positions[i][0], star_positions[i][1] + 1));
          graphics_draw_pixel(ctx, GPoint(star_positions[i][0] + 1, star_positions[i][1] + 1));
        }
      }
    }
    #endif
  } else {
    // Daytime sky - upper portion
    GColor sky_upper = COLOR_SKY_DAY;
    if (twilight > 50) {
      sky_upper = COLOR_SKY_DAWN;
    }
    graphics_context_set_fill_color(ctx, sky_upper);
    graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, sky_end * 2 / 3), 0, GCornerNone);

    // Lower sky - lighter near horizon
    GColor sky_lower = COLOR_SKY_DAY_LOW;
    if (twilight > 30) {
      sky_lower = COLOR_SKY_DUSK;
    }
    graphics_context_set_fill_color(ctx, sky_lower);
    graphics_fill_rect(ctx, GRect(0, sky_end * 2 / 3, bounds.size.w, sky_end / 3),
                       0, GCornerNone);

    // Draw sun based on progress
    int progress = get_sun_progress();
    if (progress >= 0) {
      // Sun travels in an arc across the screen
      // X: from 15% to 85% of screen width
      int sun_x = bounds.size.w * 15 / 100 +
                  (bounds.size.w * 70 / 100 * progress) / 100;

      // Y: parabolic arc, highest at noon (progress=50)
      int horizon_y = (bounds.size.h * SUN_ARC_HORIZON_Y_PCT) / 100;
      int top_y = (bounds.size.h * SUN_ARC_TOP_Y_PCT) / 100;
      int arc_height = horizon_y - top_y;

      // Parabola: y = horizon - arc_height * (1 - ((progress-50)/50)^2)
      int centered = progress - 50;  // -50 to 50
      int sun_y = horizon_y - (arc_height * (2500 - centered * centered)) / 2500;

      // Sun glow
      #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx, COLOR_SUN_GLOW);
      graphics_fill_circle(ctx, GPoint(sun_x, sun_y), SUN_RADIUS + 4);
      #endif

      // Sun body (pixel-style: slightly blocky)
      graphics_context_set_fill_color(ctx, COLOR_SUN);
      graphics_fill_circle(ctx, GPoint(sun_x, sun_y), SUN_RADIUS);

      // Pixel sun rays (small rectangles in cardinal directions)
      #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx, COLOR_SUN_GLOW);
      int ray_dist = SUN_RADIUS + 6;
      // Top/bottom rays
      graphics_fill_rect(ctx, GRect(sun_x - 1, sun_y - ray_dist - 4, 3, 4),
                         0, GCornerNone);
      graphics_fill_rect(ctx, GRect(sun_x - 1, sun_y + ray_dist, 3, 4),
                         0, GCornerNone);
      // Left/right rays
      graphics_fill_rect(ctx, GRect(sun_x - ray_dist - 4, sun_y - 1, 4, 3),
                         0, GCornerNone);
      graphics_fill_rect(ctx, GRect(sun_x + ray_dist, sun_y - 1, 4, 3),
                         0, GCornerNone);
      // Diagonal rays (2x2 pixel blocks)
      int diag = ray_dist * 70 / 100;  // ~cos(45)
      graphics_fill_rect(ctx, GRect(sun_x - diag - 2, sun_y - diag - 2, 2, 2),
                         0, GCornerNone);
      graphics_fill_rect(ctx, GRect(sun_x + diag, sun_y - diag - 2, 2, 2),
                         0, GCornerNone);
      graphics_fill_rect(ctx, GRect(sun_x - diag - 2, sun_y + diag, 2, 2),
                         0, GCornerNone);
      graphics_fill_rect(ctx, GRect(sun_x + diag, sun_y + diag, 2, 2),
                         0, GCornerNone);
      #endif
    }
  }
}

static void draw_ocean_background(GContext *ctx, GRect bounds) {
  int sky_end = (bounds.size.h * SKY_ZONE_END_PCT) / 100;
  int sand_start = (bounds.size.h * SAND_ZONE_START_PCT) / 100;
  int tide_offset = get_tide_offset();

  // Adjust sand line with tide
  sand_start += tide_offset;

  // Ocean gradient bands
  int ocean_height = sand_start - sky_end;

  #ifdef PBL_COLOR
  // Light blue near horizon
  graphics_context_set_fill_color(ctx, COLOR_OCEAN_LIGHT);
  graphics_fill_rect(ctx, GRect(0, sky_end, bounds.size.w, ocean_height / 3),
                     0, GCornerNone);

  // Mid blue
  graphics_context_set_fill_color(ctx, COLOR_OCEAN_MID);
  graphics_fill_rect(ctx, GRect(0, sky_end + ocean_height / 3,
                     bounds.size.w, ocean_height / 3), 0, GCornerNone);

  // Deep blue
  graphics_context_set_fill_color(ctx, COLOR_OCEAN_DEEP);
  graphics_fill_rect(ctx, GRect(0, sky_end + ocean_height * 2 / 3,
                     bounds.size.w, ocean_height / 3 + 2), 0, GCornerNone);
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, sky_end, bounds.size.w, ocean_height),
                     0, GCornerNone);
  #endif
}

static void draw_wave(GContext *ctx, const Wave *wave, GRect bounds) {
  int tide_offset = get_tide_offset();
  int wave_y = wave->base_y + tide_offset;

  // Draw wave as filled area below the wave line (pixel style: 4px steps)
  graphics_context_set_fill_color(ctx, wave->color);

  int step = 4;  // Pixel art step size
  for (int x = 0; x < bounds.size.w; x += step) {
    int32_t segment_angle = (wave->phase +
        (x * TRIG_MAX_ANGLE * 2 / bounds.size.w)) % TRIG_MAX_ANGLE;
    int16_t y_offset = (sin_lookup(segment_angle) * wave->amplitude) / TRIG_MAX_RATIO;

    int y = wave_y + y_offset;
    // Fill from wave crest down to bottom of ocean zone
    int sand_y = (bounds.size.h * SAND_ZONE_START_PCT) / 100 + tide_offset;
    int fill_h = sand_y - y;
    if (fill_h > 0) {
      graphics_fill_rect(ctx, GRect(x, y, step, fill_h), 0, GCornerNone);
    }
  }

  // Draw foam line on top wave (white pixels along crest)
  #ifdef PBL_COLOR
  if (wave->color.argb == COLOR_OCEAN_FOAM.argb) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int x = 0; x < bounds.size.w; x += step * 2) {
      int32_t segment_angle = (wave->phase +
          (x * TRIG_MAX_ANGLE * 2 / bounds.size.w)) % TRIG_MAX_ANGLE;
      int16_t y_offset = (sin_lookup(segment_angle) * wave->amplitude) / TRIG_MAX_RATIO;
      int y = wave_y + y_offset;
      graphics_fill_rect(ctx, GRect(x, y, step, 2), 0, GCornerNone);
    }
  }
  #endif
}

static void draw_sand(GContext *ctx, GRect bounds) {
  int tide_offset = get_tide_offset();
  int sand_y = (bounds.size.h * SAND_ZONE_START_PCT) / 100 + tide_offset;

  // Main sand
  graphics_context_set_fill_color(ctx, COLOR_SAND);
  graphics_fill_rect(ctx, GRect(0, sand_y, bounds.size.w, bounds.size.h - sand_y),
                     0, GCornerNone);

  // Wet sand near water line
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, COLOR_SAND_WET);
  graphics_fill_rect(ctx, GRect(0, sand_y, bounds.size.w, 8), 0, GCornerNone);

  // Sand texture (pixel dots)
  graphics_context_set_fill_color(ctx, COLOR_SAND_DARK);
  int dots[][2] = {
    {20, 12}, {55, 18}, {90, 10}, {130, 22}, {170, 14},
    {210, 20}, {40, 28}, {80, 32}, {120, 26}, {160, 34},
    {200, 30}, {35, 40}, {75, 38}, {145, 42}, {185, 36},
    {240, 16}, {15, 24}, {105, 44}, {225, 28}, {60, 46}
  };
  for (int i = 0; i < 20; i++) {
    int dx = dots[i][0];
    int dy = sand_y + 8 + dots[i][1];
    if (dx < bounds.size.w && dy < bounds.size.h) {
      graphics_fill_rect(ctx, GRect(dx, dy, 2, 2), 0, GCornerNone);
    }
  }
  #endif
}

static void draw_palm_tree(GContext *ctx, GRect bounds) {
  int trunk_x = (bounds.size.w * PALM_TRUNK_X_PCT) / 100;
  int top_y = (bounds.size.h * PALM_TOP_Y_PCT) / 100;
  int tide_offset = get_tide_offset();
  int sand_y = (bounds.size.h * SAND_ZONE_START_PCT) / 100 + tide_offset;

  // Trunk (pixel style: 4px wide, slight curve)
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, COLOR_PALM_TRUNK);
  #else
  graphics_context_set_fill_color(ctx, COLOR_PALM_TRUNK);
  #endif

  // Draw trunk segments with slight rightward lean
  int trunk_segments = 12;
  int trunk_height = sand_y - top_y;
  for (int i = 0; i < trunk_segments; i++) {
    int seg_y = top_y + (trunk_height * i) / trunk_segments;
    int seg_h = trunk_height / trunk_segments + 1;
    // Slight curve: offset decreases as we go up
    int x_offset = (i * i) / (trunk_segments * 2);
    int width = 3 + (i > trunk_segments - 3 ? 1 : 0);  // Wider at base
    graphics_fill_rect(ctx, GRect(trunk_x + x_offset, seg_y, width, seg_h),
                       0, GCornerNone);
  }

  // Palm fronds (pixel art style - blocky leaf shapes)
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, COLOR_PALM_LEAVES);
  #else
  graphics_context_set_fill_color(ctx, COLOR_PALM_LEAVES);
  #endif

  // Right frond
  int frond_points[][2] = {
    {0, 0}, {8, -4}, {16, -2}, {24, 2}, {30, 8},
    {34, 14}, {36, 20}
  };
  for (int i = 0; i < 6; i++) {
    int fx = trunk_x + frond_points[i][0];
    int fy = top_y + frond_points[i][1];
    graphics_fill_rect(ctx, GRect(fx, fy, 8, 4), 0, GCornerNone);
  }

  // Left frond (mirrored)
  for (int i = 0; i < 6; i++) {
    int fx = trunk_x - frond_points[i][0] - 6;
    int fy = top_y + frond_points[i][1] + 2;
    graphics_fill_rect(ctx, GRect(fx, fy, 8, 4), 0, GCornerNone);
  }

  // Drooping center frond
  for (int i = 0; i < 4; i++) {
    int fx = trunk_x - 2 + i;
    int fy = top_y - 8 + i * 2;
    graphics_fill_rect(ctx, GRect(fx, fy, 4, 4), 0, GCornerNone);
  }
}

static void draw_text_with_shadow(GContext *ctx, const char *text,
                                   GFont font, GRect box) {
  // Shadow
  graphics_context_set_text_color(ctx, COLOR_TEXT_SHADOW);
  GRect shadow_box = GRect(box.origin.x + 1, box.origin.y + 1,
                            box.size.w, box.size.h);
  graphics_draw_text(ctx, text, font, shadow_box,
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);

  // Main text
  graphics_context_set_text_color(ctx, COLOR_TEXT);
  graphics_draw_text(ctx, text, font, box,
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void draw_info_line(GContext *ctx, const char *text, GFont font,
                            GRect box, GColor shadow_color, GColor text_color) {
  graphics_context_set_text_color(ctx, shadow_color);
  graphics_draw_text(ctx, text, font,
                     GRect(box.origin.x + 1, box.origin.y + 1, box.size.w, box.size.h),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, text_color);
  graphics_draw_text(ctx, text, font, box,
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void draw_tide_info(GContext *ctx, GRect bounds) {
  int sand_y = (bounds.size.h * SAND_ZONE_START_PCT) / 100 + get_tide_offset();
  int info_y = sand_y + 10;
  GFont small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // Tide arrow and state (shown in both modes)
  const char *arrow = s_tide.tide_state ? "^" : "v";
  char tide_line[32];

  if (s_display_mode == DISPLAY_MODE_MINIMAL) {
    // MINIMAL: just tide arrow + state
    const char *state_str = s_tide.tide_state ? "RISING" : "FALLING";
    snprintf(tide_line, sizeof(tide_line), "%s %s", arrow, state_str);
    draw_info_line(ctx, tide_line, small_font,
                   GRect(0, info_y, bounds.size.w, 18),
                   COLOR_TEXT_SHADOW, COLOR_TEXT_INFO);

  } else {
    // DETAILED: tide state + next high/low + sunrise/sunset
    const char *state_str = s_tide.tide_state ? "RISING" : "FALLING";
    snprintf(tide_line, sizeof(tide_line), "%s %s", arrow, state_str);
    draw_info_line(ctx, tide_line, small_font,
                   GRect(0, info_y, bounds.size.w, 18),
                   COLOR_TEXT_SHADOW, COLOR_TEXT_INFO);

    int line_y = info_y + 15;

    // Next high/low tide times
    if (s_tide.data_valid) {
      snprintf(s_tide_info_buffer, sizeof(s_tide_info_buffer),
               "Hi %d:%02d  Lo %d:%02d",
               s_tide.next_high_hour, s_tide.next_high_min,
               s_tide.next_low_hour, s_tide.next_low_min);
      draw_info_line(ctx, s_tide_info_buffer, small_font,
                     GRect(0, line_y, bounds.size.w, 18),
                     COLOR_TEXT_SHADOW, COLOR_TEXT_INFO);
      line_y += 15;

      // Sunrise/sunset times
      snprintf(s_sun_info_buffer, sizeof(s_sun_info_buffer),
               "SR %d:%02d  SS %d:%02d",
               s_tide.sunrise_hour, s_tide.sunrise_min,
               s_tide.sunset_hour, s_tide.sunset_min);
      draw_info_line(ctx, s_sun_info_buffer, small_font,
                     GRect(0, line_y, bounds.size.w, 18),
                     COLOR_TEXT_SHADOW, COLOR_TEXT_INFO);
    }
  }
}

// ============================================================================
// MAIN CANVAS UPDATE
// ============================================================================
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Background layers (back to front)
  draw_sky(ctx, bounds);
  draw_ocean_background(ctx, bounds);

  // Waves from back to front
  for (int i = 0; i < NUM_WAVES; i++) {
    draw_wave(ctx, &s_waves[i], bounds);
  }

  // Sand
  draw_sand(ctx, bounds);

  // Palm tree
  draw_palm_tree(ctx, bounds);

  // Time display (centered in sky area)
  GFont time_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  int time_y = PBL_IF_ROUND_ELSE(bounds.size.h / 2 - 60, bounds.size.h / 2 - 55);
  draw_text_with_shadow(ctx, s_time_buffer, time_font,
                        GRect(0, time_y, bounds.size.w, 50));

  // Date below time
  GFont date_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  draw_text_with_shadow(ctx, s_date_buffer, date_font,
                        GRect(0, time_y + 44, bounds.size.w, 22));

  // Tide info on the sand
  draw_tide_info(ctx, bounds);
}

// ============================================================================
// UPDATE FUNCTIONS
// ============================================================================
static void update_waves(void) {
  for (int i = 0; i < NUM_WAVES; i++) {
    s_waves[i].phase = (s_waves[i].phase + s_waves[i].speed) % TRIG_MAX_ANGLE;
  }
}

static void update_time(void) {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  if (!tick_time) return;

  s_current_hour = tick_time->tm_hour;
  s_current_min = tick_time->tm_min;

  strftime(s_time_buffer, sizeof(s_time_buffer),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);

  if (!clock_is_24h_style() && s_time_buffer[0] == '0') {
    memmove(s_time_buffer, &s_time_buffer[1], sizeof(s_time_buffer) - 1);
  }

  strftime(s_date_buffer, sizeof(s_date_buffer), "%a, %b %d", tick_time);
}

// ============================================================================
// ANIMATION (brief wave animation on tap or minute change)
// ============================================================================
static void animation_timer_callback(void *data) {
  update_waves();
  s_anim_elapsed += WAVE_ANIM_INTERVAL;

  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }

  // Stop animation after duration or on low battery
  if (s_anim_elapsed >= WAVE_ANIM_DURATION ||
      (s_battery_level <= LOW_BATTERY_THRESHOLD)) {
    s_is_animating = false;
    s_animation_timer = NULL;
    return;
  }

  s_animation_timer = app_timer_register(WAVE_ANIM_INTERVAL,
                                          animation_timer_callback, NULL);
}

static void start_wave_animation(void) {
  if (s_is_animating) return;
  s_is_animating = true;
  s_anim_elapsed = 0;
  s_animation_timer = app_timer_register(WAVE_ANIM_INTERVAL,
                                          animation_timer_callback, NULL);
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  #if DEV_MODE
  if (s_test_preset >= 0) {
    // In test mode, don't override the preset time
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
    return;
  }
  #endif

  update_time();

  // Brief wave animation every 5 minutes
  if (tick_time->tm_min % 5 == 0) {
    start_wave_animation();
  } else if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }

  // Request data refresh every 30 minutes
  if (tick_time->tm_min % 30 == 0) {
    DictionaryIterator *iter;
    AppMessageResult result = app_message_outbox_begin(&iter);
    if (result == APP_MSG_OK) {
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
    // In dev mode: tap cycles to next preset
    s_test_preset++;
    if (s_test_preset >= NUM_TEST_PRESETS) s_test_preset = 0;
    apply_test_preset(s_test_preset);
    APP_LOG(APP_LOG_LEVEL_INFO, "DEV preset %d: %s", s_test_preset,
            s_preset_names[s_test_preset]);
  }
  #endif
  start_wave_animation();
}

// ============================================================================
// APPMESSAGE HANDLERS
// ============================================================================
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *t;

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

  t = dict_find(iterator, MESSAGE_KEY_DISPLAY_MODE);
  if (t) {
    s_display_mode = (int)t->value->int32;
    persist_write_int(PERSIST_DISPLAY_MODE, s_display_mode);
  }

  s_tide.data_valid = true;

  // Redraw with new data
  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }

  // Persist data for offline use
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
  persist_write_bool(PERSIST_DATA_VALID, true);
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", (int)reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator,
                                    AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", (int)reason);
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success");
}

// ============================================================================
// LOAD PERSISTED DATA
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
    s_tide.data_valid = true;
  }
  if (persist_exists(PERSIST_DISPLAY_MODE)) {
    s_display_mode = persist_read_int(PERSIST_DISPLAY_MODE);
  }
}

// ============================================================================
// WINDOW HANDLERS
// ============================================================================
static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Canvas layer for all drawing
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  // Initialize scene
  init_waves(bounds.size.h);

  // Get initial battery state
  BatteryChargeState charge = battery_state_service_peek();
  s_battery_level = charge.charge_percent;

  // Initial time
  update_time();

  #if DEV_MODE
  // Auto-load first preset if no persisted data (e.g. fresh emulator)
  if (!s_tide.data_valid) {
    s_test_preset = 0;
    apply_test_preset(s_test_preset);
    s_display_mode = DISPLAY_MODE_DETAILED;
    APP_LOG(APP_LOG_LEVEL_INFO, "DEV: auto-loaded preset 0, tap to cycle");
  }
  #endif

  // Brief startup animation
  start_wave_animation();
}

static void main_window_unload(Window *window) {
  if (s_animation_timer) {
    app_timer_cancel(s_animation_timer);
    s_animation_timer = NULL;
  }
  if (s_canvas_layer) {
    layer_destroy(s_canvas_layer);
    s_canvas_layer = NULL;
  }
}

// ============================================================================
// APPLICATION LIFECYCLE
// ============================================================================
static void init(void) {
  load_persisted_data();

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  window_stack_push(s_main_window, true);

  // Subscribe to services
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_callback);
  accel_tap_service_subscribe(tap_handler);

  // AppMessage - register BEFORE opening
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_open(256, 64);
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
