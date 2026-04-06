/**
 * Pixel Tides - Pebble Round 2 (Gabbro) Watchface
 * Target: gabbro (260x260, round, 64-color)
 */

#include <pebble.h>
#include <stdlib.h>

// ============================================================================
// CONSTANTS
// ============================================================================
#define NUM_WAVES         4
#define WAVE_ANIM_INTERVAL 60
#define WAVE_ANIM_DURATION 5000
#define LOW_BATTERY_THRESHOLD 20

// Layout (260px round)
#define TIME_ROW_Y_PCT    42
#define SAND_LOW_Y_PCT    75
#define SAND_HIGH_Y_PCT   88
#define SAND_MIN_Y        235  // Don't clip into bottom bezel

// Sun/moon arc
#define BODY_RADIUS       10
#define ARC_TOP_Y_PCT     5
#define ARC_BOT_Y_PCT     38

// Airplane
#define PLANE_SPEED       4
#define PLANE_Y           72   // Fixed Y px — below temp, above time

// Detail levels
#define DETAIL_LOW    0
#define DETAIL_MED    1
#define DETAIL_HIGH   2

// Persist keys
#define P_TIDE_HEIGHT  0
#define P_TIDE_STATE   1
#define P_SUNRISE_H    2
#define P_SUNRISE_M    3
#define P_SUNSET_H     4
#define P_SUNSET_M     5
#define P_NEXT_HI_H    6
#define P_NEXT_HI_M    7
#define P_NEXT_LO_H    8
#define P_NEXT_LO_M    9
#define P_DATA_VALID   10
#define P_DETAIL       11
#define P_TEMPERATURE  12
#define P_WEATHER      13
#define P_PREV_TIDE_H  14
#define P_PREV_TIDE_M  15

// Weather
#define WX_CLEAR 0
#define WX_CLOUDY 1
#define WX_OVERCAST 2
#define WX_FOG 3
#define WX_RAIN 4
#define WX_STORM 5
#define WX_SNOW 6
#define WX_WIND 7

#define DEV_MODE 1
#define NUM_PRESETS 6

// ============================================================================
// COLORS
// ============================================================================
#ifdef PBL_COLOR
  #define C_SKY       GColorPictonBlue
  #define C_SKY_DAWN  GColorMelon
  #define C_SKY_DUSK  GColorOrange
  #define C_SKY_NIGHT GColorOxfordBlue
  #define C_SUN       GColorYellow
  #define C_GLOW      GColorRajah
  #define C_MOON      GColorPastelYellow
  #define C_MOON_DK   GColorOxfordBlue
  #define C_OCEAN     GColorCobaltBlue
  #define C_FOAM      GColorCeleste
  #define C_SAND      GColorFromHEX(0xD4B070)
  #define C_SAND_DK   GColorFromHEX(0xB49650)
  #define C_SAND_WET  GColorFromHEX(0x907040)
  #define C_TEXT      GColorWhite
  #define C_SHAD      GColorBlack
  #define C_INFO      GColorCeleste
  #define C_SHELL     GColorFromHEX(0xE8D0B0)
  #define C_SIGN_P    GColorFromHEX(0x8B6914)
  #define C_SIGN_B    GColorFromHEX(0xD4B070)
  #define C_PLANE     GColorWhite
#else
  #define C_SKY       GColorWhite
  #define C_SKY_DAWN  GColorWhite
  #define C_SKY_DUSK  GColorLightGray
  #define C_SKY_NIGHT GColorBlack
  #define C_SUN       GColorWhite
  #define C_GLOW      GColorWhite
  #define C_MOON      GColorWhite
  #define C_MOON_DK   GColorBlack
  #define C_OCEAN     GColorBlack
  #define C_FOAM      GColorWhite
  #define C_SAND      GColorLightGray
  #define C_SAND_DK   GColorDarkGray
  #define C_SAND_WET  GColorDarkGray
  #define C_TEXT      GColorBlack
  #define C_SHAD      GColorWhite
  #define C_INFO      GColorBlack
  #define C_SHELL     GColorLightGray
  #define C_SIGN_P    GColorDarkGray
  #define C_SIGN_B    GColorLightGray
  #define C_PLANE     GColorBlack
#endif

// ============================================================================
// DATA
// ============================================================================
typedef struct { int base_y; int32_t phase; int amp,spd; } Wave;

typedef struct {
  int sr_h,sr_m, ss_h,ss_m;
  int tide_pct,tide_st;
  int nhi_h,nhi_m, nlo_h,nlo_m;
  int pt_h,pt_m;
  int temp,wx;
  char town[24];
  bool valid;
} Data;

// ============================================================================
// GLOBALS
// ============================================================================
static Window *s_win;
static Layer *s_canvas;
static AppTimer *s_timer;

static int s_bat=100;
static bool s_bt=true, s_anim=false;
static int s_anim_ms=0;
static Wave s_waves[NUM_WAVES];
static Data s_d={.sr_h=6,.sr_m=15,.ss_h=19,.ss_m=45,.tide_pct=50,.tide_st=1,
  .nhi_h=12,.nhi_m=0,.nlo_h=18,.nlo_m=0,.pt_h=6,.pt_m=0,
  .temp=72,.wx=WX_CLEAR,.town="Ocean City, NJ",.valid=false};

static char s_tbuf[8],s_dbuf[16],s_t1[40],s_t2[40],s_sr[8],s_ss[8],s_tmp[8];
static int s_det=DETAIL_MED, s_hr=12, s_mn=0;

static bool s_plane=false;
static int s_px=-50;
static bool s_refreshing=false;

// ============================================================================
// DEV MODE
// ============================================================================
#if DEV_MODE
static int s_pre=-1;
static const char *s_pnames[]={"Dawn","MornHi","Noon","Dusk","NightF","NightR"};
typedef struct {
  int h,m,srh,srm,ssh,ssm,tp,ts,nhh,nhm,nlh,nlm,pth,ptm,tmp,wx;
} Pre;
static const Pre s_pres[NUM_PRESETS]={
  { 6,15, 6,0,19,45, 25,1, 10,30,16,45, 4,0,   58,WX_FOG},
  { 9,30, 6,0,19,45, 95,0,  9,15,15,30, 9,15,  72,WX_CLOUDY},
  {12, 0, 6,0,19,45, 50,0, 18, 0,15,30, 9,30,  85,WX_CLEAR},
  {19,30, 6,0,19,45, 10,1, 22, 0,19,15, 19,15, 68,WX_WIND},
  {22, 0, 6,0,19,45, 60,0,  4,30,22,30, 21,30, 55,WX_RAIN},
  { 2,30, 6,0,19,45, 30,1,  4,30, 0,15, 0,15,  48,WX_SNOW},
};
static void apply_pre(int i) {
  if(i<0||i>=NUM_PRESETS) return;
  const Pre *p=&s_pres[i];
  s_hr=p->h; s_mn=p->m;
  s_d.sr_h=p->srh; s_d.sr_m=p->srm; s_d.ss_h=p->ssh; s_d.ss_m=p->ssm;
  s_d.tide_pct=p->tp; s_d.tide_st=p->ts;
  s_d.nhi_h=p->nhh; s_d.nhi_m=p->nhm; s_d.nlo_h=p->nlh; s_d.nlo_m=p->nlm;
  s_d.pt_h=p->pth; s_d.pt_m=p->ptm;
  s_d.temp=p->tmp; s_d.wx=p->wx;
  snprintf(s_d.town,sizeof(s_d.town),"Ocean City, NJ");
  s_d.valid=true;
  if(clock_is_24h_style()) snprintf(s_tbuf,sizeof(s_tbuf),"%d:%02d",p->h,p->m);
  else { int h=p->h%12; if(!h)h=12; snprintf(s_tbuf,sizeof(s_tbuf),"%d:%02d",h,p->m); }
  snprintf(s_dbuf,sizeof(s_dbuf),"DEV:%s",s_pnames[i]);
}
#endif

// ============================================================================
// UTILITY
// ============================================================================
static int sun_prog(void) {
  int sr=s_d.sr_h*60+s_d.sr_m, ss=s_d.ss_h*60+s_d.ss_m, now=s_hr*60+s_mn;
  if(now<sr||now>ss) return -1;
  int len=ss-sr; return len>0?((now-sr)*100)/len:50;
}
static bool is_night(void) { return sun_prog()<0; }
static int twi_pct(void) {
  int p=sun_prog(); if(p<0) return 0;
  if(p<15) return (15-p)*100/15;
  if(p>85) return (p-85)*100/15;
  return 0;
}
static int night_prog(void) {
  int ss=s_d.ss_h*60+s_d.ss_m, sr=s_d.sr_h*60+s_d.sr_m, now=s_hr*60+s_mn;
  int nl,el;
  if(sr<ss){nl=(24*60-ss)+sr; el=(now>=ss)?now-ss:(24*60-ss)+now;}
  else{nl=sr-ss; el=now-ss;}
  return nl>0?(el*100)/nl:50;
}
static int moon_phase(void) {
  #if DEV_MODE
  if(s_pre>=0){int ph[]={1,7,15,22,25,29}; return ph[s_pre];}
  #endif
  time_t t=time(NULL); struct tm *tm=localtime(&t); if(!tm) return 15;
  int y=tm->tm_year+1900,m=tm->tm_mon+1,d=tm->tm_mday;
  long days=(y-2000)*365+(y-2000)/4-(y-2000)/100+(y-2000)/400;
  int md[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
  for(int i=1;i<m;i++) days+=md[i];
  if(m>2&&((y%4==0&&y%100!=0)||y%400==0)) days++;
  days+=d-6;
  int ph=(int)((days*100)%2953); if(ph<0) ph+=2953;
  return (ph*30)/2953;
}
static int sand_y(int h) {
  int lo=(h*SAND_LOW_Y_PCT)/100, hi=(h*SAND_HIGH_Y_PCT)/100;
  int y=lo+(s_d.tide_pct*(hi-lo))/100;
  return y>SAND_MIN_Y?SAND_MIN_Y:y;
}
static void arc_xy(GRect b,int prog,int *ox,int *oy) {
  int top=(b.size.h*ARC_TOP_Y_PCT)/100, bot=(b.size.h*ARC_BOT_Y_PCT)/100;
  int ah=bot-top;
  *ox=b.size.w*10/100+(b.size.w*80/100*prog)/100;
  int c=prog-50; *oy=bot-(ah*(2500-c*c))/2500;
}

// ============================================================================
// WAVES
// ============================================================================
static void init_waves(int h) {
  int base=(h*SAND_LOW_Y_PCT)/100;
  s_waves[0]=(Wave){base-5, 0, 4, 350};
  s_waves[1]=(Wave){base-18, TRIG_MAX_ANGLE/4, 5, 260};
  s_waves[2]=(Wave){base-32, TRIG_MAX_ANGLE/2, 4, 190};
  s_waves[3]=(Wave){base-48, TRIG_MAX_ANGLE*3/4, 3, 140};
}

// ============================================================================
// DRAW: SKY (simple)
// ============================================================================
static void draw_sky(GContext *ctx, GRect b) {
  int sy=sand_y(b.size.h);
  if(is_night()) {
    graphics_context_set_fill_color(ctx,C_SKY_NIGHT);
    graphics_fill_rect(ctx,GRect(0,0,b.size.w,sy),0,GCornerNone);
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx,GColorWhite);
    int st[][2]={{40,35},{95,28},{155,20},{210,32},{65,55},{135,42},
                 {185,48},{50,70},{120,62},{220,58},{30,80},{170,72}};
    for(int i=0;i<12;i++) if(st[i][1]<sy-10)
      graphics_draw_pixel(ctx,GPoint(st[i][0],st[i][1]));
    #endif
  } else {
    int twi=twi_pct();
    GColor col = (twi>30) ? C_SKY_DAWN : C_SKY;
    graphics_context_set_fill_color(ctx,col);
    graphics_fill_rect(ctx,GRect(0,0,b.size.w,sy),0,GCornerNone);
    // Warm band at horizon during dawn/dusk
    #ifdef PBL_COLOR
    if(twi>20) {
      graphics_context_set_fill_color(ctx,C_SKY_DUSK);
      int band_y=sy-20; if(band_y<0) band_y=0;
      graphics_fill_rect(ctx,GRect(0,band_y,b.size.w,20),0,GCornerNone);
    }
    #endif
  }
}

// ============================================================================
// DRAW: SUN
// ============================================================================
static void draw_sun(GContext *ctx, GRect b) {
  int p=sun_prog(); if(p<0) return;
  int sx,sy; arc_xy(b,p,&sx,&sy);
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx,C_GLOW);
  graphics_fill_circle(ctx,GPoint(sx,sy),BODY_RADIUS+3);
  #endif
  graphics_context_set_fill_color(ctx,C_SUN);
  graphics_fill_circle(ctx,GPoint(sx,sy),BODY_RADIUS);
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx,C_GLOW);
  int r=BODY_RADIUS+5;
  graphics_fill_rect(ctx,GRect(sx-1,sy-r-3,3,3),0,GCornerNone);
  graphics_fill_rect(ctx,GRect(sx-1,sy+r,3,3),0,GCornerNone);
  graphics_fill_rect(ctx,GRect(sx-r-3,sy-1,3,3),0,GCornerNone);
  graphics_fill_rect(ctx,GRect(sx+r,sy-1,3,3),0,GCornerNone);
  #endif
}

// ============================================================================
// DRAW: MOON
// ============================================================================
static void draw_moon(GContext *ctx, GRect b) {
  if(!is_night()) return;
  int p=night_prog(); int mx,my; arc_xy(b,p,&mx,&my);
  int ph=moon_phase(), r=BODY_RADIUS;
  graphics_context_set_fill_color(ctx,C_MOON);
  graphics_fill_circle(ctx,GPoint(mx,my),r);
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx,C_MOON_DK);
  #else
  graphics_context_set_fill_color(ctx,C_SKY_NIGHT);
  #endif
  if(ph<15){int off=(15-ph)*r*2/15;
    if(ph<2) graphics_fill_circle(ctx,GPoint(mx,my),r-1);
    else graphics_fill_circle(ctx,GPoint(mx-off+r,my),r);
  } else {int off=(ph-15)*r*2/15;
    if(ph>27) graphics_fill_circle(ctx,GPoint(mx,my),r-1);
    else graphics_fill_circle(ctx,GPoint(mx+off-r,my),r);
  }
}

// ============================================================================
// DRAW: WEATHER ICON
// ============================================================================
static void draw_wx(GContext *ctx, int x, int y, int code) {
  graphics_context_set_fill_color(ctx,C_TEXT);
  switch(code){
    case WX_CLOUDY: case WX_OVERCAST:
      graphics_fill_circle(ctx,GPoint(x-4,y),5);
      graphics_fill_circle(ctx,GPoint(x+4,y-2),6);
      graphics_fill_circle(ctx,GPoint(x+10,y),4);
      graphics_fill_rect(ctx,GRect(x-8,y,22,6),0,GCornerNone);
      break;
    case WX_FOG:
      graphics_context_set_stroke_color(ctx,C_TEXT);
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
      break;
    case WX_WIND:
      graphics_context_set_stroke_color(ctx,C_TEXT);
      graphics_context_set_stroke_width(ctx,2);
      graphics_draw_line(ctx,GPoint(x-10,y-3),GPoint(x+10,y-5));
      graphics_draw_line(ctx,GPoint(x-8,y+2),GPoint(x+12,y));
      break;
    default: break;
  }
}

// ============================================================================
// DRAW: OCEAN (solid blue + white foam waves)
// ============================================================================
static void draw_ocean(GContext *ctx, GRect b) {
  int tb=PLANE_Y+20+44+22;  // Below time+date (matches HUD positioning)
  int sy=sand_y(b.size.h);
  if(sy<=tb) return;

  // Solid ocean color
  graphics_context_set_fill_color(ctx,C_OCEAN);
  graphics_fill_rect(ctx,GRect(0,tb,b.size.w,sy-tb),0,GCornerNone);
}

static void draw_wave(GContext *ctx, const Wave *w, GRect b) {
  int sy=sand_y(b.size.h);
  int shift=(s_d.tide_pct*(sy-(b.size.h*SAND_LOW_Y_PCT)/100));
  int wy=w->base_y+shift/100;
  if(wy>sy) return;

  int16_t yo=(sin_lookup(w->phase)*w->amp)/TRIG_MAX_RATIO;
  int dy=wy+yo;

  // Broken wave segments — pixel art style with gaps
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx,C_FOAM);
  #else
  graphics_context_set_fill_color(ctx,GColorWhite);
  #endif
  int step=4;
  int seg_id=0;
  for(int x=0;x<b.size.w;x+=step){
    int32_t a=(w->phase+(x*TRIG_MAX_ANGLE/b.size.w))%TRIG_MAX_ANGLE;
    int16_t wb=(sin_lookup(a)*3)/TRIG_MAX_RATIO;
    // Skip some segments for broken/natural look (vary by wave + position)
    seg_id++;
    int hash = (w->base_y * 7 + x * 13 + (w->phase/1000)) % 10;
    if(hash < 3) continue;  // ~30% gaps
    int h = 2 + (hash % 2);  // Vary height 2-3px
    graphics_fill_rect(ctx,GRect(x,dy+wb,step,h),0,GCornerNone);
  }
  // Extra white foam on front wave
  if(w==&s_waves[0]) {
    graphics_context_set_fill_color(ctx,GColorWhite);
    for(int x=2;x<b.size.w;x+=step*3){
      int32_t a=(w->phase+(x*TRIG_MAX_ANGLE/b.size.w))%TRIG_MAX_ANGLE;
      int16_t wb=(sin_lookup(a)*2)/TRIG_MAX_RATIO;
      graphics_fill_rect(ctx,GRect(x,dy+wb-1,step-1,2),0,GCornerNone);
    }
  }
}

// ============================================================================
// DRAW: SAND
// ============================================================================
static void draw_sand(GContext *ctx, GRect b) {
  int sy=sand_y(b.size.h);
  graphics_context_set_fill_color(ctx,C_SAND);
  graphics_fill_rect(ctx,GRect(0,sy,b.size.w,b.size.h-sy),0,GCornerNone);
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx,C_SAND_WET);
  graphics_fill_rect(ctx,GRect(0,sy,b.size.w,5),0,GCornerNone);
  graphics_context_set_fill_color(ctx,C_SAND_DK);
  int d[][2]={{30,8},{70,14},{120,10},{170,16},{220,12},{50,22},{100,20},{160,24}};
  for(int i=0;i<8;i++){int py=sy+5+d[i][1];
    if(d[i][0]<b.size.w&&py<b.size.h)
      graphics_fill_rect(ctx,GRect(d[i][0],py,2,2),0,GCornerNone);}
  #endif
}

// ============================================================================
// DRAW: BATTERY SHELLS
// ============================================================================
static void draw_shells(GContext *ctx, GRect b) {
  // Fixed position near bottom-left (always visible regardless of tide)
  int shy=b.size.h-38;
  int num=(s_bat+19)/20;
  int bx=45;  // Inset for round bezel
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx,C_SHELL);
  #else
  graphics_context_set_fill_color(ctx,GColorWhite);
  #endif
  for(int i=0;i<num&&i<5;i++){
    int sx=bx+i*10;
    graphics_fill_circle(ctx,GPoint(sx,shy+3),3);
    graphics_fill_rect(ctx,GRect(sx-2,shy+3,5,3),0,GCornerNone);
  }
  if(s_det==DETAIL_HIGH){
    char bb[6]; snprintf(bb,sizeof(bb),"%d%%",s_bat);
    GFont f=fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_text_color(ctx,C_INFO);
    graphics_draw_text(ctx,bb,f,GRect(bx,shy+8,50,16),
      GTextOverflowModeTrailingEllipsis,GTextAlignmentLeft,NULL);
  }
}

// ============================================================================
// DRAW: BT SIGN
// ============================================================================
static void draw_bt(GContext *ctx, GRect b) {
  if(s_bt) return;
  // Fixed position near bottom-right (always visible)
  int sx=b.size.w-60, py=b.size.h-38;
  graphics_context_set_fill_color(ctx,C_SIGN_P);
  graphics_fill_rect(ctx,GRect(sx+8,py,3,20),0,GCornerNone);
  graphics_context_set_fill_color(ctx,C_SIGN_B);
  graphics_fill_rect(ctx,GRect(sx,py,20,14),0,GCornerNone);
  #ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx,GColorBlue);
  #else
  graphics_context_set_stroke_color(ctx,GColorBlack);
  #endif
  graphics_context_set_stroke_width(ctx,1);
  graphics_draw_line(ctx,GPoint(sx+7,py+2),GPoint(sx+7,py+11));
  graphics_draw_line(ctx,GPoint(sx+7,py+2),GPoint(sx+13,py+6));
  graphics_draw_line(ctx,GPoint(sx+13,py+6),GPoint(sx+7,py+7));
  graphics_draw_line(ctx,GPoint(sx+7,py+7),GPoint(sx+13,py+11));
  graphics_draw_line(ctx,GPoint(sx+13,py+11),GPoint(sx+7,py+11));
  #ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx,GColorRed);
  #endif
  graphics_context_set_stroke_width(ctx,2);
  graphics_draw_line(ctx,GPoint(sx+3,py+2),GPoint(sx+17,py+12));
}

// ============================================================================
// DRAW: AIRPLANE (plane body at same Y as banner)
// ============================================================================
static void draw_plane(GContext *ctx, GRect b) {
  if(!s_plane) return;
  int py=PLANE_Y;
  int px=s_px;

  // Red banner (drawn first, behind plane)
  int blen=75;
  int bx=px-blen;
  if(bx<2) bx=2;
  int bw=px-bx-4;

  if(bw>10) {
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx,GColorRed);
    #else
    graphics_context_set_fill_color(ctx,C_SHAD);
    #endif
    graphics_fill_rect(ctx,GRect(bx,py-2,bw,14),0,GCornerNone);
    // Pennant tail
    graphics_fill_rect(ctx,GRect(bx-3,py,3,10),0,GCornerNone);

    GFont f=fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_text_color(ctx,C_TEXT);
    const char *msg=s_refreshing?"Updating...":"Updated!";
    graphics_draw_text(ctx,msg,f,GRect(bx+2,py-3,bw-4,16),
      GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
  }

  // Plane body (same Y as banner center)
  graphics_context_set_fill_color(ctx,C_PLANE);
  graphics_fill_rect(ctx,GRect(px,py,14,5),0,GCornerNone);
  // Wings
  graphics_fill_rect(ctx,GRect(px+4,py-4,6,13),0,GCornerNone);
  // Tail
  graphics_fill_rect(ctx,GRect(px-3,py-3,5,4),0,GCornerNone);

  // String
  if(bw>10){
    graphics_context_set_stroke_color(ctx,C_PLANE);
    graphics_context_set_stroke_width(ctx,1);
    graphics_draw_line(ctx,GPoint(px,py+2),GPoint(px-4,py+2));
  }
}

// ============================================================================
// TEXT HELPER
// ============================================================================
static void txt(GContext *ctx, const char *s, GFont f, GRect r, GTextAlignment a) {
  graphics_context_set_text_color(ctx,C_SHAD);
  graphics_draw_text(ctx,s,f,GRect(r.origin.x+1,r.origin.y+1,r.size.w,r.size.h),
    GTextOverflowModeTrailingEllipsis,a,NULL);
  graphics_context_set_text_color(ctx,C_TEXT);
  graphics_draw_text(ctx,s,f,r,GTextOverflowModeTrailingEllipsis,a,NULL);
}

// ============================================================================
// DRAW: HUD
// ============================================================================
static void draw_hud(GContext *ctx, GRect b) {
  int sy=sand_y(b.size.h);
  GFont f42=fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  GFont f18=fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f14=fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont f24=fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

  // -- TEMP + WEATHER (all levels) — both at same visual center --
  snprintf(s_tmp,sizeof(s_tmp),"%d°",s_d.temp);
  int temp_y=32;  // Higher up near top
  if(s_d.wx!=WX_CLEAR){
    // Temp text baseline + icon center aligned at same visual midpoint
    txt(ctx,s_tmp,f24,GRect(b.size.w/2-48,temp_y,48,28),GTextAlignmentRight);
    draw_wx(ctx,b.size.w/2+16,temp_y+12,s_d.wx);
  } else {
    txt(ctx,s_tmp,f24,GRect(0,temp_y,b.size.w,28),GTextAlignmentCenter);
  }

  // -- TIME (just below plane banner area) --
  int ty=PLANE_Y+20;  // Right below the banner Y
  txt(ctx,s_tbuf,f42,GRect(0,ty,b.size.w,50),GTextAlignmentCenter);

  // -- DATE --
  txt(ctx,s_dbuf,f18,GRect(0,ty+42,b.size.w,22),GTextAlignmentCenter);

  // -- SUNRISE/SUNSET at 9 and 3 o'clock (HIGH only) --
  if(s_det>=DETAIL_HIGH) {
    snprintf(s_sr,sizeof(s_sr),"%d:%02d",s_d.sr_h,s_d.sr_m);
    snprintf(s_ss,sizeof(s_ss),"%d:%02d",s_d.ss_h,s_d.ss_m);
    int mid_y=b.size.h/2;  // True center = 3/9 o'clock
    // Left (9 o'clock) - Sunrise
    txt(ctx,"Sunrise",f14,GRect(8,mid_y-18,60,16),GTextAlignmentLeft);
    txt(ctx,s_sr,f14,GRect(8,mid_y-4,50,18),GTextAlignmentLeft);
    // Right (3 o'clock) - Sunset
    txt(ctx,"Sunset",f14,GRect(b.size.w-68,mid_y-18,60,16),GTextAlignmentRight);
    txt(ctx,s_ss,f14,GRect(b.size.w-58,mid_y-4,50,18),GTextAlignmentRight);
  }

  // -- TIDE INFO (MED + HIGH) --
  if(s_det>=DETAIL_MED && s_d.valid) {
    int now_m=s_hr*60+s_mn;
    int nh,nm,fh,fm; const char *nl,*fl;
    if(s_d.tide_st==1){
      nh=s_d.nhi_h;nm=s_d.nhi_m;fh=s_d.nlo_h;fm=s_d.nlo_m;nl="High Tide";fl="Low Tide";
    }else{
      nh=s_d.nlo_h;nm=s_d.nlo_m;fh=s_d.nhi_h;fm=s_d.nhi_m;nl="Low Tide";fl="High Tide";
    }
    int nm2=nh*60+nm, pm=s_d.pt_h*60+s_d.pt_m;
    int until=nm2-now_m; if(until<0) until+=24*60;
    int since=now_m-pm; if(since<0) since+=24*60;
    const char *pl=(s_d.tide_st==1)?"Low Tide":"High Tide";

    if(since<=60){
      if(!since) snprintf(s_t1,sizeof(s_t1),"%s now!",pl);
      else if(since<60) snprintf(s_t1,sizeof(s_t1),"%s %dm ago",pl,since);
      else snprintf(s_t1,sizeof(s_t1),"%s 1hr ago",pl);
      snprintf(s_t2,sizeof(s_t2),"%s %d:%02d",nl,nh,nm);
    } else if(until<=60){
      if(until<60) snprintf(s_t1,sizeof(s_t1),"%s in %dm",nl,until);
      else snprintf(s_t1,sizeof(s_t1),"%s in 1hr",nl);
      snprintf(s_t2,sizeof(s_t2),"%s %d:%02d",fl,fh,fm);
    } else {
      snprintf(s_t1,sizeof(s_t1),"%s %d:%02d",nl,nh,nm);
      snprintf(s_t2,sizeof(s_t2),"%s %d:%02d",fl,fh,fm);
    }

    int iy=sy+6; if(iy+34>b.size.h) iy=b.size.h-36;

    if(s_det==DETAIL_HIGH && s_d.town[0]){
      graphics_context_set_text_color(ctx,C_INFO);
      graphics_draw_text(ctx,s_d.town,f14,GRect(0,iy,b.size.w,16),
        GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
      iy+=14;
    }
    graphics_context_set_text_color(ctx,C_SHAD);
    graphics_draw_text(ctx,s_t1,f14,GRect(1,iy+1,b.size.w,18),
      GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    graphics_context_set_text_color(ctx,C_INFO);
    graphics_draw_text(ctx,s_t1,f14,GRect(0,iy,b.size.w,18),
      GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    if(s_t2[0]){
      graphics_context_set_text_color(ctx,C_INFO);
      graphics_draw_text(ctx,s_t2,f14,GRect(0,iy+14,b.size.w,18),
        GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    }
  }
}

// ============================================================================
// CANVAS
// ============================================================================
static void canvas_proc(Layer *l, GContext *ctx) {
  GRect b=layer_get_bounds(l);
  draw_sky(ctx,b); draw_sun(ctx,b); draw_moon(ctx,b);
  draw_ocean(ctx,b);
  for(int i=NUM_WAVES-1;i>=0;i--) draw_wave(ctx,&s_waves[i],b);
  draw_sand(ctx,b); draw_shells(ctx,b); draw_bt(ctx,b);
  draw_plane(ctx,b); draw_hud(ctx,b);
}

// ============================================================================
// UPDATE
// ============================================================================
static void upd_waves(void){for(int i=0;i<NUM_WAVES;i++)
  s_waves[i].phase=(s_waves[i].phase+s_waves[i].spd)%TRIG_MAX_ANGLE;}

static void upd_time(void){
  time_t t=time(NULL); struct tm *tm=localtime(&t); if(!tm) return;
  s_hr=tm->tm_hour; s_mn=tm->tm_min;
  strftime(s_tbuf,sizeof(s_tbuf),clock_is_24h_style()?"%H:%M":"%I:%M",tm);
  if(!clock_is_24h_style()&&s_tbuf[0]=='0')
    memmove(s_tbuf,&s_tbuf[1],sizeof(s_tbuf)-1);
  strftime(s_dbuf,sizeof(s_dbuf),"%a, %b %d",tm);
}

// ============================================================================
// ANIMATION
// ============================================================================
static void anim_cb(void *data){
  upd_waves();
  s_anim_ms+=WAVE_ANIM_INTERVAL;
  if(s_plane){s_px+=PLANE_SPEED; if(s_px>300){s_plane=false;s_refreshing=false;}}
  if(s_canvas) layer_mark_dirty(s_canvas);
  #if DEV_MODE
  bool stop=false;
  #else
  bool stop=(s_anim_ms>=WAVE_ANIM_DURATION && !s_plane);
  #endif
  if(stop||(s_bat<=LOW_BATTERY_THRESHOLD&&!s_plane)){
    s_anim=false;s_timer=NULL;return;}
  s_timer=app_timer_register(WAVE_ANIM_INTERVAL,anim_cb,NULL);
}
static void start_anim(void){
  if(s_anim) return;
  s_anim=true; s_anim_ms=0;
  s_timer=app_timer_register(WAVE_ANIM_INTERVAL,anim_cb,NULL);
}

// ============================================================================
// EVENTS
// ============================================================================
static void tick_cb(struct tm *t, TimeUnits u){
  #if DEV_MODE
  if(s_pre>=0){if(s_canvas)layer_mark_dirty(s_canvas);return;}
  #endif
  upd_time();
  if(t->tm_min%5==0) start_anim();
  else if(s_canvas) layer_mark_dirty(s_canvas);
  if(t->tm_min%30==0){
    DictionaryIterator *it;
    if(app_message_outbox_begin(&it)==APP_MSG_OK){
      dict_write_uint8(it,MESSAGE_KEY_REQUEST_DATA,1);
      app_message_outbox_send();
    }
  }
}
static void bat_cb(BatteryChargeState s){
  s_bat=s.charge_percent;
  if(s_canvas) layer_mark_dirty(s_canvas);
}
static void bt_cb(bool c){
  s_bt=c; if(!c) vibes_short_pulse();
  if(s_canvas) layer_mark_dirty(s_canvas);
}
static void tap_cb(AccelAxisType a, int32_t d){
  #if DEV_MODE
  if(s_pre>=0||!s_d.valid){
    s_pre++; if(s_pre>=NUM_PRESETS) s_pre=0;
    apply_pre(s_pre);
    s_plane=true; s_px=-50; s_refreshing=false;
    APP_LOG(APP_LOG_LEVEL_INFO,"DEV %d",s_pre);
  }
  #else
  if(!s_plane){
    s_plane=true;s_px=-50;s_refreshing=true;
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
static void inbox_cb(DictionaryIterator *it, void *c){
  Tuple *t;
  #if DEV_MODE
  if(s_pre>=0){
    t=dict_find(it,MESSAGE_KEY_DISPLAY_MODE);
    if(t){s_det=(int)t->value->int32; persist_write_int(P_DETAIL,s_det);}
    return;
  }
  #endif
  t=dict_find(it,MESSAGE_KEY_TIDE_HEIGHT);   if(t) s_d.tide_pct=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_TIDE_STATE);    if(t) s_d.tide_st=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_SUNRISE_HOUR);  if(t) s_d.sr_h=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_SUNRISE_MIN);   if(t) s_d.sr_m=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_SUNSET_HOUR);   if(t) s_d.ss_h=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_SUNSET_MIN);    if(t) s_d.ss_m=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_NEXT_HIGH_HOUR);if(t) s_d.nhi_h=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_NEXT_HIGH_MIN); if(t) s_d.nhi_m=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_NEXT_LOW_HOUR); if(t) s_d.nlo_h=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_NEXT_LOW_MIN);  if(t) s_d.nlo_m=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_PREV_TIDE_HOUR);if(t) s_d.pt_h=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_PREV_TIDE_MIN); if(t) s_d.pt_m=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_DISPLAY_MODE);  if(t) s_det=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_TEMPERATURE);   if(t) s_d.temp=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_WEATHER_CODE);  if(t) s_d.wx=(int)t->value->int32;
  t=dict_find(it,MESSAGE_KEY_TOWN_NAME);
  if(t) snprintf(s_d.town,sizeof(s_d.town),"%s",t->value->cstring);

  s_d.valid=true;
  s_refreshing=false;
  if(s_canvas) layer_mark_dirty(s_canvas);

  // Persist everything
  persist_write_int(P_TIDE_HEIGHT,s_d.tide_pct);
  persist_write_int(P_TIDE_STATE,s_d.tide_st);
  persist_write_int(P_SUNRISE_H,s_d.sr_h);
  persist_write_int(P_SUNRISE_M,s_d.sr_m);
  persist_write_int(P_SUNSET_H,s_d.ss_h);
  persist_write_int(P_SUNSET_M,s_d.ss_m);
  persist_write_int(P_NEXT_HI_H,s_d.nhi_h);
  persist_write_int(P_NEXT_HI_M,s_d.nhi_m);
  persist_write_int(P_NEXT_LO_H,s_d.nlo_h);
  persist_write_int(P_NEXT_LO_M,s_d.nlo_m);
  persist_write_int(P_PREV_TIDE_H,s_d.pt_h);
  persist_write_int(P_PREV_TIDE_M,s_d.pt_m);
  persist_write_int(P_TEMPERATURE,s_d.temp);
  persist_write_int(P_WEATHER,s_d.wx);
  persist_write_int(P_DETAIL,s_det);
  persist_write_bool(P_DATA_VALID,true);
}
static void drop_cb(AppMessageResult r,void *c){}
static void fail_cb(DictionaryIterator *i,AppMessageResult r,void *c){}
static void sent_cb(DictionaryIterator *i,void *c){}

// ============================================================================
// PERSIST
// ============================================================================
static void load_data(void){
  if(persist_exists(P_DATA_VALID)&&persist_read_bool(P_DATA_VALID)){
    s_d.tide_pct=persist_read_int(P_TIDE_HEIGHT);
    s_d.tide_st=persist_read_int(P_TIDE_STATE);
    s_d.sr_h=persist_read_int(P_SUNRISE_H);
    s_d.sr_m=persist_read_int(P_SUNRISE_M);
    s_d.ss_h=persist_read_int(P_SUNSET_H);
    s_d.ss_m=persist_read_int(P_SUNSET_M);
    s_d.nhi_h=persist_read_int(P_NEXT_HI_H);
    s_d.nhi_m=persist_read_int(P_NEXT_HI_M);
    s_d.nlo_h=persist_read_int(P_NEXT_LO_H);
    s_d.nlo_m=persist_read_int(P_NEXT_LO_M);
    if(persist_exists(P_PREV_TIDE_H)){
      s_d.pt_h=persist_read_int(P_PREV_TIDE_H);
      s_d.pt_m=persist_read_int(P_PREV_TIDE_M);
    }
    s_d.valid=true;
  }
  if(persist_exists(P_TEMPERATURE)) s_d.temp=persist_read_int(P_TEMPERATURE);
  if(persist_exists(P_WEATHER)) s_d.wx=persist_read_int(P_WEATHER);
  if(persist_exists(P_DETAIL)) s_det=persist_read_int(P_DETAIL);
}

// ============================================================================
// WINDOW
// ============================================================================
static void win_load(Window *w){
  Layer *wl=window_get_root_layer(w);
  GRect b=layer_get_bounds(wl);
  s_canvas=layer_create(b);
  layer_set_update_proc(s_canvas,canvas_proc);
  layer_add_child(wl,s_canvas);
  init_waves(b.size.h);
  s_bat=battery_state_service_peek().charge_percent;
  s_bt=connection_service_peek_pebble_app_connection();
  upd_time();
  #if DEV_MODE
  if(!s_d.valid){s_pre=0;apply_pre(0);APP_LOG(APP_LOG_LEVEL_INFO,"DEV:0");}
  // Don't override s_det — use persisted or default value
  #endif
  start_anim();
}
static void win_unload(Window *w){
  if(s_timer){app_timer_cancel(s_timer);s_timer=NULL;}
  if(s_canvas){layer_destroy(s_canvas);s_canvas=NULL;}
}

// ============================================================================
// LIFECYCLE
// ============================================================================
static void init(void){
  srand(time(NULL));
  load_data();
  s_win=window_create();
  window_set_background_color(s_win,GColorBlack);
  window_set_window_handlers(s_win,(WindowHandlers){.load=win_load,.unload=win_unload});
  window_stack_push(s_win,true);
  tick_timer_service_subscribe(MINUTE_UNIT,tick_cb);
  battery_state_service_subscribe(bat_cb);
  connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler=bt_cb});
  accel_tap_service_subscribe(tap_cb);
  app_message_register_inbox_received(inbox_cb);
  app_message_register_inbox_dropped(drop_cb);
  app_message_register_outbox_failed(fail_cb);
  app_message_register_outbox_sent(sent_cb);
  app_message_open(1024,64);
}
static void deinit(void){
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  accel_tap_service_unsubscribe();
  window_destroy(s_win);
}
int main(void){init();app_event_loop();deinit();return 0;}
