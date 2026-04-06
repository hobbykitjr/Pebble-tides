/**
 * Pixel Tides - Pebble Round 2 (Gabbro) Watchface
 *
 * Pixel art beach: sun/moon tracking sky arc, weather, 3 detail levels,
 * animated waves toward shore, tide-variable water, airplane refresh,
 * seashell battery, beach sign BT indicator.
 *
 * Target: gabbro (260x260, round, 64-color)
 */

#include <pebble.h>
#include <stdlib.h>

// ============================================================================
// LAYOUT CONSTANTS (260x260 round)
// ============================================================================
#define NUM_WAVES         4
#define WAVE_ANIM_INTERVAL 60
#define WAVE_ANIM_DURATION 5000
#define LOW_BATTERY_THRESHOLD 20

// Vertical layout: sky -> time row -> ocean -> sand
#define TIME_ROW_Y_PCT    42   // Center of time text
#define SAND_LOW_Y_PCT    75   // Sand top at LOW tide
#define SAND_HIGH_Y_PCT   88   // Sand top at HIGH tide (ocean covers more)
#define SAND_MIN_Y        200  // Sand never goes below this (absolute px)

// Sun/moon arc (invisible path)
#define BODY_RADIUS       10
#define ARC_TOP_Y_PCT     12
#define ARC_BOT_Y_PCT     42   // At time row level (horizon)

// Airplane animation
#define PLANE_SPEED       4    // px per frame
#define PLANE_Y_PCT       22   // Y position in sky

// Detail levels
#define DETAIL_LOW    0
#define DETAIL_MED    1
#define DETAIL_HIGH   2

// Persist keys
#define P_TIDE_HEIGHT    0
#define P_TIDE_STATE     1
#define P_SUNRISE_H      2
#define P_SUNRISE_M      3
#define P_SUNSET_H       4
#define P_SUNSET_M       5
#define P_NEXT_HI_H      6
#define P_NEXT_HI_M      7
#define P_NEXT_LO_H      8
#define P_NEXT_LO_M      9
#define P_DATA_VALID      10
#define P_DETAIL_LEVEL    11
#define P_TEMPERATURE     12
#define P_WEATHER_CODE    13
#define P_PREV_TIDE_H     14
#define P_PREV_TIDE_M     15

// Weather codes
#define WX_CLEAR  0
#define WX_CLOUDY 1
#define WX_OVERCAST 2
#define WX_FOG    3
#define WX_RAIN   4
#define WX_STORM  5
#define WX_SNOW   6
#define WX_WIND   7

// Dev mode
#define DEV_MODE 1
#define NUM_TEST_PRESETS 6

// ============================================================================
// COLORS
// ============================================================================
#ifdef PBL_COLOR
  #define C_SKY_DAY     GColorPictonBlue
  #define C_SKY_LOW     GColorCeleste
  #define C_SKY_DAWN    GColorMelon
  #define C_SKY_DUSK    GColorOrange
  #define C_SKY_NIGHT   GColorOxfordBlue
  #define C_SUN         GColorYellow
  #define C_SUN_GLOW    GColorRajah
  #define C_MOON        GColorPastelYellow
  #define C_MOON_DARK   GColorOxfordBlue
  #define C_OCEAN_DEEP  GColorCobaltBlue
  #define C_OCEAN_MID   GColorBlue
  #define C_OCEAN_LIGHT GColorVividCerulean
  #define C_OCEAN_FOAM  GColorCeleste
  #define C_SAND        GColorFromHEX(0xD4B070)
  #define C_SAND_DARK   GColorFromHEX(0xB49650)
  #define C_SAND_WET    GColorFromHEX(0x907040)
  #define C_TEXT        GColorWhite
  #define C_TEXT_SHAD   GColorBlack
  #define C_TEXT_INFO   GColorCeleste
  #define C_WX_ICON    GColorWhite
  #define C_SHELL       GColorFromHEX(0xE8D0B0)
  #define C_SIGN_POST   GColorFromHEX(0x8B6914)
  #define C_SIGN_BOARD  GColorFromHEX(0xD4B070)
  #define C_PLANE       GColorWhite
  #define C_BANNER      GColorRajah
#else
  #define C_SKY_DAY     GColorWhite
  #define C_SKY_LOW     GColorWhite
  #define C_SKY_DAWN    GColorWhite
  #define C_SKY_DUSK    GColorLightGray
  #define C_SKY_NIGHT   GColorBlack
  #define C_SUN         GColorWhite
  #define C_SUN_GLOW    GColorWhite
  #define C_MOON        GColorWhite
  #define C_MOON_DARK   GColorBlack
  #define C_OCEAN_DEEP  GColorBlack
  #define C_OCEAN_MID   GColorBlack
  #define C_OCEAN_LIGHT GColorDarkGray
  #define C_OCEAN_FOAM  GColorWhite
  #define C_SAND        GColorLightGray
  #define C_SAND_DARK   GColorDarkGray
  #define C_SAND_WET    GColorDarkGray
  #define C_TEXT        GColorBlack
  #define C_TEXT_SHAD   GColorWhite
  #define C_TEXT_INFO   GColorBlack
  #define C_WX_ICON    GColorBlack
  #define C_SHELL       GColorLightGray
  #define C_SIGN_POST   GColorDarkGray
  #define C_SIGN_BOARD  GColorLightGray
  #define C_PLANE       GColorBlack
  #define C_BANNER      GColorLightGray
#endif

// ============================================================================
// DATA
// ============================================================================
typedef struct {
  int base_y;
  int32_t phase;
  int amplitude, speed;
  GColor color;
} Wave;

typedef struct {
  int sunrise_h, sunrise_m, sunset_h, sunset_m;
  int tide_pct, tide_state;          // 0-100, 0=falling/1=rising
  int next_hi_h, next_hi_m;
  int next_lo_h, next_lo_m;
  int prev_tide_h, prev_tide_m;
  int temperature, weather_code;
  char town[24];
  bool valid;
} Data;

// ============================================================================
// GLOBALS
// ============================================================================
static Window *s_window;
static Layer *s_canvas;
static AppTimer *s_anim_timer;

static int s_battery = 100;
static bool s_bt_connected = true;
static bool s_animating = false;
static int s_anim_ms = 0;

static Wave s_waves[NUM_WAVES];
static Data s_data = {
  .sunrise_h=6,.sunrise_m=15,.sunset_h=19,.sunset_m=45,
  .tide_pct=50,.tide_state=1,
  .next_hi_h=12,.next_hi_m=0,.next_lo_h=18,.next_lo_m=0,
  .prev_tide_h=6,.prev_tide_m=0,
  .temperature=72,.weather_code=WX_CLEAR,
  .town="Ocean City, NJ",.valid=false
};

static char s_time_buf[8], s_date_buf[16];
static char s_tide1[40], s_tide2[40];
static char s_sr_buf[8], s_ss_buf[8], s_temp_buf[8];

static int s_detail = DETAIL_MED;
static int s_hour = 12, s_min = 0;

// Airplane state
static bool s_plane_active = false;
static int s_plane_x = -50;
static bool s_data_refreshing = false;

// ============================================================================
// DEV MODE
// ============================================================================
#if DEV_MODE
static int s_preset = -1;
static const char *s_preset_names[] = {
  "Dawn Rise","Morn Hi","Noon Mid","Dusk Low","Night Fall","Night Rise"
};
typedef struct {
  int h,m, sr_h,sr_m, ss_h,ss_m;
  int tp,ts, nh_h,nh_m, nl_h,nl_m, pt_h,pt_m;
  int temp,wx;
} Preset;
static const Preset s_presets[NUM_TEST_PRESETS] = {
  { 6,15, 6,0,19,45, 25,1, 10,30,16,45, 4,0,   58,WX_FOG },
  { 9,30, 6,0,19,45, 95,0,  9,15,15,30, 9,15,  72,WX_CLOUDY },
  {12, 0, 6,0,19,45, 50,0, 18, 0,15,30, 9,30,  85,WX_CLEAR },
  {19,30, 6,0,19,45, 10,1, 22, 0,19,15, 19,15, 68,WX_WIND },
  {22, 0, 6,0,19,45, 60,0,  4,30,22,30, 21,30, 55,WX_RAIN },
  { 2,30, 6,0,19,45, 30,1,  4,30, 0,15, 0,15,  48,WX_SNOW },
};
static void apply_preset(int i) {
  if (i<0||i>=NUM_TEST_PRESETS) return;
  const Preset *p=&s_presets[i];
  s_hour=p->h; s_min=p->m;
  s_data.sunrise_h=p->sr_h; s_data.sunrise_m=p->sr_m;
  s_data.sunset_h=p->ss_h; s_data.sunset_m=p->ss_m;
  s_data.tide_pct=p->tp; s_data.tide_state=p->ts;
  s_data.next_hi_h=p->nh_h; s_data.next_hi_m=p->nh_m;
  s_data.next_lo_h=p->nl_h; s_data.next_lo_m=p->nl_m;
  s_data.prev_tide_h=p->pt_h; s_data.prev_tide_m=p->pt_m;
  s_data.temperature=p->temp; s_data.weather_code=p->wx;
  snprintf(s_data.town, sizeof(s_data.town), "Ocean City, NJ");
  s_data.valid=true;
  snprintf(s_time_buf,sizeof(s_time_buf),"%d:%02d",p->h,p->m);
  snprintf(s_date_buf,sizeof(s_date_buf),"DEV:%s",s_preset_names[i]);
}
#endif

// ============================================================================
// UTILITY
// ============================================================================
static int sun_progress(void) {
  int sr=s_data.sunrise_h*60+s_data.sunrise_m;
  int ss=s_data.sunset_h*60+s_data.sunset_m;
  int now=s_hour*60+s_min;
  if(now<sr||now>ss) return -1;
  int len=ss-sr; if(len<=0) return 50;
  return ((now-sr)*100)/len;
}

static bool is_night(void) { return sun_progress()<0; }

static int twilight_pct(void) {
  int p=sun_progress();
  if(p<0) return 0;
  if(p<15) return (15-p)*100/15;
  if(p>85) return (p-85)*100/15;
  return 0;
}

// Night progress: 0=sunset, 100=sunrise (for moon position)
static int night_progress(void) {
  int ss=s_data.sunset_h*60+s_data.sunset_m;
  int sr=s_data.sunrise_h*60+s_data.sunrise_m;
  int now=s_hour*60+s_min;
  int night_len;
  int elapsed;
  if(sr<ss) { // Normal: sunset PM, sunrise AM next day
    night_len=(24*60-ss)+sr;
    if(now>=ss) elapsed=now-ss;
    else elapsed=(24*60-ss)+now;
  } else {
    night_len=sr-ss;
    elapsed=now-ss;
  }
  if(night_len<=0) return 50;
  return (elapsed*100)/night_len;
}

// Moon phase: 0-29 (days into synodic month, 0=new, ~15=full)
static int moon_phase_day(void) {
  // Calculate from known new moon: Jan 6, 2000
  // Using simplified Julian day calculation
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  if (!tm) return 15;

  #if DEV_MODE
  // In dev mode, vary phase by preset for visual testing
  if (s_preset >= 0) {
    int phases[] = {1, 7, 15, 22, 25, 29}; // new->full->new
    return phases[s_preset];
  }
  #endif

  // Days since known new moon (Jan 6, 2000)
  int y=tm->tm_year+1900, m=tm->tm_mon+1, d=tm->tm_mday;
  // Simplified days since Jan 6 2000
  long days = (y-2000)*365 + (y-2000)/4 - (y-2000)/100 + (y-2000)/400;
  int month_days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
  for(int i=1;i<m;i++) days+=month_days[i];
  if(m>2 && ((y%4==0&&y%100!=0)||y%400==0)) days++;
  days += d - 6; // Jan 6
  // Synodic month ~29.53 days, use fixed point: 2953/100
  int phase = (int)((days * 100) % 2953);
  if (phase < 0) phase += 2953;
  return (phase * 30) / 2953; // 0-29
}

// Sand Y position based on tide
static int sand_y(int h) {
  int low = (h * SAND_LOW_Y_PCT) / 100;
  int high = (h * SAND_HIGH_Y_PCT) / 100;
  int y = low + (s_data.tide_pct * (high - low)) / 100;
  if (y > SAND_MIN_Y) y = SAND_MIN_Y;
  return y;
}

// Arc position for sky body (sun/moon)
static void arc_pos(GRect b, int progress, int *ox, int *oy) {
  int top = (b.size.h * ARC_TOP_Y_PCT) / 100;
  int bot = (b.size.h * ARC_BOT_Y_PCT) / 100;
  int ah = bot - top;
  *ox = b.size.w*20/100 + (b.size.w*60/100*progress)/100;
  int c = progress - 50;
  *oy = bot - (ah * (2500 - c*c)) / 2500;
}

// ============================================================================
// WAVE INIT
// ============================================================================
static void init_waves(int h) {
  int base = (h * SAND_LOW_Y_PCT) / 100;
  s_waves[0]=(Wave){base-5,  0,                    4,350,C_OCEAN_FOAM};
  s_waves[1]=(Wave){base-18, TRIG_MAX_ANGLE/4,     5,260,C_OCEAN_LIGHT};
  s_waves[2]=(Wave){base-32, TRIG_MAX_ANGLE/2,     4,190,C_OCEAN_MID};
  s_waves[3]=(Wave){base-48, TRIG_MAX_ANGLE*3/4,   3,140,C_OCEAN_DEEP};
}

// ============================================================================
// DRAWING: SKY
// ============================================================================
static void draw_sky(GContext *ctx, GRect b) {
  int sy = sand_y(b.size.h);
  bool night = is_night();
  int twi = twilight_pct();

  if (night) {
    graphics_context_set_fill_color(ctx, C_SKY_NIGHT);
    graphics_fill_rect(ctx, GRect(0,0,b.size.w,sy), 0, GCornerNone);
    // Stars
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorWhite);
    int s[][2]={{40,35},{95,28},{155,20},{210,32},{65,55},{135,42},
                {185,48},{50,70},{120,62},{220,58},{30,80},{170,72}};
    for(int i=0;i<12;i++)
      if(s[i][1]<sy-10) graphics_draw_pixel(ctx,GPoint(s[i][0],s[i][1]));
    #endif
  } else {
    GColor upper = (twi>50)?C_SKY_DAWN:C_SKY_DAY;
    GColor lower = (twi>30)?C_SKY_DUSK:C_SKY_LOW;
    int mid = sy*2/3;
    graphics_context_set_fill_color(ctx, upper);
    graphics_fill_rect(ctx, GRect(0,0,b.size.w,mid), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, lower);
    graphics_fill_rect(ctx, GRect(0,mid,b.size.w,sy-mid), 0, GCornerNone);
  }
}

// ============================================================================
// DRAWING: SUN
// ============================================================================
static void draw_sun(GContext *ctx, GRect b) {
  int prog = sun_progress();
  if (prog < 0) return;
  int sx, sy;
  arc_pos(b, prog, &sx, &sy);

  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, C_SUN_GLOW);
  graphics_fill_circle(ctx, GPoint(sx,sy), BODY_RADIUS+3);
  #endif
  graphics_context_set_fill_color(ctx, C_SUN);
  graphics_fill_circle(ctx, GPoint(sx,sy), BODY_RADIUS);

  #ifdef PBL_COLOR
  // Pixel rays
  graphics_context_set_fill_color(ctx, C_SUN_GLOW);
  int r=BODY_RADIUS+5;
  graphics_fill_rect(ctx,GRect(sx-1,sy-r-3,3,3),0,GCornerNone);
  graphics_fill_rect(ctx,GRect(sx-1,sy+r,3,3),0,GCornerNone);
  graphics_fill_rect(ctx,GRect(sx-r-3,sy-1,3,3),0,GCornerNone);
  graphics_fill_rect(ctx,GRect(sx+r,sy-1,3,3),0,GCornerNone);
  #endif
}

// ============================================================================
// DRAWING: MOON with phase
// ============================================================================
static void draw_moon(GContext *ctx, GRect b) {
  if (!is_night()) return;
  int prog = night_progress();
  int mx, my;
  arc_pos(b, prog, &mx, &my);

  int phase = moon_phase_day(); // 0-29
  int r = BODY_RADIUS;

  // Draw full moon circle
  graphics_context_set_fill_color(ctx, C_MOON);
  graphics_fill_circle(ctx, GPoint(mx,my), r);

  // Overlay dark circle to create phase shape
  // phase 0=new (all dark), 15=full (no dark), 29=almost new
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, C_MOON_DARK);
  #else
  graphics_context_set_fill_color(ctx, C_SKY_NIGHT);
  #endif

  if (phase < 15) {
    // Waxing: dark side on left, shrinking
    // Shadow offset: large at phase 0, zero at phase 15
    int offset = (15 - phase) * r * 2 / 15;
    if (phase < 2) {
      // Nearly new: cover almost all
      graphics_fill_circle(ctx, GPoint(mx, my), r-1);
    } else {
      graphics_fill_circle(ctx, GPoint(mx - offset + r, my), r);
    }
  } else {
    // Waning: dark side on right, growing
    int offset = (phase - 15) * r * 2 / 15;
    if (phase > 27) {
      graphics_fill_circle(ctx, GPoint(mx, my), r-1);
    } else {
      graphics_fill_circle(ctx, GPoint(mx + offset - r, my), r);
    }
  }
}

// ============================================================================
// DRAWING: WEATHER ICON
// ============================================================================
static void draw_wx(GContext *ctx, int x, int y, int code) {
  graphics_context_set_fill_color(ctx, C_WX_ICON);
  switch(code) {
    case WX_CLOUDY: case WX_OVERCAST:
      graphics_fill_circle(ctx,GPoint(x-4,y),5);
      graphics_fill_circle(ctx,GPoint(x+4,y-2),6);
      graphics_fill_circle(ctx,GPoint(x+10,y),4);
      graphics_fill_rect(ctx,GRect(x-8,y,22,6),0,GCornerNone);
      if(code==WX_OVERCAST){
        #ifdef PBL_COLOR
        graphics_context_set_fill_color(ctx,GColorLightGray);
        #endif
        graphics_fill_rect(ctx,GRect(x-10,y+4,26,4),0,GCornerNone);
      }
      break;
    case WX_FOG:
      graphics_context_set_stroke_color(ctx,C_WX_ICON);
      graphics_context_set_stroke_width(ctx,2);
      graphics_draw_line(ctx,GPoint(x-10,y-4),GPoint(x+10,y-4));
      graphics_draw_line(ctx,GPoint(x-8,y),GPoint(x+12,y));
      graphics_draw_line(ctx,GPoint(x-10,y+4),GPoint(x+10,y+4));
      break;
    case WX_RAIN:
      graphics_fill_circle(ctx,GPoint(x-3,y-4),4);
      graphics_fill_circle(ctx,GPoint(x+5,y-5),5);
      graphics_fill_rect(ctx,GRect(x-7,y-4,16,4),0,GCornerNone);
      #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx,GColorPictonBlue);
      #endif
      graphics_fill_rect(ctx,GRect(x-5,y+2,2,4),0,GCornerNone);
      graphics_fill_rect(ctx,GRect(x+1,y+4,2,4),0,GCornerNone);
      graphics_fill_rect(ctx,GRect(x+7,y+2,2,4),0,GCornerNone);
      break;
    case WX_STORM:
      graphics_fill_circle(ctx,GPoint(x-3,y-4),4);
      graphics_fill_circle(ctx,GPoint(x+5,y-5),5);
      graphics_fill_rect(ctx,GRect(x-7,y-4,16,4),0,GCornerNone);
      #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx,GColorYellow);
      #endif
      graphics_fill_rect(ctx,GRect(x+1,y+1,3,3),0,GCornerNone);
      graphics_fill_rect(ctx,GRect(x-1,y+4,3,3),0,GCornerNone);
      graphics_fill_rect(ctx,GRect(x+1,y+7,3,3),0,GCornerNone);
      break;
    case WX_SNOW:
      graphics_fill_circle(ctx,GPoint(x-3,y-4),4);
      graphics_fill_circle(ctx,GPoint(x+5,y-5),5);
      graphics_fill_rect(ctx,GRect(x-7,y-4,16,4),0,GCornerNone);
      graphics_fill_rect(ctx,GRect(x-4,y+2,1,3),0,GCornerNone);
      graphics_fill_rect(ctx,GRect(x-5,y+3,3,1),0,GCornerNone);
      graphics_fill_rect(ctx,GRect(x+5,y+4,1,3),0,GCornerNone);
      graphics_fill_rect(ctx,GRect(x+4,y+5,3,1),0,GCornerNone);
      graphics_fill_rect(ctx,GRect(x,y+5,1,3),0,GCornerNone);
      graphics_fill_rect(ctx,GRect(x-1,y+6,3,1),0,GCornerNone);
      break;
    case WX_WIND:
      graphics_context_set_stroke_color(ctx,C_WX_ICON);
      graphics_context_set_stroke_width(ctx,2);
      graphics_draw_line(ctx,GPoint(x-10,y-3),GPoint(x+10,y-5));
      graphics_draw_line(ctx,GPoint(x-8,y+2),GPoint(x+12,y));
      break;
    default: break; // Clear: sun is enough
  }
}

// ============================================================================
// DRAWING: OCEAN + WAVES
// ============================================================================
static void draw_ocean(GContext *ctx, GRect b) {
  int time_bottom = (b.size.h*TIME_ROW_Y_PCT)/100 + 35;
  int sy = sand_y(b.size.h);
  int ocean_h = sy - time_bottom;
  if (ocean_h <= 0) return;

  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, C_OCEAN_LIGHT);
  graphics_fill_rect(ctx,GRect(0,time_bottom,b.size.w,ocean_h/3),0,GCornerNone);
  graphics_context_set_fill_color(ctx, C_OCEAN_MID);
  graphics_fill_rect(ctx,GRect(0,time_bottom+ocean_h/3,b.size.w,ocean_h/3),0,GCornerNone);
  graphics_context_set_fill_color(ctx, C_OCEAN_DEEP);
  graphics_fill_rect(ctx,GRect(0,time_bottom+ocean_h*2/3,b.size.w,ocean_h/3+2),0,GCornerNone);
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx,GRect(0,time_bottom,b.size.w,ocean_h),0,GCornerNone);
  #endif
}

static void draw_wave(GContext *ctx, const Wave *w, GRect b) {
  int sy_val = sand_y(b.size.h);
  int tide_shift = (s_data.tide_pct * (sy_val - (b.size.h*SAND_LOW_Y_PCT)/100));
  int wy = w->base_y + tide_shift / 100;
  if (wy > sy_val) return;

  int16_t y_osc = (sin_lookup(w->phase) * w->amplitude) / TRIG_MAX_RATIO;
  int dy = wy + y_osc;
  graphics_context_set_fill_color(ctx, w->color);
  int step = 4;
  for (int x=0; x<b.size.w; x+=step) {
    int32_t a = (w->phase + (x*TRIG_MAX_ANGLE/b.size.w)) % TRIG_MAX_ANGLE;
    int16_t wobble = (sin_lookup(a)*2)/TRIG_MAX_RATIO;
    graphics_fill_rect(ctx,GRect(x,dy+wobble,step,5),0,GCornerNone);
  }
  // Foam on front wave
  #ifdef PBL_COLOR
  if (w->color.argb == C_OCEAN_FOAM.argb) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    for(int x=0;x<b.size.w;x+=step*3)
      graphics_fill_rect(ctx,GRect(x,dy-1,step,2),0,GCornerNone);
  }
  #endif
}

// ============================================================================
// DRAWING: SAND
// ============================================================================
static void draw_sand(GContext *ctx, GRect b) {
  int sy = sand_y(b.size.h);
  graphics_context_set_fill_color(ctx, C_SAND);
  graphics_fill_rect(ctx,GRect(0,sy,b.size.w,b.size.h-sy),0,GCornerNone);
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, C_SAND_WET);
  graphics_fill_rect(ctx,GRect(0,sy,b.size.w,5),0,GCornerNone);
  graphics_context_set_fill_color(ctx, C_SAND_DARK);
  int d[][2]={{30,8},{70,14},{120,10},{170,16},{220,12},{50,22},{100,20},{160,24},{210,18}};
  for(int i=0;i<9;i++){
    int py=sy+5+d[i][1];
    if(d[i][0]<b.size.w&&py<b.size.h)
      graphics_fill_rect(ctx,GRect(d[i][0],py,2,2),0,GCornerNone);
  }
  #endif
}

// ============================================================================
// DRAWING: BATTERY SEASHELLS (left of sand)
// ============================================================================
static void draw_shells(GContext *ctx, GRect b) {
  int sy = sand_y(b.size.h);
  int shell_y = sy + 8;
  if (shell_y + 10 > b.size.h) return;

  int num = (s_battery + 19) / 20; // 1-5 shells
  int base_x = 40; // Inset for round display

  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, C_SHELL);
  #else
  graphics_context_set_fill_color(ctx, GColorWhite);
  #endif

  for (int i = 0; i < num && i < 5; i++) {
    int sx = base_x + i * 10;
    // Small pixel shell: fan shape
    graphics_fill_circle(ctx, GPoint(sx, shell_y+3), 3);
    graphics_fill_rect(ctx, GRect(sx-2, shell_y+3, 5, 3), 0, GCornerNone);
  }

  // High detail: show percentage
  if (s_detail == DETAIL_HIGH) {
    char bat_buf[6];
    snprintf(bat_buf, sizeof(bat_buf), "%d%%", s_battery);
    GFont tiny = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_text_color(ctx, C_TEXT_INFO);
    graphics_draw_text(ctx, bat_buf, tiny,
      GRect(base_x, shell_y+8, 50, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

// ============================================================================
// DRAWING: BT DISCONNECT BEACH SIGN (right of sand)
// ============================================================================
static void draw_bt_sign(GContext *ctx, GRect b) {
  if (s_bt_connected) return;
  int sy = sand_y(b.size.h);
  int sx = b.size.w - 55; // Inset for round
  int post_y = sy + 2;
  if (post_y + 20 > b.size.h) return;

  // Post
  graphics_context_set_fill_color(ctx, C_SIGN_POST);
  graphics_fill_rect(ctx, GRect(sx+8, post_y, 3, 20), 0, GCornerNone);

  // Sign board
  graphics_context_set_fill_color(ctx, C_SIGN_BOARD);
  graphics_fill_rect(ctx, GRect(sx, post_y, 20, 14), 0, GCornerNone);

  // BT symbol (simplified pixel art "B" with strike)
  #ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx, GColorBlue);
  #else
  graphics_context_set_stroke_color(ctx, GColorBlack);
  #endif
  graphics_context_set_stroke_width(ctx, 1);
  // B shape
  graphics_draw_line(ctx, GPoint(sx+7,post_y+2), GPoint(sx+7,post_y+11));
  graphics_draw_line(ctx, GPoint(sx+7,post_y+2), GPoint(sx+13,post_y+6));
  graphics_draw_line(ctx, GPoint(sx+13,post_y+6), GPoint(sx+7,post_y+7));
  graphics_draw_line(ctx, GPoint(sx+7,post_y+7), GPoint(sx+13,post_y+11));
  graphics_draw_line(ctx, GPoint(sx+13,post_y+11), GPoint(sx+7,post_y+11));
  // Strike-through
  #ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx, GColorRed);
  #endif
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(sx+3,post_y+2), GPoint(sx+17,post_y+12));
}

// ============================================================================
// DRAWING: AIRPLANE (tap-to-refresh)
// ============================================================================
static void draw_airplane(GContext *ctx, GRect b) {
  if (!s_plane_active) return;
  int py = (b.size.h * PLANE_Y_PCT) / 100;
  int px = s_plane_x;

  // Plane body
  graphics_context_set_fill_color(ctx, C_PLANE);
  graphics_fill_rect(ctx, GRect(px, py, 12, 4), 0, GCornerNone);
  // Wings
  graphics_fill_rect(ctx, GRect(px+3, py-3, 6, 10), 0, GCornerNone);
  // Tail
  graphics_fill_rect(ctx, GRect(px-2, py-2, 4, 3), 0, GCornerNone);

  // Banner trailing behind plane
  if (px > 10) {
    graphics_context_set_fill_color(ctx, C_BANNER);
    int bx = px - 30;
    if (bx < 5) bx = 5;
    graphics_fill_rect(ctx, GRect(bx, py+5, px-bx-2, 10), 0, GCornerNone);

    // Banner text
    GFont tiny = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_text_color(ctx, C_TEXT_SHAD);
    const char *msg = s_data_refreshing ? "Updating..." : "Updated!";
    graphics_draw_text(ctx, msg, tiny,
      GRect(bx+2, py+3, px-bx-6, 14),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    // Banner string to plane
    graphics_context_set_stroke_color(ctx, C_PLANE);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, GPoint(px, py+4), GPoint(px-2, py+5));
  }
}

// ============================================================================
// DRAWING: TEXT HELPER
// ============================================================================
static void txt(GContext *ctx, const char *s, GFont f, GRect r, GTextAlignment a) {
  graphics_context_set_text_color(ctx, C_TEXT_SHAD);
  graphics_draw_text(ctx,s,f,GRect(r.origin.x+1,r.origin.y+1,r.size.w,r.size.h),
    GTextOverflowModeTrailingEllipsis,a,NULL);
  graphics_context_set_text_color(ctx, C_TEXT);
  graphics_draw_text(ctx,s,f,r,GTextOverflowModeTrailingEllipsis,a,NULL);
}

// ============================================================================
// DRAWING: HUD (detail-level aware)
// ============================================================================
static void draw_hud(GContext *ctx, GRect b) {
  int sy = sand_y(b.size.h);
  GFont f42 = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  GFont f18 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont f24 = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

  // -- TEMP + WEATHER (all levels) --
  snprintf(s_temp_buf, sizeof(s_temp_buf), "%d°", s_data.temperature);
  int temp_y = 42;
  if (s_data.weather_code != WX_CLEAR) {
    txt(ctx, s_temp_buf, f24, GRect(b.size.w/2-50, temp_y, 50, 28), GTextAlignmentRight);
    draw_wx(ctx, b.size.w/2+14, temp_y+12, s_data.weather_code);
  } else {
    txt(ctx, s_temp_buf, f24, GRect(0, temp_y, b.size.w, 28), GTextAlignmentCenter);
  }

  // -- TIME (all levels) --
  int time_y = (b.size.h * TIME_ROW_Y_PCT)/100 - 24;
  txt(ctx, s_time_buf, f42, GRect(0,time_y,b.size.w,50), GTextAlignmentCenter);

  // -- DATE (all levels) --
  txt(ctx, s_date_buf, f18, GRect(0,time_y+44,b.size.w,22), GTextAlignmentCenter);

  // -- SUNRISE/SUNSET (HIGH detail only) --
  if (s_detail >= DETAIL_HIGH) {
    snprintf(s_sr_buf,sizeof(s_sr_buf),"%d:%02d",s_data.sunrise_h,s_data.sunrise_m);
    snprintf(s_ss_buf,sizeof(s_ss_buf),"%d:%02d",s_data.sunset_h,s_data.sunset_m);
    int sun_y = time_y + 14;
    // Labels
    txt(ctx, "Sunrise", f14, GRect(35,sun_y-14,55,16), GTextAlignmentLeft);
    txt(ctx, "Sunset", f14, GRect(b.size.w-90,sun_y-14,55,16), GTextAlignmentRight);
    // Times
    txt(ctx, s_sr_buf, f14, GRect(35,sun_y,50,18), GTextAlignmentLeft);
    txt(ctx, s_ss_buf, f14, GRect(b.size.w-85,sun_y,50,18), GTextAlignmentRight);
  }

  // -- TIDE INFO (MED + HIGH) --
  if (s_detail >= DETAIL_MED && s_data.valid) {
    int now_m = s_hour*60+s_min;
    int nh,nm,fh,fm;
    const char *nl, *fl;

    if (s_data.tide_state==1) {
      nh=s_data.next_hi_h; nm=s_data.next_hi_m;
      fh=s_data.next_lo_h; fm=s_data.next_lo_m;
      nl="High Tide"; fl="Low Tide";
    } else {
      nh=s_data.next_lo_h; nm=s_data.next_lo_m;
      fh=s_data.next_hi_h; fm=s_data.next_hi_m;
      nl="Low Tide"; fl="High Tide";
    }
    int next_m=nh*60+nm, prev_m=s_data.prev_tide_h*60+s_data.prev_tide_m;
    int until=next_m-now_m; if(until<0) until+=24*60;
    int since=now_m-prev_m; if(since<0) since+=24*60;
    const char *pl = (s_data.tide_state==1)?"Low Tide":"High Tide";

    if (since<=60) {
      if(since==0) snprintf(s_tide1,sizeof(s_tide1),"%s now!",pl);
      else if(since<60) snprintf(s_tide1,sizeof(s_tide1),"%s %dm ago",pl,since);
      else snprintf(s_tide1,sizeof(s_tide1),"%s 1hr ago",pl);
      snprintf(s_tide2,sizeof(s_tide2),"%s %d:%02d",nl,nh,nm);
    } else if (until<=60) {
      if(until<60) snprintf(s_tide1,sizeof(s_tide1),"%s in %dm",nl,until);
      else snprintf(s_tide1,sizeof(s_tide1),"%s in 1hr",nl);
      snprintf(s_tide2,sizeof(s_tide2),"%s %d:%02d",fl,fh,fm);
    } else {
      snprintf(s_tide1,sizeof(s_tide1),"%s %d:%02d",nl,nh,nm);
      snprintf(s_tide2,sizeof(s_tide2),"%s %d:%02d",fl,fh,fm);
    }

    int iy = sy + 6;
    if (iy+34 > b.size.h) iy = b.size.h-36;

    // Town name (HIGH only)
    if (s_detail == DETAIL_HIGH && s_data.town[0]) {
      graphics_context_set_text_color(ctx, C_TEXT_INFO);
      graphics_draw_text(ctx, s_data.town, f14,
        GRect(0,iy,b.size.w,16), GTextOverflowModeTrailingEllipsis,
        GTextAlignmentCenter, NULL);
      iy += 14;
    }

    // Tide lines
    graphics_context_set_text_color(ctx, C_TEXT_SHAD);
    graphics_draw_text(ctx,s_tide1,f14,GRect(1,iy+1,b.size.w,18),
      GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    graphics_context_set_text_color(ctx, C_TEXT_INFO);
    graphics_draw_text(ctx,s_tide1,f14,GRect(0,iy,b.size.w,18),
      GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);

    if(s_tide2[0]) {
      graphics_context_set_text_color(ctx, C_TEXT_INFO);
      graphics_draw_text(ctx,s_tide2,f14,GRect(0,iy+14,b.size.w,18),
        GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    }
  }
}

// ============================================================================
// MAIN CANVAS
// ============================================================================
static void canvas_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  draw_sky(ctx, b);
  draw_sun(ctx, b);
  draw_moon(ctx, b);
  draw_ocean(ctx, b);
  for(int i=NUM_WAVES-1;i>=0;i--) draw_wave(ctx,&s_waves[i],b);
  draw_sand(ctx, b);
  draw_shells(ctx, b);
  draw_bt_sign(ctx, b);
  draw_airplane(ctx, b);
  draw_hud(ctx, b);
}

// ============================================================================
// UPDATE
// ============================================================================
static void update_waves(void) {
  for(int i=0;i<NUM_WAVES;i++)
    s_waves[i].phase=(s_waves[i].phase+s_waves[i].speed)%TRIG_MAX_ANGLE;
}

static void update_time(void) {
  time_t t=time(NULL); struct tm *tm=localtime(&t);
  if(!tm) return;
  s_hour=tm->tm_hour; s_min=tm->tm_min;
  strftime(s_time_buf,sizeof(s_time_buf),clock_is_24h_style()?"%H:%M":"%I:%M",tm);
  if(!clock_is_24h_style()&&s_time_buf[0]=='0')
    memmove(s_time_buf,&s_time_buf[1],sizeof(s_time_buf)-1);
  strftime(s_date_buf,sizeof(s_date_buf),"%a, %b %d",tm);
}

// ============================================================================
// ANIMATION
// ============================================================================
static void anim_cb(void *data) {
  update_waves();
  s_anim_ms += WAVE_ANIM_INTERVAL;

  // Airplane movement
  if (s_plane_active) {
    s_plane_x += PLANE_SPEED;
    if (s_plane_x > 300) { // Off screen
      s_plane_active = false;
      s_data_refreshing = false;
    }
  }

  if(s_canvas) layer_mark_dirty(s_canvas);

  #if DEV_MODE
  bool stop=false;
  #else
  bool stop=(s_anim_ms>=WAVE_ANIM_DURATION && !s_plane_active);
  #endif
  if(stop || (s_battery<=LOW_BATTERY_THRESHOLD && !s_plane_active)) {
    s_animating=false; s_anim_timer=NULL; return;
  }
  s_anim_timer=app_timer_register(WAVE_ANIM_INTERVAL,anim_cb,NULL);
}

static void start_anim(void) {
  if(s_animating) return;
  s_animating=true; s_anim_ms=0;
  s_anim_timer=app_timer_register(WAVE_ANIM_INTERVAL,anim_cb,NULL);
}

// ============================================================================
// EVENTS
// ============================================================================
static void tick_cb(struct tm *t, TimeUnits u) {
  #if DEV_MODE
  if(s_preset>=0){if(s_canvas)layer_mark_dirty(s_canvas);return;}
  #endif
  update_time();
  if(t->tm_min%5==0) start_anim();
  else if(s_canvas) layer_mark_dirty(s_canvas);
  if(t->tm_min%30==0) {
    DictionaryIterator *it;
    if(app_message_outbox_begin(&it)==APP_MSG_OK){
      dict_write_uint8(it,MESSAGE_KEY_REQUEST_DATA,1);
      app_message_outbox_send();
    }
  }
}

static void battery_cb(BatteryChargeState s) { s_battery=s.charge_percent; }

static void bt_cb(bool connected) {
  s_bt_connected = connected;
  if (!connected) vibes_short_pulse();
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void tap_cb(AccelAxisType axis, int32_t dir) {
  #if DEV_MODE
  if(s_preset>=0||!s_data.valid){
    s_preset++;
    if(s_preset>=NUM_TEST_PRESETS) s_preset=0;
    apply_preset(s_preset);
    // Fly airplane on preset transition
    s_plane_active = true;
    s_plane_x = -50;
    s_data_refreshing = false; // Show "Updated!" immediately
    APP_LOG(APP_LOG_LEVEL_INFO,"DEV %d: %s",s_preset,s_preset_names[s_preset]);
  }
  #else
  // Launch airplane + data refresh
  if (!s_plane_active) {
    s_plane_active = true;
    s_plane_x = -50;
    s_data_refreshing = true;
    // Request fresh data
    DictionaryIterator *it;
    if(app_message_outbox_begin(&it)==APP_MSG_OK){
      dict_write_uint8(it,MESSAGE_KEY_REQUEST_DATA,1);
      app_message_outbox_send();
    }
  }
  #endif
  start_anim();
}

// ============================================================================
// APPMESSAGE
// ============================================================================
static void inbox_cb(DictionaryIterator *it, void *ctx) {
  Tuple *t;
  #if DEV_MODE
  if(s_preset>=0){
    t=dict_find(it,MESSAGE_KEY_DISPLAY_MODE);
    if(t) s_detail=(int)t->value->int32;
    return;
  }
  #endif
  t=dict_find(it,MESSAGE_KEY_TIDE_HEIGHT);   if(t) s_data.tide_pct=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_TIDE_STATE);    if(t) s_data.tide_state=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_SUNRISE_HOUR);  if(t) s_data.sunrise_h=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_SUNRISE_MIN);   if(t) s_data.sunrise_m=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_SUNSET_HOUR);   if(t) s_data.sunset_h=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_SUNSET_MIN);    if(t) s_data.sunset_m=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_NEXT_HIGH_HOUR);if(t) s_data.next_hi_h=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_NEXT_HIGH_MIN); if(t) s_data.next_hi_m=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_NEXT_LOW_HOUR); if(t) s_data.next_lo_h=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_NEXT_LOW_MIN);  if(t) s_data.next_lo_m=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_PREV_TIDE_HOUR);if(t) s_data.prev_tide_h=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_PREV_TIDE_MIN); if(t) s_data.prev_tide_m=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_DISPLAY_MODE);  if(t) s_detail=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_TEMPERATURE);   if(t) s_data.temperature=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_WEATHER_CODE);  if(t) s_data.weather_code=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_TOWN_NAME);
  if(t) snprintf(s_data.town,sizeof(s_data.town),"%s",t->value->cstring);

  s_data.valid=true;
  s_data_refreshing=false; // Airplane banner changes to "Updated!"
  if(s_canvas) layer_mark_dirty(s_canvas);

  persist_write_int(P_TIDE_HEIGHT,s_data.tide_pct);
  persist_write_int(P_TIDE_STATE,s_data.tide_state);
  persist_write_int(P_SUNRISE_H,s_data.sunrise_h);
  persist_write_int(P_SUNRISE_M,s_data.sunrise_m);
  persist_write_int(P_SUNSET_H,s_data.sunset_h);
  persist_write_int(P_SUNSET_M,s_data.sunset_m);
  persist_write_int(P_NEXT_HI_H,s_data.next_hi_h);
  persist_write_int(P_NEXT_HI_M,s_data.next_hi_m);
  persist_write_int(P_NEXT_LO_H,s_data.next_lo_h);
  persist_write_int(P_NEXT_LO_M,s_data.next_lo_m);
  persist_write_int(P_PREV_TIDE_H,s_data.prev_tide_h);
  persist_write_int(P_PREV_TIDE_M,s_data.prev_tide_m);
  persist_write_int(P_TEMPERATURE,s_data.temperature);
  persist_write_int(P_WEATHER_CODE,s_data.weather_code);
  persist_write_bool(P_DATA_VALID,true);
  persist_write_int(P_DETAIL_LEVEL,s_detail);
}
static void inbox_drop(AppMessageResult r,void *c){
  APP_LOG(APP_LOG_LEVEL_ERROR,"drop:%d",(int)r);
}
static void outbox_fail(DictionaryIterator *i,AppMessageResult r,void *c){
  APP_LOG(APP_LOG_LEVEL_ERROR,"fail:%d",(int)r);
}
static void outbox_ok(DictionaryIterator *i,void *c){}

// ============================================================================
// PERSIST
// ============================================================================
static void load_data(void) {
  if(persist_exists(P_DATA_VALID)&&persist_read_bool(P_DATA_VALID)){
    s_data.tide_pct=persist_read_int(P_TIDE_HEIGHT);
    s_data.tide_state=persist_read_int(P_TIDE_STATE);
    s_data.sunrise_h=persist_read_int(P_SUNRISE_H);
    s_data.sunrise_m=persist_read_int(P_SUNRISE_M);
    s_data.sunset_h=persist_read_int(P_SUNSET_H);
    s_data.sunset_m=persist_read_int(P_SUNSET_M);
    s_data.next_hi_h=persist_read_int(P_NEXT_HI_H);
    s_data.next_hi_m=persist_read_int(P_NEXT_HI_M);
    s_data.next_lo_h=persist_read_int(P_NEXT_LO_H);
    s_data.next_lo_m=persist_read_int(P_NEXT_LO_M);
    if(persist_exists(P_PREV_TIDE_H)){
      s_data.prev_tide_h=persist_read_int(P_PREV_TIDE_H);
      s_data.prev_tide_m=persist_read_int(P_PREV_TIDE_M);
    }
    s_data.valid=true;
  }
  if(persist_exists(P_TEMPERATURE)) s_data.temperature=persist_read_int(P_TEMPERATURE);
  if(persist_exists(P_WEATHER_CODE)) s_data.weather_code=persist_read_int(P_WEATHER_CODE);
  if(persist_exists(P_DETAIL_LEVEL)) s_detail=persist_read_int(P_DETAIL_LEVEL);
}

// ============================================================================
// WINDOW
// ============================================================================
static void win_load(Window *w) {
  Layer *wl=window_get_root_layer(w);
  GRect b=layer_get_bounds(wl);
  s_canvas=layer_create(b);
  layer_set_update_proc(s_canvas,canvas_proc);
  layer_add_child(wl,s_canvas);
  init_waves(b.size.h);
  s_battery=battery_state_service_peek().charge_percent;
  s_bt_connected=connection_service_peek_pebble_app_connection();
  update_time();
  #if DEV_MODE
  if(!s_data.valid){
    s_preset=0; apply_preset(0);
    s_detail=DETAIL_HIGH;
    APP_LOG(APP_LOG_LEVEL_INFO,"DEV: preset 0");
  }
  #endif
  start_anim();
}

static void win_unload(Window *w) {
  if(s_anim_timer){app_timer_cancel(s_anim_timer);s_anim_timer=NULL;}
  if(s_canvas){layer_destroy(s_canvas);s_canvas=NULL;}
}

// ============================================================================
// LIFECYCLE
// ============================================================================
static void init(void) {
  srand(time(NULL));
  load_data();
  s_window=window_create();
  window_set_background_color(s_window,GColorBlack);
  window_set_window_handlers(s_window,(WindowHandlers){.load=win_load,.unload=win_unload});
  window_stack_push(s_window,true);
  tick_timer_service_subscribe(MINUTE_UNIT,tick_cb);
  battery_state_service_subscribe(battery_cb);
  connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler=bt_cb});
  accel_tap_service_subscribe(tap_cb);
  app_message_register_inbox_received(inbox_cb);
  app_message_register_inbox_dropped(inbox_drop);
  app_message_register_outbox_failed(outbox_fail);
  app_message_register_outbox_sent(outbox_ok);
  app_message_open(1024,64);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  accel_tap_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) { init(); app_event_loop(); deinit(); return 0; }
