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
#define SAND_LOW_Y_PCT    65   // More beach at low tide
#define SAND_HIGH_Y_PCT   85   // Ocean covers more at high tide
#define SAND_MIN_Y        235  // Don't clip into bottom bezel

// Sun/moon arc
#define BODY_RADIUS       10
#define ARC_TOP_Y_PCT     5
#define ARC_BOT_Y_PCT     38

// Airplane
#define PLANE_SPEED       4
#define PLANE_Y           58   // Fixed Y px — below temp, above time

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
    #ifdef PBL_COLOR
    if(twi>20) {
      // Dawn/dusk: ombré gradient — continuous color sweep, no visible bands
      GColor sc[] = {
        GColorOxfordBlue,
        GColorImperialPurple,
        GColorPurple,
        GColorMagenta,
        GColorSunsetOrange,
        C_SKY_DUSK,
        GColorRajah,
      };
      int n=7;
      // Draw every 2px row, smoothly interpolating across all colors
      for(int y=0;y<sy;y+=2){
        // Position in color array as fixed-point (0 to (n-1)*1000)
        int pos=(y*(n-1)*1000)/sy;
        int idx=pos/1000;           // Base color index
        int frac=pos%1000;          // 0-999 blend fraction
        if(idx>=n-1){idx=n-2;frac=999;}
        // Checkerboard dither: even rows bias toward current, odd toward next
        // This creates smooth sub-pixel blending between adjacent colors
        int threshold=500;
        if((y/2)%2==0) threshold=550;  // Even: slightly favor current
        else threshold=450;             // Odd: slightly favor next
        GColor c=(frac<threshold)?sc[idx]:sc[idx+1];
        graphics_context_set_fill_color(ctx,c);
        graphics_fill_rect(ctx,GRect(0,y,b.size.w,2),0,GCornerNone);
      }
    } else {
      // Normal day: solid blue sky
      graphics_context_set_fill_color(ctx,C_SKY);
      graphics_fill_rect(ctx,GRect(0,0,b.size.w,sy),0,GCornerNone);
    }
    #else
    graphics_context_set_fill_color(ctx,C_SKY);
    graphics_fill_rect(ctx,GRect(0,0,b.size.w,sy),0,GCornerNone);
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
  int tb=PLANE_Y+18+44+20;  // Below time+date
  int sy=sand_y(b.size.h);
  if(sy<=tb) return;
  int oh=sy-tb;

  // Ocean gradient with dithered band edges
  #ifdef PBL_COLOR
  {
    GColor oc[]={C_OCEAN, GColorBlue, GColorVividCerulean, GColorTiffanyBlue};
    int n=4;
    int bh=oh/n; if(bh<1) bh=1;
    for(int i=0;i<n;i++){
      int y0=tb+bh*i;
      int h1=(i==n-1)?oh-bh*i:bh;
      graphics_context_set_fill_color(ctx,oc[i]);
      graphics_fill_rect(ctx,GRect(0,y0,b.size.w,h1),0,GCornerNone);
      // Dither 10px at each band boundary
      if(i<n-1){
        int dz=10;
        int ds=y0+h1-dz;
        for(int dy=0;dy<dz;dy++){
          bool use_next;
          if(dy<dz/3) use_next=((dy%3)==0);
          else if(dy<dz*2/3) use_next=((dy%2)==0);
          else use_next=((dy%3)!=0);
          if(use_next){
            graphics_context_set_fill_color(ctx,oc[i+1]);
            graphics_fill_rect(ctx,GRect(0,ds+dy,b.size.w,1),0,GCornerNone);
          }
        }
      }
    }
  }
  #else
  graphics_context_set_fill_color(ctx,GColorBlack);
  graphics_fill_rect(ctx,GRect(0,tb,b.size.w,oh),0,GCornerNone);
  #endif
}

// Waves: only white foam at the shoreline, not scattered through ocean
static void draw_waves(GContext *ctx, GRect b) {
  int sy=sand_y(b.size.h);
  int step=4;

  // Foam line 1: right at shoreline (front wave)
  int16_t yo1=(sin_lookup(s_waves[0].phase)*s_waves[0].amp)/TRIG_MAX_RATIO;
  int foam1_y=sy-3+yo1;

  graphics_context_set_fill_color(ctx,GColorWhite);
  for(int x=0;x<b.size.w;x+=step){
    int32_t a=(s_waves[0].phase+(x*TRIG_MAX_ANGLE*2/b.size.w))%TRIG_MAX_ANGLE;
    int16_t wb=(sin_lookup(a)*3)/TRIG_MAX_RATIO;
    graphics_fill_rect(ctx,GRect(x,foam1_y+wb,step,3),0,GCornerNone);
  }

  // Foam line 2: a few px above shoreline (second wave)
  int16_t yo2=(sin_lookup(s_waves[1].phase)*s_waves[1].amp)/TRIG_MAX_RATIO;
  int foam2_y=sy-12+yo2;

  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx,C_FOAM);
  #endif
  for(int x=0;x<b.size.w;x+=step){
    int32_t a=(s_waves[1].phase+(x*TRIG_MAX_ANGLE*3/b.size.w))%TRIG_MAX_ANGLE;
    int16_t wb=(sin_lookup(a)*2)/TRIG_MAX_RATIO;
    // Broken segments — not a solid line
    int hash=(x*13+s_waves[1].phase/600)%10;
    if(hash<4) continue;
    graphics_fill_rect(ctx,GRect(x,foam2_y+wb,step,2),0,GCornerNone);
  }

  // One subtle lighter line in mid-ocean for depth
  int mid_ocean=(sy+PLANE_Y+80)/2;
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx,GColorPictonBlue);
  int16_t yo3=(sin_lookup(s_waves[2].phase)*2)/TRIG_MAX_RATIO;
  for(int x=0;x<b.size.w;x+=step*2){
    int32_t a=(s_waves[2].phase+(x*TRIG_MAX_ANGLE/b.size.w))%TRIG_MAX_ANGLE;
    int16_t wb=(sin_lookup(a)*1)/TRIG_MAX_RATIO;
    int hash=(x*7+s_waves[2].phase/500)%10;
    if(hash<6) continue;
    graphics_fill_rect(ctx,GRect(x,mid_ocean+yo3+wb,step,1),0,GCornerNone);
  }
  #endif

}

// ============================================================================
// DRAW: SAND
// ============================================================================
static void draw_sand(GContext *ctx, GRect b) {
  int sy=sand_y(b.size.h);

  // Main sand fill
  graphics_context_set_fill_color(ctx,C_SAND);
  graphics_fill_rect(ctx,GRect(0,sy,b.size.w,b.size.h-sy),0,GCornerNone);

  #ifdef PBL_COLOR
  // Wavy wet sand — follows the same wave shape as foam line
  graphics_context_set_fill_color(ctx,C_SAND_WET);
  int step=4;
  for(int x=0;x<b.size.w;x+=step){
    int32_t a=(s_waves[0].phase+(x*TRIG_MAX_ANGLE*2/b.size.w))%TRIG_MAX_ANGLE;
    int16_t wb=(sin_lookup(a)*3)/TRIG_MAX_RATIO;
    int16_t yo=(sin_lookup(s_waves[0].phase)*s_waves[0].amp)/TRIG_MAX_RATIO;
    int wy=sy+yo+wb;
    // Wet sand band: 8px tall following wave contour
    if(wy>=sy-2) graphics_fill_rect(ctx,GRect(x,wy,step,8),0,GCornerNone);
  }

  // Subtle darker sand band below wet sand (gradient effect)
  graphics_context_set_fill_color(ctx,C_SAND_DK);
  for(int x=0;x<b.size.w;x+=step){
    int32_t a=(s_waves[0].phase+(x*TRIG_MAX_ANGLE*2/b.size.w))%TRIG_MAX_ANGLE;
    int16_t wb=(sin_lookup(a)*2)/TRIG_MAX_RATIO;
    int16_t yo=(sin_lookup(s_waves[0].phase)*s_waves[0].amp)/TRIG_MAX_RATIO;
    int wy=sy+yo+wb+8;
    if(wy>=sy) graphics_fill_rect(ctx,GRect(x,wy,step,4),0,GCornerNone);
  }

  // Sand texture dots
  graphics_context_set_fill_color(ctx,C_SAND_DK);
  int d[][2]={{30,16},{70,22},{120,18},{170,24},{220,20},{50,28},{100,26},{160,30}};
  for(int i=0;i<8;i++){int py=sy+d[i][1];
    if(d[i][0]<b.size.w&&py<b.size.h)
      graphics_fill_rect(ctx,GRect(d[i][0],py,2,2),0,GCornerNone);}
  #endif
}

// ============================================================================
// DRAW: BATTERY UMBRELLA
// ============================================================================
static void draw_battery(GContext *ctx, GRect b) {
  // Beach umbrella — canopy shows battery as colored segments
  // Bigger, more visible, no radial fill (avoids crash)
  int cx=65;              // Center X (inset for round bezel)
  int pole_bot=b.size.h-42;
  int pole_top=pole_bot-18;
  int r=18;               // Canopy radius
  int canopy_y=pole_top-3; // Canopy center Y

  // Pole (brown stick)
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx,C_SIGN_P);
  #else
  graphics_context_set_fill_color(ctx,GColorDarkGray);
  #endif
  graphics_fill_rect(ctx,GRect(cx-1,pole_top,3,pole_bot-pole_top),0,GCornerNone);

  // Canopy: draw as a series of vertical slices (left to right)
  // Battery determines how many slices are colored vs gray
  int total_slices=r*2;  // One slice per pixel width
  int filled_slices=(s_bat*total_slices)/100;

  for(int dx=-r;dx<=r;dx++){
    // Height of this slice (circle equation: h = sqrt(r²-dx²))
    int h_sq=r*r-dx*dx;
    if(h_sq<=0) continue;
    // Approximate sqrt with integer math
    int h=0;
    for(int t=r;t>=1;t>>=1) if((h+t)*(h+t)<=h_sq) h+=t;
    if(h<=0) continue;

    // Only draw upper half (dome)
    int slice_x=cx+dx;
    int slice_idx=dx+r;  // 0 to total_slices

    #ifdef PBL_COLOR
    if(slice_idx<filled_slices){
      // Colored: battery level color
      GColor bc;
      if(s_bat<=20) bc=GColorRed;
      else if(s_bat<=40) bc=GColorOrange;
      else if(s_bat<=60) bc=GColorChromeYellow;
      else bc=GColorGreen;
      graphics_context_set_fill_color(ctx,bc);
    } else {
      // Empty portion
      graphics_context_set_fill_color(ctx,GColorDarkGray);
    }
    #else
    graphics_context_set_fill_color(ctx,
      (slice_idx<filled_slices)?GColorWhite:GColorLightGray);
    #endif

    graphics_fill_rect(ctx,GRect(slice_x,canopy_y-h,1,h),0,GCornerNone);
  }

  // Canopy bottom edge line
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx,GColorWhite);
  #else
  graphics_context_set_fill_color(ctx,GColorBlack);
  #endif
  graphics_fill_rect(ctx,GRect(cx-r,canopy_y,r*2+1,2),0,GCornerNone);

  // Stripe lines on canopy (3 white lines radiating from pole top)
  graphics_context_set_stroke_color(ctx,GColorWhite);
  graphics_context_set_stroke_width(ctx,1);
  for(int i=1;i<=3;i++){
    int32_t a=DEG_TO_TRIGANGLE(180+i*45);
    int lx=cx+(sin_lookup(a)*r)/TRIG_MAX_RATIO;
    int ly=canopy_y-(cos_lookup(a)*r)/TRIG_MAX_RATIO;
    if(ly<canopy_y) graphics_draw_line(ctx,GPoint(cx,canopy_y),GPoint(lx,ly));
  }

  // HIGH detail: percentage text centered under umbrella
  if(s_det==DETAIL_HIGH){
    char bb[6]; snprintf(bb,sizeof(bb),"%d%%",s_bat);
    GFont f=fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_text_color(ctx,C_INFO);
    graphics_draw_text(ctx,bb,f,GRect(cx-18,pole_bot+1,36,16),
      GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
  }
}

// ============================================================================
// DRAW: BT SIGN
// ============================================================================
static void draw_bt(GContext *ctx, GRect b) {
  if(s_bt) return;
  // Fixed position right side, above tide text area
  int sx=b.size.w-65, py=b.size.h-48;
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

  // Banner and plane drawn together at same Y level
  // Banner is the main element, plane overlaps its right edge
  int by=py;  // Banner at same Y as plane reference
  if(bw>10) {
    // Red banner
    #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx,GColorRed);
    #else
    graphics_context_set_fill_color(ctx,C_SHAD);
    #endif
    graphics_fill_rect(ctx,GRect(bx,by,bw,14),0,GCornerNone);
    // Pennant tail
    graphics_fill_rect(ctx,GRect(bx-3,by+2,3,10),0,GCornerNone);

    // Banner text
    GFont f=fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_text_color(ctx,C_TEXT);
    const char *msg=s_refreshing?"Updating...":"Updated!";
    graphics_draw_text(ctx,msg,f,GRect(bx+2,by-1,bw-4,16),
      GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
  }

  // Plane body centered in the 14px banner
  int plane_y=by+4;
  graphics_context_set_fill_color(ctx,C_PLANE);
  graphics_fill_rect(ctx,GRect(px,plane_y,14,5),0,GCornerNone);
  // Wings
  graphics_fill_rect(ctx,GRect(px+4,plane_y-4,6,13),0,GCornerNone);
  // Tail
  graphics_fill_rect(ctx,GRect(px-3,plane_y-2,5,4),0,GCornerNone);

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

  // -- TEMP + WEATHER (all levels) --
  snprintf(s_tmp,sizeof(s_tmp),"%d°",s_d.temp);
  int temp_y=26;  // Near top of round visible area
  if(s_d.wx!=WX_CLEAR){
    // Icon draws centered at its y param — align with temp text visual center
    // GOTHIC_24_BOLD renders ~20px tall, visual center at temp_y+10
    txt(ctx,s_tmp,f24,GRect(b.size.w/2-48,temp_y,48,28),GTextAlignmentRight);
    draw_wx(ctx,b.size.w/2+16,temp_y+18,s_d.wx);  // Icon lower to match temp baseline
  } else {
    txt(ctx,s_tmp,f24,GRect(0,temp_y,b.size.w,28),GTextAlignmentCenter);
  }

  // -- TIME (right below plane banner) --
  int ty=PLANE_Y+16;
  txt(ctx,s_tbuf,f42,GRect(0,ty,b.size.w,50),GTextAlignmentCenter);

  // -- DATE --
  txt(ctx,s_dbuf,f18,GRect(0,ty+42,b.size.w,22),GTextAlignmentCenter);

  // -- SUNRISE/SUNSET at 9 and 3 o'clock --
  // MED: times only. HIGH: labels + times.
  if(s_det>=DETAIL_MED) {
    snprintf(s_sr,sizeof(s_sr),"%d:%02d",s_d.sr_h,s_d.sr_m);
    snprintf(s_ss,sizeof(s_ss),"%d:%02d",s_d.ss_h,s_d.ss_m);
    int mid_y=b.size.h/2;
    if(s_det>=DETAIL_HIGH) {
      // HIGH: labels above times
      txt(ctx,"Sunrise",f14,GRect(8,mid_y-18,60,16),GTextAlignmentLeft);
      txt(ctx,"Sunset",f14,GRect(b.size.w-68,mid_y-18,60,16),GTextAlignmentRight);
    }
    // Times at 9 and 3 o'clock
    int time_y=(s_det>=DETAIL_HIGH)?mid_y-4:mid_y-10;
    txt(ctx,s_sr,f14,GRect(8,time_y,50,18),GTextAlignmentLeft);
    txt(ctx,s_ss,f14,GRect(b.size.w-58,time_y,50,18),GTextAlignmentRight);
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

    // Fixed position at bottom of screen
    int iy=b.size.h-40;

    // HIGH: city name on same line, shifted up slightly
    if(s_det==DETAIL_HIGH && s_d.town[0]){
      graphics_context_set_text_color(ctx,C_INFO);
      graphics_draw_text(ctx,s_d.town,f14,GRect(0,iy-12,b.size.w,14),
        GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    }

    graphics_context_set_text_color(ctx,C_SHAD);
    graphics_draw_text(ctx,s_t1,f14,GRect(1,iy+1,b.size.w,18),
      GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    graphics_context_set_text_color(ctx,C_INFO);
    graphics_draw_text(ctx,s_t1,f14,GRect(0,iy,b.size.w,18),
      GTextOverflowModeTrailingEllipsis,GTextAlignmentCenter,NULL);
    if(s_t2[0]){
      graphics_context_set_text_color(ctx,C_INFO);
      graphics_draw_text(ctx,s_t2,f14,GRect(0,iy+13,b.size.w,18),
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
  draw_waves(ctx,b);
  draw_sand(ctx,b); draw_battery(ctx,b); draw_bt(ctx,b);
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
  // Don't mark dirty here — animation loop handles redraws.
  // If not animating, just mark dirty for next redraw.
  if(!s_anim && s_canvas) layer_mark_dirty(s_canvas);
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
    if(t){
      s_det=(int)t->value->int32;
      persist_write_int(P_DETAIL,s_det);
      if(s_canvas) layer_mark_dirty(s_canvas);
      APP_LOG(APP_LOG_LEVEL_INFO,"Detail set to %d",s_det);
    }
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
