/* SPDX-License-Identifier: Apache-2.0 */
/* GLES3 port of Adriwin06/black-hole's Schwarzschild Binet integrator. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <png.h>
#include <GLES3/gl32.h>
#include "ncz_gl.h"
#include "ncz_gpu_tier.h"
#include "ncz_options.h"
#include "blackhole_opts.h"
#include "blackhole_randomize.h"
#include "blackhole_director.h"
#include "ncz_harness_cfg.h"
#include "ncz_render.h"
#include "gles3_harness_hooks.h"
#include "ncz_hack_shim.h"
#include "ncz_feed_render.h"
#include "ncz_time_offset.h"
#include "bh_pacer_state.h"
/* gcc 14+ makes implicit declarations an error under -std=c11 even though
 * both headers above declare this; explicit forward decl avoids the
 * regression if the header order ever changes. */
#ifdef NCZ_GLES3_BUILD
extern void ncz_harness_die(int code);
#endif
typedef struct {
 GLuint program,vbo;
 GLint time,resolution,seed,radius,temperature,density,rotation,inclination,orbit_rate,jet,star_density,camera_mode,palette,approach,periapsis,nebula,nebula_axis;
 GLint pal_a,pal_b,pal_mix,spin,isco,exposure,beaming,bloom,fringe,lensing,nebula_amt,hot_sector,fade,disk_out,jet_len,torus,torus_r,comp,flyby,fly_t,fly_var,dmin,dmax,incl;
 GLint palette_phase,palette_rate,palette_contrast,nebula_scheme,tonemap,structure;
 // Per-launch path-shape parameters (replaces the hardcoded curve
 // coefficients that were previously literals in disk_color's camera block).
 GLint path_d_base,path_d_swing,path_d_harm_amp,path_d_harm_freq;
 GLint path_o_rate,path_o_harm_amp,path_o_harm_freq,path_o_count,path_sign;
 GLint path_e_swing,path_e_freq,path_phase_jitter;
 // Disk-axis tilt: per-launch spin axis + slow precession. Two vec4
 // uniforms let the shader rotate into disk-local space once per frame
 // and reconstruct the disk normal including slow precession.
 GLint disk_axis,disk_precess;
 // Camera trajectory family selector: 0 = zoom-whirl (bound), 1 =
 // hyperbolic flyby (unbound). Drawn per-launch from the seeded RNG.
 GLint camera_family;
 // Real trajectory family parameters. orbit_q is the zoom-whirl
 // ratio (angular/rev radial); orbit_e is eccentricity (zoom-whirl
 // draws [0.15,0.7], flyby draws [1.05,3.0]); orbit_omega is the
 // argument of periapse in radians.
 GLint orbit_q,orbit_e,orbit_omega;
 // GPU-quality uniform: bounds the Schwarzschild integration loop. Adaptive per tier
 // so the floor tier can hold 30fps on Intel iGPU.
 GLint max_steps;
 /* scene uniforms (blackhole_scenes.h) */
 GLint sc_holes,sc_scn,sc_gw,sc_gw2,sc_fx,sc_tde,sc_tde2,sc_tde3,sc_gal,sc_gal2,sc_zoom;
 BhSceneFrame sf;
 BhDirector dir;int dir_ready;float frame[2];
 GLint jitter,dither,camo,drift,kerr_on,kerr_tab,jet_duty,photon_ring,bright_stars,enh;GLuint kerr_tx;
 uint32_t launch_seed;
 float last_fade;
 double started;
 /* frame pacing + adaptive quality (see bh_pace) */
 double prev_t,last_change,refr;
 float level,levelmax;
 unsigned long nfr;
 /* Budget-specific failure history (see bh_pacer_state.h). The miss/near
  * rings and the cached fail_level are cleared on a budget transition so
  * the new mode does not inherit the previous mode's misses. The ray-step
  * level itself lives outside this seam (it's the controller's "keep" set). */
 BhPacerState pace;
 float dtring[300];int dtn;float wdt[120];unsigned long hit[16];int nhit;
 // 0:seed 1:radius 2:temp 3:density 4:rotation 5:inclination 6:orbit_rate
 // 7:jet 8:star_density 9:camera_mode 10:palette 11:approach 12:periapsis
 // 13..16:nebula vec4 (hue,scale,coverage,yaw) 17..18:nebula_axis vec2 (tilt,offset)
 // 19:palette_phase 20:palette_rate 21:palette_contrast 22:nebula_scheme
 // 23:path_d_base 24:path_d_swing 25:path_d_harm_amp 26:path_d_harm_freq
 // 27:path_o_rate 28:path_o_harm_amp 29:path_o_harm_freq 30:path_o_count 31:path_sign
 // 32:path_e_swing 33:path_e_freq 34:path_phase_jitter
 // 35:disk_axis.xyz + wobble (xyz=spin axis unit vec, w=half-angle tilt)
 // 36..42:disk_precess.xyz + rate (xyz=precession axis, w=rate rad/s)
 // 43:camera_family (0 zoom-whirl, 1 hyperbolic flyby)
 // 44:orbit_q (zoom-whirl ratio; ~1 for flyby family)
 // 45:orbit_e (eccentricity; 0.15..0.7 zoom-whirl, 1.05..3.0 flyby)
 // 46:orbit_omega (periapse direction, radians)
 float v[47];
} State;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static float rnd(uint32_t*s,float a,float b){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return a+(b-a)*(float)(*s&0xffffffu)/16777215.f;}

/* ---- options -----------------------------------------------------------
 * One table (blackhole_opts.h), three channels: environment
 * NCZ_BLACKHOLE_<OPTION>, --option=value and $XDG_CONFIG_HOME/ncz-screensavers/
 * blackhole.conf ([blackhole]); precedence CLI > env > file > defaults. */
typedef struct {
 int pal_seq[16],npal;
 int flyby;                    /* 0 auto, 1..7 types, 8 random */
 double interval,ptrans,speed,dmin,dmax,duration;
 float incl,spin,exposure,beaming,bloom,fringe,nebula,hot,lensing,disk_out,jet_len,torus,comp;
 int adaptive,enh;
 uint32_t seed;
 BhSceneCfg sc;
 float tonemap,structure,jet_duty,photon_ring,bright_stars;int drift;
} BhOpts;
/* The harness parses the merged (generic + Black Hole) option set before any
 * Wayland or EGL work and owns it; this hack only reads the resolved values. */
const ncz_opt_def *ncz_hack_options(size_t *n,const char **prefix,const char **group){
 *n=BH_NOPTS;*prefix="NCZ_BLACKHOLE_";*group="blackhole";return BH_OPTS;
}
static const ncz_opts *g_op;static int g_o_ready;static BhOpts g_b;
#define g_o (*g_op)

/* ---- randomize ---------------------------------------------------------
 * Runs before the option set is resolved (the harness calls this weak hook),
 * so the rolled preset and look values flow through the normal precedence
 * chain: an explicit --option / NCZ_BLACKHOLE_* / command line still wins.
 * The engine has no scene/preset transition, so the scene type is re-rolled
 * only at process start (each screensaver start); the look is re-rolled there
 * too. The palette cycle and the per-flyby cycle stay the user's. */
/* Option name, environment variable and roll bit of every value the roll may
 * write. A name the user pinned (the launcher's NCZ_HACK_PINNED list, an
 * explicit --option= on our command line, or - when no launcher is involved -
 * an NCZ_BLACKHOLE_* already in the environment) is left untouched. */
typedef struct{const char *name,*env;unsigned bit;}BhRollField;
static const BhRollField BH_ROLL_FIELDS[]={
 {"exposure","NCZ_BLACKHOLE_EXPOSURE",BH_RF_EXPOSURE},
 {"disk-brightness","NCZ_BLACKHOLE_DISK_BRIGHTNESS",BH_RF_DISK_BRIGHTNESS},
 {"bloom","NCZ_BLACKHOLE_BLOOM",BH_RF_BLOOM},
 {"fringe","NCZ_BLACKHOLE_FRINGE",BH_RF_FRINGE},
 {"beaming","NCZ_BLACKHOLE_BEAMING",BH_RF_BEAMING},
 {"star-density","NCZ_BLACKHOLE_STAR_DENSITY",BH_RF_STAR_DENSITY},
 {"nebula","NCZ_BLACKHOLE_NEBULA",BH_RF_NEBULA},
 {"jet","NCZ_BLACKHOLE_JET",BH_RF_JET},
 {"hot-sector","NCZ_BLACKHOLE_HOT_SECTOR",BH_RF_HOT_SECTOR},
 {"temperature","NCZ_BLACKHOLE_TEMPERATURE",BH_RF_TEMPERATURE},
 {"rotation-speed","NCZ_BLACKHOLE_ROTATION_SPEED",BH_RF_ROTATION_SPEED},
 {"precession","NCZ_BLACKHOLE_PRECESSION",BH_RF_PRECESSION},
 {"disk-outer","NCZ_BLACKHOLE_DISK_OUTER",BH_RF_DISK_OUTER},
 {"jet-length","NCZ_BLACKHOLE_JET_LENGTH",BH_RF_JET_LENGTH},
 {"spin","NCZ_BLACKHOLE_SPIN",BH_RF_SPIN},
 {"ripples","NCZ_BLACKHOLE_RIPPLES",BH_RF_RIPPLES},
 {"speed","NCZ_BLACKHOLE_SPEED",BH_RF_SPEED},
 {"palette-transition","NCZ_BLACKHOLE_PALETTE_TRANSITION",BH_RF_PALETTE_TRANSITION},
 {"palette","NCZ_BLACKHOLE_PALETTE",BH_RF_PALETTE},
 {"preset","NCZ_BLACKHOLE_PRESET",BH_RF_PRESET},
};
#define BH_NROLL (sizeof BH_ROLL_FIELDS/sizeof BH_ROLL_FIELDS[0])
static void csv_add(char *b,size_t sz,const char *n){size_t l=strlen(b);if(l+1>=sz)return;if(l)b[l++]=',';snprintf(b+l,sz-l,"%s",n);}
static void set_num(const char *name, double v){char b[32];snprintf(b,sizeof b,"%.6g",v);setenv(name,b,1);}
void ncz_hack_preinit(int argc, char **argv){
 const char *env_mode=getenv("NCZ_BLACKHOLE_RANDOMIZE"),*arg_mode=NULL,*arg_seed=NULL;
 const char *env_seed=getenv("NCZ_BLACKHOLE_SEED");
 if(!env_seed||!*env_seed)env_seed=getenv("NCZ_BLACKHOLE_FIXED_SEED");
 for(int i=1;i<argc;i++){if(!argv[i])continue;
  if(!strncmp(argv[i],"--randomize=",12))arg_mode=argv[i]+12;
  else if(!strncmp(argv[i],"--seed=",7))arg_seed=argv[i]+7;
  else if(!strcmp(argv[i],"--dump-schema")||!strcmp(argv[i],"--dump-schema-json")||
          !strcmp(argv[i],"--dump-schema-common")||!strcmp(argv[i],"--help")||
          !strcmp(argv[i],"--list-options")||!strcmp(argv[i],"--list-presets")||
          !strcmp(argv[i],"--version"))return;   /* schema/help stay deterministic */
 }
 const char *mode=arg_mode?arg_mode:(env_mode?env_mode:"");
 if(!*mode||!strcmp(mode,"off"))return;
 int m=!strcmp(mode,"everything")?BH_RAND_EVERYTHING:!strcmp(mode,"look")?BH_RAND_LOOK:-1;
 if(m<0)return;  /* invalid choice: the option parser reports it */
 uint32_t rollout_seed=0;
 const char *seedstr=arg_seed?arg_seed:env_seed;
 if(seedstr&&*seedstr)rollout_seed=(uint32_t)strtoul(seedstr,NULL,0);
 if(!rollout_seed){int f=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
  if(f>=0){ssize_t n=read(f,&rollout_seed,4);close(f);if(n!=4)rollout_seed=0;}}
 if(!rollout_seed){struct timespec t;clock_gettime(CLOCK_REALTIME,&t);rollout_seed=(uint32_t)(t.tv_nsec^t.tv_sec^getpid());}
 if(!rollout_seed)rollout_seed=1;
 const char *cls=ncz_gpu_class(NULL,getenv("NCZ_GPU_CLASS"));
 const char *pool=getenv("NCZ_GPU_POOL");   /* launcher pool-gpu-class: heavy opt-in */
 int heavy=pool&&(!strcmp(pool,"strong")||!strcmp(pool,"all"));
 /* Pinned set, in precedence order: the launcher's list of stored options,
  * then an explicit --option= on our own command line, then (only when no
  * launcher is in charge) an NCZ_BLACKHOLE_* already in the environment. */
 char pinned[512]="";
 const char *launcher_pins=getenv("NCZ_HACK_PINNED");
 int strict=launcher_pins!=NULL;
 if(strict&&*launcher_pins)snprintf(pinned,sizeof pinned,"%s",launcher_pins);
 for(size_t i=0;i<BH_NROLL;i++){
  const BhRollField *f=&BH_ROLL_FIELDS[i];
  if(bh_randomize_pinned(pinned,f->name))continue;
  char flag[80];snprintf(flag,sizeof flag,"--%s=",f->name);
  int cli=0;for(int a=1;a<argc&&!cli;a++)if(argv[a]&&!strncmp(argv[a],flag,strlen(flag)))cli=1;
  if(cli){csv_add(pinned,sizeof pinned,f->name);continue;}
  if(!strict){const char *v=getenv(f->env);
   if((!v||!*v)&&!strcmp(f->name,"palette"))v=getenv("NCZ_BLACKHOLE_COLORS");
   if(v&&*v)csv_add(pinned,sizeof pinned,f->name);}
 }
 BhRandomPreset cat[64];size_t nc=bh_randomize_catalogue("blackhole",cat,64);
 BhRandomRoll roll;
 if(bh_randomize_roll(rollout_seed,m,cls,heavy,cat,nc,pinned,&roll)!=0){
  fprintf(stderr,"[diag] blackhole randomize mode=%s class=%s: no allowed preset; roll skipped\n",mode,cls);return;
 }
 {char sb[24];snprintf(sb,sizeof sb,"%u",roll.seed);setenv("NCZ_BLACKHOLE_SEED",sb,1);}
 if((roll.rolled&BH_RF_PRESET)&&roll.preset[0])setenv("NCZ_BLACKHOLE_PRESET",roll.preset,1);
 if((roll.rolled&BH_RF_PALETTE)&&roll.palette[0])setenv("NCZ_BLACKHOLE_PALETTE",roll.palette,1);
 #define APPLY(bit,env,field) do{if(roll.rolled&(bit))set_num(env,roll.field);}while(0)
 APPLY(BH_RF_EXPOSURE,"NCZ_BLACKHOLE_EXPOSURE",exposure);
 APPLY(BH_RF_DISK_BRIGHTNESS,"NCZ_BLACKHOLE_DISK_BRIGHTNESS",disk_brightness);
 APPLY(BH_RF_BLOOM,"NCZ_BLACKHOLE_BLOOM",bloom);
 APPLY(BH_RF_FRINGE,"NCZ_BLACKHOLE_FRINGE",fringe);
 APPLY(BH_RF_BEAMING,"NCZ_BLACKHOLE_BEAMING",beaming);
 APPLY(BH_RF_STAR_DENSITY,"NCZ_BLACKHOLE_STAR_DENSITY",star_density);
 APPLY(BH_RF_NEBULA,"NCZ_BLACKHOLE_NEBULA",nebula);
 APPLY(BH_RF_JET,"NCZ_BLACKHOLE_JET",jet);
 APPLY(BH_RF_HOT_SECTOR,"NCZ_BLACKHOLE_HOT_SECTOR",hot_sector);
 APPLY(BH_RF_TEMPERATURE,"NCZ_BLACKHOLE_TEMPERATURE",temperature);
 APPLY(BH_RF_ROTATION_SPEED,"NCZ_BLACKHOLE_ROTATION_SPEED",rotation_speed);
 APPLY(BH_RF_PRECESSION,"NCZ_BLACKHOLE_PRECESSION",precession);
 APPLY(BH_RF_DISK_OUTER,"NCZ_BLACKHOLE_DISK_OUTER",disk_outer);
 APPLY(BH_RF_JET_LENGTH,"NCZ_BLACKHOLE_JET_LENGTH",jet_length);
 APPLY(BH_RF_SPIN,"NCZ_BLACKHOLE_SPIN",spin);
 APPLY(BH_RF_RIPPLES,"NCZ_BLACKHOLE_RIPPLES",ripples);
 APPLY(BH_RF_SPEED,"NCZ_BLACKHOLE_SPEED",speed);
 APPLY(BH_RF_PALETTE_TRANSITION,"NCZ_BLACKHOLE_PALETTE_TRANSITION",palette_transition);
 #undef APPLY
 char rolled[512]="";
 for(size_t i=0;i<BH_NROLL;i++)if(roll.rolled&BH_ROLL_FIELDS[i].bit)csv_add(rolled,sizeof rolled,BH_ROLL_FIELDS[i].name);
 const char *preset_shown=(roll.rolled&BH_RF_PRESET)?roll.preset:(getenv("NCZ_BLACKHOLE_PRESET")?getenv("NCZ_BLACKHOLE_PRESET"):"-");
 const char *palette_shown=(roll.rolled&BH_RF_PALETTE)?roll.palette:(getenv("NCZ_BLACKHOLE_PALETTE")?getenv("NCZ_BLACKHOLE_PALETTE"):"-");
 fprintf(stderr,"[diag] blackhole randomize seed=%u mode=%s class=%s pool=%s preset=%s palette=%s "
  "pinned=%s rolled=%s exposure=%.3f disk_brightness=%.3f bloom=%.3f fringe=%.3f beaming=%.3f star_density=%.3f nebula=%.3f "
  "jet=%.3f hot_sector=%.3f temperature=%.3f rotation_speed=%.3f precession=%.3f disk_outer=%.2f "
  "jet_length=%.2f spin=%.3f ripples=%.3f speed=%.3f palette_transition=%.2f\n",
  roll.seed,roll.mode_name,cls,heavy?"heavy":"class",preset_shown,palette_shown,
  pinned[0]?pinned:"-",rolled[0]?rolled:"-",
  roll.exposure,roll.disk_brightness,roll.bloom,roll.fringe,roll.beaming,roll.star_density,roll.nebula,
  roll.jet,roll.hot_sector,roll.temperature,roll.rotation_speed,roll.precession,roll.disk_outer,
  roll.jet_length,roll.spin,roll.ripples,roll.speed,roll.palette_transition);
 fflush(stderr);
}

static int pal_id(const char*n){static const char*t[]={"stylized","kipthorne","faithful","singularity","slingshot","whitehole","eht","psychedelic","synthwave","cygnus"};for(int i=0;i<10;i++)if(!strcmp(n,t[i]))return i;return 0;}
/* 0 = legacy per-launch path, 1..7 flyby types, 8 random, 9 director (auto resolves to it) */
enum{BH_FLY_DIRECTOR=9,BH_FLY_LEGACY=10};
static int fly_id(const char*n){static const char*t[]={"auto","orbit","slow-orbit","equatorial","polar","plunge","slingshot","drift","random","director","legacy"};for(int i=0;i<11;i++)if(!strcmp(n,t[i]))return i;return 0;}
static void bh_resolve(void){
 if(g_o_ready)return;
 g_op=ncz_harness_opts();
 if(!g_op)return;
 g_o_ready=1;
 BhOpts*b=&g_b;
 b->npal=0;b->pal_seq[b->npal++]=pal_id(ncz_opts_get(&g_o,"palette"));
 {char l[256];snprintf(l,sizeof l,"%s",ncz_opts_get(&g_o,"cycle-palettes"));
  for(char*t=strtok(l,",");t&&b->npal<16;t=strtok(NULL,","))b->pal_seq[b->npal++]=pal_id(t);}
 b->flyby=fly_id(ncz_opts_get(&g_o,"flyby"));
 b->interval=ncz_opts_get_float(&g_o,"cycle-interval");b->ptrans=ncz_opts_get_float(&g_o,"palette-transition");
 b->speed=ncz_opts_get_float(&g_o,"speed");b->dmin=ncz_opts_get_float(&g_o,"distance-min");b->dmax=ncz_opts_get_float(&g_o,"distance-max");
 b->duration=ncz_opts_get_float(&g_o,"duration");b->incl=(float)ncz_opts_get_float(&g_o,"inclination");
 b->spin=(float)ncz_opts_get_float(&g_o,"spin");b->exposure=(float)ncz_opts_get_float(&g_o,"exposure");
 b->beaming=(float)ncz_opts_get_float(&g_o,"beaming");b->bloom=(float)ncz_opts_get_float(&g_o,"bloom");
 b->fringe=(float)ncz_opts_get_float(&g_o,"fringe");b->nebula=(float)ncz_opts_get_float(&g_o,"nebula");
 b->hot=(float)ncz_opts_get_float(&g_o,"hot-sector");
 b->disk_out=(float)ncz_opts_get_float(&g_o,"disk-outer");b->jet_len=(float)ncz_opts_get_float(&g_o,"jet-length");
 b->torus=(float)ncz_opts_get_float(&g_o,"torus");b->comp=(float)ncz_opts_get_float(&g_o,"companion");b->lensing=ncz_opts_get_bool(&g_o,"lensing")?1.f:0.f;
 b->seed=(uint32_t)strtoul(ncz_opts_get(&g_o,"seed"),NULL,0);
 /* Weak GPUs (Intel UHD class): lighter defaults for the optional extras unless the user set them. */
 if(!strcmp(ncz_gpu_class_current(),"weak")){
  if(!ncz_opts_is_set(&g_o,"bloom"))b->bloom=0.f;
  if(!ncz_opts_is_set(&g_o,"nebula"))b->nebula*=0.5f;
  if(!ncz_opts_is_set(&g_o,"torus"))b->torus*=0.6f;
 }
 b->adaptive=ncz_opts_get_bool(&g_o,"adaptive")&&!strcmp(ncz_opts_get(&g_o,"quality"),"auto");
 /* Scenes (blackhole_scenes.h). Each scene picks its own camera path and
  * distances unless the user chose them; auto quality is the thin-lens light
  * mode on weak GPUs and the integrated rich mode elsewhere. */
 {
  BhSceneCfg*c=&b->sc;
  c->scene=bh_scene_id(ncz_opts_get(&g_o,"scene"));
  const char*q=ncz_opts_get(&g_o,"scene-quality");
  c->light=!strcmp(q,"light")||(!strcmp(q,"auto")&&!strcmp(ncz_gpu_class_current(),"weak"));
  double cy=ncz_opts_get_float(&g_o,"scene-cycle");
  c->cycle=BH_SC_CYCLE[c->scene]>0?(cy>0?(cy<30?30:cy):BH_SC_CYCLE[c->scene]):0;
  c->q=(float)ncz_opts_get_float(&g_o,"mass-ratio");
  c->sep=(float)ncz_opts_get_float(&g_o,"separation");
  c->ripples=(float)ncz_opts_get_float(&g_o,"ripples");
  c->seed=b->seed;
  /* auto -> director (the galaxy zoom keeps its own camera); legacy -> the
   * earlier per-launch path (scenes need a flyby path: their default one). */
  b->enh=!strcmp(ncz_opts_get(&g_o,"style"),"enhanced");
  if(b->flyby==0&&(c->scene||b->enh))b->flyby=c->scene==BH_SC_GALAXY?BH_SC_FLY[c->scene]:BH_FLY_DIRECTOR;
  else if(b->flyby==BH_FLY_LEGACY)b->flyby=c->scene?BH_SC_FLY[c->scene]:0;
  if(b->incl>=0&&b->incl<3.f)b->incl=3.f;
  /* Style: classic keeps the original look; enhanced turns the reviewed
   * improvements on unless the user set them explicitly. */
#define ENH(opt,val) (b->enh&&!ncz_opts_is_set(&g_o,opt)?(val):
  b->tonemap=ENH("tonemap",0.f)(strcmp(ncz_opts_get(&g_o,"tonemap"),"classic")?0.f:1.f));
  b->structure=ENH("disk-structure",1.f)(float)ncz_opts_get_float(&g_o,"disk-structure"));
  b->jet_duty=ENH("jet-duty",.75f)(float)ncz_opts_get_float(&g_o,"jet-duty"));
  b->photon_ring=ENH("photon-ring",1.f)(float)ncz_opts_get_float(&g_o,"photon-ring"));
  b->bright_stars=ENH("bright-stars",1.f)(float)ncz_opts_get_float(&g_o,"bright-stars"));
  b->drift=ENH("drift",1)ncz_opts_get_bool(&g_o,"drift"));
#undef ENH   /* never exactly edge-on (aliased 1-pixel disk) */
 }
}
static uint32_t seed(void){
 if(g_b.seed)return g_b.seed;
 uint32_t s=0;int f=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
 if(f>=0){ssize_t n=read(f,&s,4);close(f);if(n==4)return s;}
 struct timespec t;clock_gettime(CLOCK_REALTIME,&t);
 return t.tv_nsec^t.tv_sec^getpid();
}
static uint32_t hash32(uint32_t a,uint32_t b){uint32_t h=a*2654435761u^(b+0x9E3779B9u);h^=h>>16;h*=0x85ebca6bu;h^=h>>13;h*=0xc2b2ae35u;h^=h>>16;return h;}
static float sstep(float a,float b,float x){float t=(x-a)/(b-a);t=t<0?0:t>1?1:t;return t*t*(3-2*t);}
/* Per-type natural period (s) and default distance range. Index = type 1..7. */
static const double FLY_T[8]={0,60,180,45,50,40,45,240};
static const double FLY_DMIN[8]={0,11,14,6.5,7.5,3.8,4.6,9};
static const double FLY_DMAX[8]={0,16,20,30,30,26,34,20};
/* Upload every uniform that changes per frame because of the options. */
static void bh_frame_uniforms(State*s,double t,double treal){
 const BhOpts*b=&g_b;
 /* palette sequence with smooth cross-fades */
 int pa=b->pal_seq[0],pb=pa;float pm=0.f;
 if(b->npal>1){
  long k=(long)(treal/b->interval);double tb=treal-k*b->interval;
  int n=b->npal;
  pb=b->pal_seq[k%n];pa=pb;
  double tr=b->ptrans>b->interval*0.5?b->interval*0.5:b->ptrans;
  if(k>0&&tr>0&&tb<tr){pa=b->pal_seq[(k-1)%n];pm=sstep(0,(float)tr,(float)tb);}
 }
 glUniform1f(s->pal_a,(float)pa);glUniform1f(s->pal_b,(float)pb);glUniform1f(s->pal_mix,pm);
 glUniform1f(s->spin,b->spin);glUniform1f(s->isco,3.f-1.4f*b->spin);
 glUniform1f(s->exposure,b->exposure);glUniform1f(s->beaming,b->beaming);
 glUniform1f(s->bloom,b->bloom);glUniform1f(s->fringe,b->fringe);
 glUniform1f(s->disk_out,b->disk_out);glUniform1f(s->jet_len,b->jet_len);glUniform1f(s->torus,b->torus);glUniform1f(s->torus_r,b->disk_out*2.6f);glUniform1f(s->comp,b->comp);
 glUniform1f(s->tonemap,b->tonemap);glUniform1f(s->structure,b->structure);
 glUniform1f(s->photon_ring,b->photon_ring);glUniform1f(s->bright_stars,b->bright_stars);glUniform1f(s->enh,(float)b->enh);
 glUniform1f(s->lensing,b->lensing);glUniform1f(s->nebula_amt,b->nebula);glUniform1f(s->hot_sector,b->hot);
 float fade=1.f,flyid=0.f,flyt=0.f,flyv=0.f;double dmin=6,dmax=20;
 if(b->flyby&&b->flyby<BH_FLY_DIRECTOR){
  int ft=b->flyby,type=ft;
  double T=b->duration>0?b->duration:(ft==8?50.:FLY_T[ft]);
  long k=(long)floor(t/T);
  uint32_t sd=(uint32_t)s->v[0];
  if(ft==8){type=1+(int)(hash32(sd,(uint32_t)k)%7u);int prev=k>0?1+(int)(hash32(sd,(uint32_t)(k-1))%7u):-1;if(type==prev)type=type%7+1;T=b->duration>0?b->duration:FLY_T[type]*0.6;k=(long)floor(t/T);}
  int wrap=(ft==8)||b->duration>0||(type>=3&&type<=6);
  flyid=(float)type;
  flyt=(float)(t/T);
  flyv=(hash32(sd,(uint32_t)(wrap?k:0))>>8)/16777216.f;
  if(wrap){double sec=t-k*T;fade=fminf(sstep(0,1.2f,(float)sec),sstep(0,1.2f,(float)(T-sec)));}
  int sc=b->sc.scene;
  dmin=b->dmin>0?b->dmin:(sc?BH_SC_DMIN[sc]:FLY_DMIN[type]);dmax=b->dmax>0?b->dmax:(sc?BH_SC_DMAX[sc]:FLY_DMAX[type]);
  if(dmin<3.2)dmin=3.2;if(dmax<dmin+1.)dmax=dmin+1.;
 }
 /* Director camera (auto): distances from the scene, or from the disk size
  * for the single hole (occupancy floor: the far end keeps the disk at
  * least about a sixth of the frame height), or the user's range. */
 if(b->flyby==BH_FLY_DIRECTOR){
  if(!s->dir_ready){
   int sc=b->sc.scene;
   double nr=sc?BH_SC_DMIN[sc]:fmax(b->disk_out*0.95,6.),fr=sc?BH_SC_DMAX[sc]*1.2:fmin(b->disk_out*1.9,48.);
   if(b->dmin>0)nr=fmax(b->dmin,3.4);
   if(b->dmax>0)fr=b->dmax;
   if(fr<nr*1.3)fr=nr*1.3;
   bhd_init(&s->dir,s->launch_seed^0x5bd1e995u,nr,fr);s->dir_ready=1;
  }
  float cp[3];bhd_camera(&s->dir,t,cp,s->frame);
  glUniform4f(s->camo,cp[0],cp[1],cp[2],1.f);
 }else glUniform4f(s->camo,0,0,0,0);
 /* Explicit framing (--frame-x/--frame-y) adds to the director's framing. */
 float fox=(float)ncz_opts_get_float(&g_o,"frame-x"),foy=(float)ncz_opts_get_float(&g_o,"frame-y");
 /* Unattended runs: slow composition drift and occasional dim passages. */
 if(b->drift){
  double tw=treal;
  glUniform2f(s->drift,fox+s->frame[0]+(float)(0.08*sin(tw*6.2831853/300.)),foy+s->frame[1]+(float)(0.06*sin(tw*6.2831853/420.+1.)));
  double ph=fmod(tw,420.)-300.;                          /* a 45 s dip every 7 minutes */
  float dim=ph>0&&ph<45?(float)(1.0-0.22*sin(ph/45.*M_PI)):1.f;
  glUniform1f(s->exposure,b->exposure*dim);
 }else glUniform2f(s->drift,fox+s->frame[0],foy+s->frame[1]);
 /* Jets come and go: on for the duty fraction of a ~190 s cycle, 8 s fades. */
 {
  double duty=b->jet_duty,env=1;
  if(duty<0.999){
   double P=190.,x=fmod(treal+P*(double)(s->launch_seed%997u)/997.,P),on=duty*P;
   env=duty<=0?0:fmin(sstep(0,8,(float)x),sstep(0,8,(float)(on-x)));
   if(x>on)env=0;
  }
  glUniform1f(s->jet_duty,(float)env);
 }
 glUniform1f(s->flyby,flyid);glUniform1f(s->fly_t,flyt);glUniform1f(s->fly_var,flyv);
 glUniform1f(s->dmin,(float)dmin);glUniform1f(s->dmax,(float)dmax);
 glUniform1f(s->incl,b->incl>=0?b->incl*0.01745329f:BH_SC_INCL[b->sc.scene]);
 if(b->sc.scene){
  BhSceneFrame*f=&s->sf;
  bh_scene_frame(&b->sc,t,f);
  glUniform4fv(s->sc_holes,3,f->holes);glUniform4fv(s->sc_scn,1,f->scn);
  glUniform4fv(s->sc_gw,1,f->gw);glUniform4fv(s->sc_gw2,1,f->gw2);glUniform4fv(s->sc_fx,1,f->fx);
  glUniform4fv(s->sc_tde,1,f->tde);glUniform4fv(s->sc_tde2,1,f->tde2);glUniform4fv(s->sc_tde3,1,f->tde3);
  glUniform4fv(s->sc_gal,1,f->gal);glUniform4fv(s->sc_gal2,1,f->gal2);glUniform4fv(s->sc_zoom,1,f->zoom);
  glUniform1f(s->nebula_amt,b->nebula*f->zoom[2]);
  fade=fminf(fade,f->fade);
 }else{s->sf.zoom[1]=1.f;}
 glUniform1f(s->fade,fade);
 s->last_fade=fade;
}
static char*load(void){const char*p[]={"vendor/blackhole/blackhole.frag","../vendor/blackhole/blackhole.frag","../../vendor/blackhole/blackhole.frag","/usr/share/ncz-screensavers/shaders/blackhole.frag"};FILE*f=0;const char*u=0;for(unsigned i=0;i<4;i++)if((f=fopen(p[i],"rb"))){u=p[i];break;}if(!f){fprintf(stderr,"blackhole: cannot locate shader\n");return 0;}fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char*b=malloc(n+1);if(!b||fread(b,1,n,f)!=(size_t)n){free(b);fclose(f);return 0;}fclose(f);b[n]=0;fprintf(stderr,"[diag] blackhole shader=%s\n",u);
 /* Specialize for the scene: insert the defines after the #version line so the
  * default single-hole shader compiles exactly as before. */
 char def[96];snprintf(def,sizeof def,"#define BH_SCENE %d\n#define BH_LIGHT %d\n",g_b.sc.scene,g_b.sc.light);
 char*nl=strchr(b,'\n');
 if(!nl||strncmp(b,"#version",8)){fprintf(stderr,"blackhole: shader must start with a #version line\n");free(b);return 0;}
 size_t hl=(size_t)(nl-b)+1,dl=strlen(def);
 char*o=malloc((size_t)n+dl+1);if(!o){free(b);return 0;}
 memcpy(o,b,hl);memcpy(o+hl,def,dl);memcpy(o+hl+dl,b+hl,(size_t)n-hl+1);free(b);return o;}
static GLuint comp(GLenum t,const char*x){GLuint s=glCreateShader(t);glShaderSource(s,1,&x,0);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char l[8192];glGetShaderInfoLog(s,sizeof l,0,l);fprintf(stderr,"blackhole compile: %s\n",l);glDeleteShader(s);return 0;}return s;}
static void init_blackhole(ModeInfo*m){
 static const char*vs="#version 300 es\nlayout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}\n";
 State*s=calloc(1,sizeof*s);
 if(!s){ncz_harness_die(1);return;}
 m->data=s;
 bh_resolve();   /* the scene selects the shader variant */
 char*x=load();
 if(!x){ncz_harness_die(1);return;}
 GLuint v=comp(GL_VERTEX_SHADER,vs),f=comp(GL_FRAGMENT_SHADER,x);
 free(x);
 if(!v||!f){ncz_harness_die(1);return;}
 s->program=glCreateProgram();
 glAttachShader(s->program,v);
 glAttachShader(s->program,f);
 glLinkProgram(s->program);
 glDeleteShader(v);
 glDeleteShader(f);
 GLint ok=0;
 glGetProgramiv(s->program,GL_LINK_STATUS,&ok);
 if(!ok){char l[8192];glGetProgramInfoLog(s->program,sizeof l,0,l);fprintf(stderr,"blackhole link: %s\n",l);ncz_harness_die(1);return;}
 const float q[]={-1,-1,1,-1,-1,1,-1,1,1,-1,1,1};
 glGenBuffers(1,&s->vbo);
 glBindBuffer(GL_ARRAY_BUFFER,s->vbo);
 glBufferData(GL_ARRAY_BUFFER,sizeof q,q,GL_STATIC_DRAW);
 glBindBuffer(GL_ARRAY_BUFFER,0);
#define L(n) s->n=glGetUniformLocation(s->program,"u_"#n)
 L(time);L(resolution);L(seed);L(radius);L(temperature);L(density);L(rotation);L(inclination);L(orbit_rate);L(jet);L(star_density);L(camera_mode);L(palette);L(approach);L(periapsis);L(nebula);L(nebula_axis);L(pal_a);L(pal_b);L(pal_mix);L(spin);L(isco);L(exposure);L(beaming);L(bloom);L(fringe);L(lensing);L(nebula_amt);L(hot_sector);L(disk_out);L(jet_len);L(torus);L(torus_r);L(comp);L(fade);L(flyby);L(fly_t);L(fly_var);L(dmin);L(dmax);L(incl);
 L(palette_phase);L(palette_rate);L(palette_contrast);L(nebula_scheme);L(tonemap);L(structure);
 L(path_d_base);L(path_d_swing);L(path_d_harm_amp);L(path_d_harm_freq);
 L(path_o_rate);L(path_o_harm_amp);L(path_o_harm_freq);L(path_o_count);L(path_sign);
 L(path_e_swing);L(path_e_freq);L(path_phase_jitter);
 L(disk_axis);L(disk_precess);L(camera_family);
 L(orbit_q);L(orbit_e);L(orbit_omega);
 L(max_steps);
#undef L
#define LS(n,u) s->n=glGetUniformLocation(s->program,u)
 LS(sc_holes,"u_holes");LS(sc_scn,"u_scn");LS(sc_gw,"u_gw");LS(sc_gw2,"u_gw2");LS(sc_fx,"u_fx");
 LS(sc_tde,"u_tde");LS(sc_tde2,"u_tde2");LS(sc_tde3,"u_tde3");LS(sc_gal,"u_gal");LS(sc_gal2,"u_gal2");LS(sc_zoom,"u_zoom");
 LS(jitter,"u_jitter");LS(dither,"u_dither");LS(camo,"u_camo");LS(drift,"u_drift");LS(kerr_on,"u_kerr");LS(kerr_tab,"u_kerr_tab");LS(jet_duty,"u_jet_duty");LS(photon_ring,"u_photon_ring");LS(bright_stars,"u_bright_stars");LS(enh,"u_enh");
#undef LS
 bh_resolve();
 /* Analytic Kerr shadow outline (single hole with spin). */
 if(g_b.spin>0.02f&&g_b.sc.scene==BH_SC_SINGLE&&s->kerr_tab>=0){
  float tab[320];bh_kerr_table(g_b.spin,tab);
  glGenTextures(1,&s->kerr_tx);glBindTexture(GL_TEXTURE_2D,s->kerr_tx);
  glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,32,10,0,GL_RED,GL_FLOAT,tab);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D,0);
 }
 {const char*q=ncz_opts_get(&g_o,"quality");if(strcmp(q,"auto"))setenv("NCZ_GPU_TIER",q,1);}
 uint32_t z=seed();
 s->launch_seed=z;
 fprintf(stderr,"[diag] blackhole launch seed=%u (reproduce with NCZ_BLACKHOLE_SEED=%u or --seed=%u)\n",z,z,z);
 s->v[0]=(float)z;
 s->v[1]=rnd(&z,.82,1.15);
 s->v[2]=rnd(&z,.45,1);
 s->v[3]=rnd(&z,.72,1.3);
 s->v[4]=(rnd(&z,0,1)<.5?-1:1)*rnd(&z,.65,1.35);
 s->v[5]=rnd(&z,.18,.48);
 s->v[6]=rnd(&z,.055,.125);
 // u_jet: the operator observed 0.000 in one launch. Cause: 58% of launches
 // drew jet=0 (jet disabled). That made the jet feature effectively dead
 // even though the shader supports it. Always draw a positive jet; the
 // intensity range still spans "faint plume" (0.15) to "bright jet" (0.95)
 // so the visual varies per launch. The old off/on draw has been removed.
 s->v[7]=rnd(&z,.15,.95);
 s->v[8]=rnd(&z,.55,1.25);
 s->v[9]=(float)((z>>8)%4);
 // 0..5: five named palettes + psychedelic (5). ~1/6 chance of psychedelic
 // each launch, with the operator-requested "restrained" ultraviolet kept as
 // palette 3 so that branch of the random draw still produces a classical
 // look.
 s->v[10]=(float)((z>>16)%6);
 s->v[11]=(rnd(&z,0,1)<.5?-1:1)*rnd(&z,.82,1.22);
 s->v[12]=rnd(&z,5.4,6.35);
 // nebula vec4: hue, spatial scale, cloud coverage (raised low end so the
 // sampled range is useful across its whole span), yaw.
 s->v[13]=rnd(&z,0,1);
 s->v[14]=rnd(&z,2.8,6.5);
 s->v[15]=rnd(&z,.28,.95);  // nebula_coverage (was 0..1; low end was wasted)
 s->v[16]=rnd(&z,0,6.2831853);
 s->v[17]=rnd(&z,-1.4,1.4);
 s->v[18]=rnd(&z,0,128);
 // Palette time evolution: cycle period ~72s (rate = 1/72 /s). Random rate
 // multiplier 0.6..1.4 so two launches never lockstep.
 s->v[19]=rnd(&z,0,1);
 s->v[20]=rnd(&z,0.0084,0.0196);  // ~53..123s cycle period
 // Contrast toe: 0.95..1.25 randomised; high values restore the deep blacks
 // and contrast that the Reinhard tonemap was flattening.
 s->v[21]=rnd(&z,.95,1.25);
 // Decorrelation scheme for nebula hue vs disk hue: 0=complement, 1=triad,
 // 2=split-complement. Drawn per-launch so the relationship is visible.
 s->v[22]=(float)(z%3);
 // ---------------------------------------------------------------------
 // Per-launch flight-path parameters (v[23..34]).
 // These replace the hardcoded curve coefficients that used to live in
 // disk_color's camera block. Each launch now draws a different shape, not
 // a rescaled copy. The mode still selects the *character* (diving arc,
 // slingshot, polar pass, enveloped arrival); the magnitudes / harmonics
 // are randomised within each so the specific path varies.
 // ---------------------------------------------------------------------
 // Base distance from the BH for the arc. The four modes used 9.8..18;
 // widen so even mode 3's "fast arrival" can be a slow one.
 s->v[23]=rnd(&z,9.2,18.5);
 // Radial swing amplitude (the coefficient in front of cos(phase) in dist).
 // 2.8..4.8 covers everything the four modes used and beyond; too low and
 // the camera is essentially stationary, too high and the path crosses
 // u_periapsis (clamped later at 5.4).
 s->v[24]=rnd(&z,2.8,4.8);
 // Secondary dist harmonic: amplitude and frequency multiplier on a sin(2*ph)
 // style wiggle. Modes 0/1 used this directly; mode 2 used a cos(.91*ph)
 // stretch and mode 3 uses envelopes (this term is only used for modes 0/1).
 s->v[25]=rnd(&z,0.5,1.4);
 s->v[26]=rnd(&z,1.6,2.4);
 // Orbital rate multiplier on the phase ramp. Modes used 1.0..1.35; widen
 // so some launches trace a leisurely arc and others a fast sweep.
 s->v[27]=rnd(&z,0.85,1.55);
 // Orbital secondary-harmonic amplitude (the .28/.34/.55 in front of the
 // trig term on orbit). Widen so some arcs wobble visibly, others are clean.
 s->v[28]=rnd(&z,0.18,0.55);
 // Orbital secondary-harmonic frequency multiplier on phase.
 s->v[29]=rnd(&z,1.6,2.4);
 // How many orbits the arc covers before phase wraps. 1 = single pass,
 // 2 = double, 3 = triple. Combined with the slow orbit_rate this
 // determines how long the user sees a coherent shot.
 s->v[30]=rnd(&z,1.0,3.0);
 // Prograde (+1) or retrograde (-1) relative to the disk's spin. Flips
 // orbit direction and (for mode 3) roll direction. Equal probability.
 s->v[31]=(rnd(&z,0,1)<.5)?-1.f:1.f;
 // Elevation swing amplitude and frequency. Modes used .48..78 and
 // .69..1.17; widen so the arc climbs/falls more or less.
 s->v[32]=rnd(&z,0.32,0.92);
 s->v[33]=rnd(&z,0.65,1.35);
 // Deterministic per-launch phase offset so two launches of the same mode
 // aren't sitting at the same point on the curve at t=0.
 s->v[34]=rnd(&z,0,6.2831853);
 // ---------------------------------------------------------------------
 // Disk axis tilt + slow precession (v[35..38] as the four vec4 components
 // u_disk_axis.xyz + wobble, u_disk_precess.xyz + rate; then v[37] used as
 // the scalar camera_family at draw time).
 //
 // u_disk_axis: v[35..38]
 //   v[35,36,37] = spin axis (uniform on the sphere, Marsaglia)
 //   v[38]       = wobble half-angle in [-1,1] (0=disk perp to axis,
 //                 |w|=1 tilts the disk up to ~pi/2 from the axis)
 // u_disk_precess: stored at v[35..38] then MOVED to the drawing step —
 // for clarity we lay both vec4s side-by-side using v[35..42]: v[35..38]
 // is disk_axis, v[39..42] is disk_precess. The v[] array is float[38],
 // so we need 8 floats. Recompute: v[35..38] = disk_axis, v[39..42] =
 // disk_precess, v[43] = camera_family. That requires extending v[] to
 // 44 floats; see State struct above. The layout used here is:
 //   v[35,36,37] spin axis x,y,z     v[38] wobble
 //   v[39,40,41] precession axis x,y,z v[42] precession rate rad/s
 //   v[43] camera_family (0 bound, 1 unbound)
 // ---------------------------------------------------------------------
 {
  // Spin axis: uniform on the sphere via Marsaglia (cos^2 + sin^2 = 1,
  // and z=1-2u1 gives a uniform z distribution).
  float u1=rnd(&z,0,1),u2=rnd(&z,0,1);
  float cz=1.f-2.f*u1;
  float sn=sqrtf(fmaxf(0.f,1.f-cz*cz));
  float th=6.2831853f*u2,ct=cosf(th),st=sinf(th);
  s->v[35]=ct*sn;
  s->v[36]=st*sn;
  s->v[37]=cz;
  // Wobble in [-1,1] so the disk normal can swing either side of the
  // spin axis. Symmetric draw so + and - each get 50%.
  s->v[38]=(rnd(&z,0,1)<.5?-1.f:1.f)*rnd(&z,0,1);
  // Precession axis: another uniform draw on the sphere. We can't reuse
  // the spin axis because we want the disk normal to swing AROUND a
  // different axis (the typical Lense-Thirring picture is that the spin
  // axis is mostly fixed and the disk normal precesses about it).
  u1=rnd(&z,0,1); u2=rnd(&z,0,1);
  cz=1.f-2.f*u1;
  sn=sqrtf(fmaxf(0.f,1.f-cz*cz));
  th=6.2831853f*u2; ct=cosf(th); st=sinf(th);
  s->v[39]=ct*sn;
  s->v[40]=st*sn;
  s->v[41]=cz;
  // Rate in rad/s: slow enough that a 60s capture shows a visible but
  // unhurried motion (60s * 0.05 rad/s = 3 rad ~ 172 deg).
  s->v[42]=rnd(&z,0.02,0.08);
  // Camera trajectory family: 50/50 between the two real families.
  // Bound = zoom-whirl, unbound = hyperbolic flyby. Both families
  // must be reachable; this is the family selector.
  s->v[43]=(rnd(&z,0,1)<.5)?0.f:1.f;
 }
 // ---------------------------------------------------------------------
 // Real trajectory family parameters (v[44..46]).
 // zoom-whirl ratio q is the angular/rev-frequency ratio per radial
 // cycle. Rational -> closed rosette, irrational -> aperiodic. Drawn
 // in [1.4, 5.5]: sub-1.5 looks like a circle and is boring; above 5.5
 // the whirls blur. The interval is mostly irrational so most launches
 // trace a path that never repeats.
 //   v[44]: orbit_q (zoom-whirl ratio, drawn if family=0; otherwise 1)
 // orbit_e is eccentricity. Family=0 draws [0.15, 0.7] for the bound
 // zoom-whirl; family=1 draws (1.05, 3.0] for the hyperbolic flyby.
 // orbit_omega is the argument of periapse: drawn [0, 2pi].
 // ---------------------------------------------------------------------
 {
  int family=(int)s->v[43];
  if(family==0){
   // Zoom-whirl. q in [1.4, 5.5] mostly avoids rational coincidences;
   // the spread covers loose 3-leaf clovers through tight precessing
   // rosettes. Combined with eccentricity this draws a different
   // rosette essentially every launch.
   s->v[44]=rnd(&z,1.15,2.8);
   // Eccentricity: 0.15 keeps the orbit nearly circular (mild zoom);
   // 0.7 is strongly elongated (deep periapsis dips). Both visible.
   s->v[45]=rnd(&z,0.15,0.7);
  }else{
   // Hyperbolic flyby. q does not apply (only one traversal). We
   // still set it so the shader has a defined value; the shader
   // ignores it in the flyby branch.
   s->v[44]=1.f;
   // Eccentricity > 1 for a hyperbola. deflection = 2*asin(1/e):
   // e=1.05 -> deflection ~ 144 deg (extreme slingshot);
   // e=3.0 -> deflection ~ 39 deg (gentle drift-by). 1.05..3.0 spans
   // the full useful range from "violent whip-around" to
   // "barely-bent drift past".
   s->v[45]=rnd(&z,1.05,3.0);
  }
  // Argument of periapse: 0..2pi draws the periapse direction in the
  // orbital plane uniformly. Combined with the disk's own tilt this
  // gives the family a different sweep orientation every launch.
  s->v[46]=rnd(&z,0,6.2831853);
 }
 // ---------------------------------------------------------------------
 // Audit (revalidation-2026-09-26): every uniform's draw, range, and the
 // visible variety it produces. Reachable / distribution / change-of-mind.
 //   u_seed             uint32       full       random per launch
 //   u_radius           0.82..1.15   uniform     ~40% zoom-in variation,
 //                                                visible in disk angular size
 //   u_temperature      0.45..1.0    uniform     maps to disk "heat" and
 //                                                paletteT (Reinhard input
 //                                                warmth), clearly different
 //   u_density          0.72..1.3    uniform     disk surface brightness;
 //                                                doubles are obvious
 //   u_rotation         +/- 0.65..1.35  symmetric  spin direction and rate,
 //                                                visible Doppler band motion
 //   u_inclination      0.18..0.48   uniform     ~30° elevation range;
 //                                                disk silhouette changes
 //   u_orbit_rate       0.140..0.265 uniform     widened from 0.175..0.235
 //                                                so a path_o_count=1 launch
 //                                                can take ~24s or ~45s;
 //                                                combined with path_o_count
 //                                                1..3 the period spans
 //                                                ~7.9..44.9s
 //   u_jet              0.15..0.95   uniform     FIXED: was 58% chance of 0
 //                                                (operator observed 0.000);
 //                                                now always-on with varied
 //                                                intensity. The shader
 //                                                branches on u_jet>0 so
 //                                                every launch sees a jet
 //   u_star_density     0.55..1.25   uniform     star coverage; 2.3x spread
 //   u_camera_mode      0..3         uniform     (z>>8)%4: all four modes
 //                                                equally likely (~25% each)
 //   u_palette          0..5         uniform     (z>>16)%6: five named + one
 //                                                psychedelic, ~16.7% each
 //   u_approach         +/- 0.82..1.22  symmetric  direction and approach
 //                                                intensity; both signs
 //                                                equiprobable
 //   u_periapsis        5.4..6.35    uniform     close approach distance;
 //                                                dist is clamped >=5.4
 //                                                so the camera never crosses
 //                                                the event horizon
 //   u_nebula.x         0..1         uniform     nebula base hue
 //   u_nebula.y         2.8..6.5     uniform     nebula spatial scale; 2.3x
 //                                                spread visibly varies cloud
 //                                                size
 //   u_nebula.z         0.28..0.95   uniform     nebula coverage; previous
 //                                                range 0..1 wasted the
 //                                                0..0.28 segment, fixed
 //                                                earlier
 //   u_nebula.w         0..2pi       uniform     nebula yaw
 //   u_nebula_axis.x    -1.4..1.4    symmetric   nebula tilt; can flip sign
 //   u_nebula_axis.y    0..128       uniform     nebula noise offset
 //   u_palette_phase    0..1         uniform     hue rotation start
 //   u_palette_rate     0.0084..0.0196  uniform  ~53..123s cycle period
 //                                                (2pi / rate)
 //   u_palette_contrast 0.95..1.25   uniform     toe contrast; shader clamps
 //                                                to 0.85..1.35 so the
 //                                                0.95 floor is just above
 //                                                the clamp floor. Intentionally
 //                                                narrow because too-wide
 //                                                contrast makes the disk
 //                                                wash to black or blow to
 //                                                white.
 //   u_nebula_scheme    0..2         uniform     z%3: complement / triad /
 //                                                split-complement, equal
 //   u_path_d_base      9.2..18.5    uniform     widened so even mode 3 can
 //                                                be a slow arrival
 //   u_path_d_swing     2.8..4.8     uniform     visible radial swing
 //   u_path_d_harm_amp  0.5..1.4     uniform     secondary dist harmonic
 //   u_path_d_harm_freq 1.6..2.4     uniform     secondary dist harmonic
 //   u_path_o_rate      0.85..1.55   uniform     orbital rate multiplier
 //   u_path_o_harm_amp  0.18..0.55   uniform     orbital harmonic amplitude
 //   u_path_o_harm_freq 1.6..2.4     uniform     orbital harmonic frequency
 //   u_path_o_count     1..3         uniform     orbits before phase wraps
 //   u_path_sign        -1 or +1     binomial   prograde/retrograde,
 //                                                equiprobable
 //   u_path_e_swing     0.32..0.92   uniform     elevation swing amplitude
 //   u_path_e_freq      0.65..1.35   uniform     elevation swing frequency
 //   u_path_phase_jitter 0..2pi      uniform     per-launch phase offset
 // Deliberately left narrow:
 //   u_palette_contrast 0.95..1.25 - the shader clamps to 0.85..1.35; below
 //       0.95 the S-curve collapses and the disk washes to grey, above 1.25
 //       the dark stops blow out. This band is the safe range.
 //   u_periapsis 5.4..6.35 - close enough to the event horizon (2.598) for a
 //       dramatic lensing pass, but high enough that the floor clamp at 5.4
 //       never bites.
 // ---------------------------------------------------------------------
 /* ---- option overrides of the per-launch draws (defaults change nothing) */
 s->v[3]*=(float)ncz_opts_get_float(&g_o,"disk-brightness");
 s->v[7]*=(float)ncz_opts_get_float(&g_o,"jet");
 s->v[8]*=(float)ncz_opts_get_float(&g_o,"star-density")*((!strcmp(ncz_gpu_class_current(),"weak")&&!ncz_opts_is_set(&g_o,"star-density"))?0.5f:1.f);
 s->v[2]*=(float)ncz_opts_get_float(&g_o,"temperature");
 s->v[4]*=(float)ncz_opts_get_float(&g_o,"rotation-speed")*(1.f+1.5f*g_b.spin);
 s->v[42]*=(float)ncz_opts_get_float(&g_o,"precession");
 if(g_b.incl>=0)s->v[5]=g_b.incl*0.01745329f;
 if(g_b.flyby==0&&g_b.dmin>0)s->v[12]=fmaxf((float)g_b.dmin,5.4f);
 s->v[1]*=BH_SC_ZOOM[g_b.sc.scene];
 {double lens=ncz_opts_get_float(&g_o,"lens");if(lens>0)s->v[1]=(float)lens;}
 if(g_b.flyby!=0){s->v[35]=0;s->v[36]=0;s->v[37]=1;s->v[38]=0;s->v[42]=0;}
 fprintf(stderr,"[diag] blackhole nebula_hue=%.9g nebula_scale=%.9g nebula_coverage=%.9g nebula_yaw=%.9g nebula_tilt=%.9g nebula_offset=%.9g nebula_scheme=%d\n",
  s->v[13],s->v[14],s->v[15],s->v[16],s->v[17],s->v[18],(int)s->v[22]);
 s->started=now();
 /* Initialise the adaptive quality tier module. Reads NCZ_GPU_TIER
  * override, queries GL hints, and arms the rolling-median frame
  * timer. The static prior appears in the [diag] line below as
  * "tier=" so a capture can be attributed to a tier after the fact. */
 ncz_gpu_tier_init();
 fprintf(stderr,"[diag] blackhole seed=%u radius=%.3f temp=%.3f density=%.3f rotation=%.3f inclination=%.3f flyby=%d camera_rate=%.4f palette=%d approach=%.3f periapsis=%.3f jet=%.3f stars=%.3f palette_phase=%.4f palette_rate=%.5f palette_contrast=%.3f path_d_base=%.3f path_d_swing=%.3f path_d_harm_amp=%.3f path_d_harm_freq=%.3f path_o_rate=%.3f path_o_harm_amp=%.3f path_o_harm_freq=%.3f path_o_count=%.2f path_sign=%.0f path_e_swing=%.3f path_e_freq=%.3f path_phase_jitter=%.4f disk_axis=(%.3f,%.3f,%.3f,wobble=%.3f) precess_rate=%.4f camera_family=%d orbit_q=%.4f orbit_e=%.4f orbit_omega=%.3f GL=%s\n",
  s->launch_seed,s->v[1],s->v[2],s->v[3],s->v[4],s->v[5],(int)s->v[9],s->v[6],(int)s->v[10],s->v[11],s->v[12],s->v[7],s->v[8],s->v[19],s->v[20],s->v[21],
  s->v[23],s->v[24],s->v[25],s->v[26],s->v[27],s->v[28],s->v[29],s->v[30],s->v[31],s->v[32],s->v[33],s->v[34],
  s->v[35],s->v[36],s->v[37],s->v[38],s->v[42],(int)s->v[43],s->v[44],s->v[45],s->v[46],glGetString(GL_VERSION));
  /* Tier attribution: every [diag] line includes the chosen tier and a
   * short reason. The runtime may later downgrade if frame-time is over
   * budget; the next [diag] line will reflect it. */
  const ncz_gpu_tier_t *_tier=ncz_gpu_tier_current();
  /* Start at the tier's static prior but let the adaptive controller climb
   * to the full step budget when frames have headroom (it used to be capped
   * at the prior: Mali-G720 sat at level 0.33, 139 steps, for a whole run). */
  s->level=_tier->scalar;s->levelmax=1.f;
  fprintf(stderr,"[diag] blackhole tier=%s scalar=%.2f reason=\"%s\"\n",
   _tier->name,_tier->scalar,_tier->reason);
  fprintf(stderr,"[diag] blackhole scene=%s quality=%s cycle=%.0fs flyby=%d\n",ncz_opts_get(&g_o,"scene"),
   g_b.sc.light?"light":"rich",g_b.sc.cycle,g_b.flyby);
}

/* ---- frame pacing and adaptive quality ---------------------------------
 * The GPU tier module's frame-time hook was never wired to this hack, so the
 * quality was the static prior for the whole run: on a heavy panel that misses
 * vsync every few frames and looks like stutter. This controller measures the
 * frame period, estimates the display refresh from the first frames, counts
 * frames that miss it and moves one "level" knob (0..1 = ray steps 80..260)
 * with hysteresis. Resolution is handled by the shared harness (render-scale).
 * It steps down when more than 10% of the last 60 frames missed, steps up
 * slowly after 10 s without a miss, and never returns to a level that failed
 * until 90 s have passed, so it converges instead of oscillating.
 *
 * The miss / near decision uses the ACTIVE feed cadence (the budget the
 * renderer publishes through ncz_time_offset_frame before each draw), not
 * just the panel period. Without that, an intentional 30 fps cap (33.33 ms
 * inter-frame) or a warm-15 fallback (66.67 ms) is misclassified as missed
 * display frames on a 60 Hz panel and the controller drops ray steps all
 * the way to the floor. Standalone / on-screen runs (ncz_time_offset not
 * armed) keep the original panel-only behaviour. A change in the active
 * budget rebases the miss / near window and the budget-specific failure
 * history without forcing quality back up. */
static int cmpf(const void*a,const void*b){float x=*(const float*)a,y=*(const float*)b;return x<y?-1:x>y;}
static void bh_level_split(float level,float*q,float*scale){*q=level;*scale=1.f;}
/* Reset the adaptive controller's budget-specific failure history without
 * touching the current ray-step level. Called when the active feed budget
 * changes (warm <-> 30 <-> 60) so the previous mode's misses do not poison
 * the new mode's first window. The shared seam lives in bh_pacer_state.h so
 * the unit test can exercise the same code path. */
static void bh_pace(State*s,double tn){
 static int perf=-1;
 if(perf<0){const char*e=getenv("NCZ_BLACKHOLE_PERF_LOG");perf=e?(atoi(e)>=2?2:1):0;}
 /* The active feed budget the renderer publishes through ncz_time_offset_frame
  * BEFORE drawing. 0 when the renderer is not in off-screen frame-source mode
  * (standalone / harness / capture): the controller falls back to the panel
  * period only. */
 long long eff_budget_ns=ncz_time_offset_budget_ns();
 /* On a budget change (warm-15 <-> capped-30 <-> 60) rebase the miss/near
  * window and the budget-specific failure history without forcing quality. */
 int budget_changed=ncz_time_offset_budget_changed();
 if(budget_changed){
  fprintf(stderr,"[diag] blackhole pace budget change eff_ns=%lld panel_ms=%.2f\n",
   (long long)eff_budget_ns,s->refr);
  s->dtn=0;
 }
 if(s->prev_t>0){
  double dt=(tn-s->prev_t)*1000.;
  s->nfr++;
  if(s->nfr>30&&s->nfr<=150){s->wdt[s->nfr-31]=(float)dt;
   if(s->nfr==150){float t[120];memcpy(t,s->wdt,sizeof t);qsort(t,120,sizeof(float),cmpf);
    /* vsync-paced frames cluster at the refresh interval; assume 60 Hz unless the median says faster */
    s->refr=(t[60]<12.5f&&t[60]>=5.f)?t[60]:16.67;}}
  bh_pacer_state_update(&s->pace,s->nfr,dt,s->refr,eff_budget_ns,budget_changed);
  if(!budget_changed){
   if(perf>=2&&dt>25.&&s->nhit<16)s->hit[s->nhit++]=s->nfr;
   s->dtring[s->dtn++%300]=(float)dt;
  }
  if(s->nfr>150&&(s->nfr%30)==0){
   double miss_rate=s->pace.mcnt/60.;
   double target_ms=s->refr;
   if(eff_budget_ns>0){double m=(double)eff_budget_ns/1.0e6;if(m>target_ms)target_ms=m;}
   if(g_b.adaptive&&!budget_changed){
    /* step down on real misses, or when a quarter of the frames run over the
     * active cadence (a panel that is not vsync-locked shows long frames
     * instead of missed ones) */
    if((miss_rate>0.10||s->pace.ncnt>15)&&tn-s->last_change>1.0&&s->level>0.f){
     s->pace.cur_fail_level=s->level;s->level=fmaxf(0.f,s->level-0.08f);s->last_change=tn;
     float q,sc;bh_level_split(s->level,&q,&sc);
     fprintf(stderr,"[diag] blackhole adapt down level=%.2f steps=%d scale=%.2f miss=%.0f%% panel_ms=%.1f target_ms=%.1f\n",
      s->level,80+(int)(180*q+.5f),sc,miss_rate*100,s->refr,target_ms);
    }else if(s->pace.mcnt==0&&s->pace.ncnt==0&&tn-s->last_change>10.&&s->level<s->levelmax){   /* climb only with headroom */
     float cand=fminf(s->levelmax,s->level+0.04f);
     if(s->pace.cur_fail_level<=0.||cand<=s->pace.cur_fail_level-0.06){
      s->level=cand;s->last_change=tn;
      float q,sc;bh_level_split(s->level,&q,&sc);
      fprintf(stderr,"[diag] blackhole adapt up level=%.2f steps=%d scale=%.2f\n",s->level,80+(int)(180*q+.5f),sc);
     }
    }
    if(s->pace.cur_fail_level>0.&&tn-s->last_change>90.){s->pace.cur_fail_level+=0.1;if(s->pace.cur_fail_level>=1.)s->pace.cur_fail_level=0.;s->last_change=tn;}
   }
  }
  if(perf>=2&&s->nfr%300==0&&s->dtn>0){
   double target_ms=s->refr;
   if(eff_budget_ns>0){double m=(double)eff_budget_ns/1.0e6;if(m>target_ms)target_ms=m;}
   float w[300];int n=s->dtn<300?s->dtn:300;memcpy(w,s->dtring,sizeof(float)*n);qsort(w,n,sizeof(float),cmpf);
   int m=0;for(int k=0;k<n;k++)if(w[k]>target_ms*1.3)m++;
   float q,sc;bh_level_split(s->level,&q,&sc);
   fprintf(stderr,"[diag] blackhole pace frames=%lu p50=%.2f p95=%.2f p99=%.2f max=%.2f ms miss=%d/%d panel_ms=%.2f target_ms=%.2f level=%.2f steps=%d scale=%.2f\n",
    s->nfr,w[n/2],w[(int)(n*0.95)],w[(int)(n*0.99)],(double)w[n-1],m,n,s->refr,target_ms,s->level,g_b.adaptive?80+(int)(180*q+.5f):-1,sc);
   if(s->nhit){fprintf(stderr,"[diag] blackhole hitches(>25ms) at frames:");for(int k=0;k<s->nhit;k++)fprintf(stderr," %lu",s->hit[k]);fprintf(stderr,"\n");s->nhit=0;}
  }
 }else{s->refr=33.3;}
 s->prev_t=tn;
}
/* One full-screen pass with every uniform set: the screensaver frame and each
 * capture-quality sample go through exactly this code. dither: 1 = output
 * dither on, 0 = off, -1 = write linear radiance (capture accumulation). */
static void bh_draw_pass(State*s,int rw,int rh,double bt,double treal,int steps,float jx,float jy,float dither){
 glUseProgram(s->program);
 glUniform1f(s->time,(float)bt);
 glUniform2f(s->resolution,(float)rw,(float)rh);
 glUniform2f(s->jitter,jx,jy);glUniform1f(s->dither,dither);
 glUniform1f(s->seed,s->v[0]);
 glUniform1f(s->radius,s->v[1]);
 glUniform1f(s->temperature,s->v[2]);
 bh_frame_uniforms(s,bt,treal);
 glUniform1f(s->density,s->v[3]);
 glUniform1f(s->rotation,s->v[4]);
 glUniform1f(s->inclination,s->v[5]);
 glUniform1f(s->orbit_rate,s->v[6]);
 glUniform1f(s->jet,s->v[7]);
 glUniform1f(s->star_density,s->v[8]*s->sf.zoom[1]);
 glUniform1f(s->camera_mode,s->v[9]);
 glUniform1f(s->palette,s->v[10]);
 glUniform1f(s->approach,s->v[11]);
 glUniform1f(s->periapsis,s->v[12]);
 glUniform4fv(s->nebula,1,s->v+13);
 glUniform2fv(s->nebula_axis,1,s->v+17);
 glUniform1f(s->palette_phase,s->v[19]);
 glUniform1f(s->palette_rate,s->v[20]);
 glUniform1f(s->palette_contrast,s->v[21]);
 glUniform1f(s->nebula_scheme,s->v[22]);
 // Per-launch flight-path parameters. The shader uses these in place of
 // the previously-hardcoded curve coefficients in disk_color's camera
 // block; every launch draws a distinct path shape, not a rescaled one.
 glUniform1f(s->path_d_base,s->v[23]);
 glUniform1f(s->path_d_swing,s->v[24]);
 glUniform1f(s->path_d_harm_amp,s->v[25]);
 glUniform1f(s->path_d_harm_freq,s->v[26]);
 glUniform1f(s->path_o_rate,s->v[27]);
 glUniform1f(s->path_o_harm_amp,s->v[28]);
 glUniform1f(s->path_o_harm_freq,s->v[29]);
 glUniform1f(s->path_o_count,s->v[30]);
 glUniform1f(s->path_sign,s->v[31]);
 glUniform1f(s->path_e_swing,s->v[32]);
 glUniform1f(s->path_e_freq,s->v[33]);
 glUniform1f(s->path_phase_jitter,s->v[34]);
 // Disk axis (vec4 = unit spin axis xyz + wobble half-angle) and slow
 // precession (vec4 = unit precession axis xyz + rate rad/s).
 glUniform4fv(s->disk_axis,1,s->v+35);
 glUniform4fv(s->disk_precess,1,s->v+39);
 glUniform1f(s->camera_family,s->v[43]);
 // Real trajectory family params (zoom-whirl ratio, eccentricity,
 // periapse direction). Draws are conditional on the family in
 // init_blackhole; the shader uses whatever values are present.
 glUniform1f(s->orbit_q,s->v[44]);
 glUniform1f(s->orbit_e,s->v[45]);
 glUniform1f(s->orbit_omega,s->v[46]);
 /* Tier-driven step count. The Schwarzschild Binet integration in the
  * shader is the dominant cost on Intel UHD — 260 steps/pixel at 1080p is
  * the difference between 60fps and 8fps. Per-tier budgets:
  *   low=80, medium=140, high=200, ultra=260.
  * The scalar from the tier module is mapped via ncz_gpu_tier_count so
  * the transition is continuous if the runtime later downgrades/upgrades.
  */
 {
  glUniform1f(s->max_steps,(float)steps);
 }
 glUniform1f(s->kerr_on,s->kerr_tx?1.f:0.f);
 if(s->kerr_tx){glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,s->kerr_tx);glUniform1i(s->kerr_tab,0);}
 glBindBuffer(GL_ARRAY_BUFFER,s->vbo);
 glEnableVertexAttribArray(0);
 glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);
 glDisable(GL_DEPTH_TEST);
 glDrawArrays(GL_TRIANGLES,0,6);
 glDisableVertexAttribArray(0);
 glBindBuffer(GL_ARRAY_BUFFER,0);
 glUseProgram(0);
}
static void bh_capture(State*s,int w,int h);
static int bh_capture_still(State*s);
static void draw_blackhole(ModeInfo*m){
 State*s=m->data;
 if(!s||!s->program)return;
 int w=m->xgwa.width,h=m->xgwa.height;
 if(w<1)w=1;if(h<1)h=1;
 if(bh_capture_still(s))return;
 bh_pace(s,now());
 float lq=0.f,lscale=1.f;
 if(g_b.adaptive)bh_level_split(s->level,&lq,&lscale);
 int rw=w,rh=h;
 glViewport(0,0,rw,rh);
 glClear(GL_COLOR_BUFFER_BIT);
 /* NCZ_BLACKHOLE_T0 (seconds) starts the animation clock later: a test hook
  * for contact sheets at fixed scene times; not a user option. */
 static double t0=-1;if(t0<0){const char*e=getenv("NCZ_BLACKHOLE_T0");t0=e?atof(e):0;if(t0<0)t0=0;}
 double bt=(now()-s->started)*g_b.speed+t0;
 {
  /* Tier-driven step count: see bh_pace; low=80 .. ultra=260. */
  int steps=g_b.adaptive?80+(int)(180*lq+.5f):ncz_gpu_tier_count(80, 260);
  bh_draw_pass(s,rw,rh,bt,now()-s->started,steps,0.f,0.f,1.f);
 }
 // Per-vendor frame-time measurement. Logged to stderr at every 30th frame
 // only when NCZ_BLACKHOLE_PERF_LOG is set in the environment (so a real
 // run never pays the fprintf/fflush cost on Intel). Tag the line with
 // [frame_t] for downstream parsing.
 static unsigned long _ft_counter;
 static double _ft_prev;
 static int _ft_enabled;
 if(!_ft_enabled)_ft_enabled=(getenv("NCZ_BLACKHOLE_PERF_LOG")!=NULL);
 if(_ft_enabled){
  double _t_now=now();
  if((++_ft_counter%30)==0){
   if(_ft_prev>0.){
    double _dt=(_t_now-_ft_prev)/30.;
    fprintf(stderr,"[diag] blackhole frame_t frame=%lu dt_ms=%.3f\n",_ft_counter,_dt*1000.);
    fflush(stderr);
   }
   _ft_prev=now();
  }
 }
 bh_capture(s,w,h);
 static int once;
 if(!once++){
  unsigned char px[16]={0};
  glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
  glReadPixels(w/8,h/8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px+4);
  glReadPixels(7*w/8,h/8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px+8);
  glReadPixels(w/8,7*h/8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px+12);
  GLenum e=glGetError();
  fprintf(stderr,"[diag] blackhole first draw gl_error=0x%x samples=%u,%u,%u;%u,%u,%u;%u,%u,%u;%u,%u,%u uniforms=time:%d resolution:%d seed:%d camera:%d palette:%d approach:%d periapsis:%d\n",
   e,px[0],px[1],px[2],px[4],px[5],px[6],px[8],px[9],px[10],px[12],px[13],px[14],
   s->time,s->resolution,s->seed,s->camera_mode,s->palette,s->approach,s->periapsis);
  once=1;
 }
}
/* ---- long-run capture (test hook, NCZ_BLACKHOLE_LONGCAP=<dir>; not NCZ_BLACKHOLE_CAPTURE, which is the env name of --capture) -----------
 * For the 30-minute acceptance captures: a half-size thumbnail every 2 s
 * (t_<seconds>.ppm, for the clip and the exposure/choreography analysis) and,
 * every 30 s, three consecutive full-resolution 384x384 center crops
 * (b_<seconds>_<i>.ppm, for frame-to-frame shimmer and noise swimming). The
 * readbacks cost a few ms on the frames they happen; p95 is unaffected. */
static void cap_write(const char*path,const unsigned char*rgba,int w,int h,int step){
 FILE*f=fopen(path,"wb");if(!f)return;
 int ow=w/step,oh=h/step;fprintf(f,"P6\n%d %d\n255\n",ow,oh);
 unsigned char*row=malloc((size_t)ow*3);if(!row){fclose(f);return;}
 for(int y=oh-1;y>=0;y--){                               /* GL rows are bottom-up */
  for(int x=0;x<ow;x++){unsigned a[3]={0,0,0};
   for(int j=0;j<step;j++)for(int i=0;i<step;i++){const unsigned char*q=rgba+4*((size_t)(y*step+j)*w+(size_t)(x*step+i));a[0]+=q[0];a[1]+=q[1];a[2]+=q[2];}
   for(int c=0;c<3;c++)row[3*x+c]=(unsigned char)(a[c]/(unsigned)(step*step));}
  fwrite(row,1,(size_t)ow*3,f);}
 free(row);fclose(f);
}
static void bh_capture(State*s,int w,int h){
 static int on=-1;static char dir[400];static double last=-1e9,lastb=-1e9;static int burst=0;
 if(on<0){const char*e=getenv("NCZ_BLACKHOLE_LONGCAP");on=e&&*e;if(on){snprintf(dir,sizeof dir,"%s",e);mkdir(dir,0755);}}
 if(!on)return;
 double tn=now()-s->started;char path[512];
 if(burst>0||tn-lastb>=30.){
  if(burst==0){burst=3;lastb=tn;}
  int cw=w<384?w:384,ch=h<384?h:384;
  unsigned char*px=malloc((size_t)cw*ch*4);if(!px)return;
  glReadPixels((w-cw)/2,(h-ch)/2,cw,ch,GL_RGBA,GL_UNSIGNED_BYTE,px);
  snprintf(path,sizeof path,"%s/b_%07.1f_%d.ppm",dir,lastb,3-burst);cap_write(path,px,cw,ch,1);free(px);burst--;
 }else if(tn-last>=2.){
  last=tn;unsigned char*px=malloc((size_t)w*h*4);if(!px)return;
  glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,px);
  snprintf(path,sizeof path,"%s/t_%07.1f.ppm",dir,tn);cap_write(path,px,w,h,2);free(px);
 }
}
/* ---- capture-quality mode (--capture=PATH) -----------------------------
 * Renders exactly what the screensaver renders, better sampled, at fixed scene
 * times: frame i shows scene time capture-at + i / capture-fps (the wall clock
 * plays no part, the adaptive controller is bypassed and the ray-step budget
 * is capture-steps). Each frame averages capture-supersample^2 stratified
 * jittered passes. With a half-float target the passes are averaged in linear
 * radiance and the shader's display transform (tone map, gamma, contrast,
 * black point) is applied afterwards, here; the output dither is off. Frames
 * are written as 16-bit PNG and the process exits after the last one. */
static struct{int on,W,H,ss,steps,n,i;double at,fps;char pat[400];GLuint fbo,tex;int fl,fp32,fp16;float*acc,*row;}g_cap;
static int png16(const char*path,const float*rgb,int W,int H){
 FILE*f=fopen(path,"wb");if(!f)return -1;
 png_structp png=png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
 png_infop info=png?png_create_info_struct(png):NULL;
 unsigned char*row=malloc((size_t)W*6);
 if(!png||!info||!row){if(png)png_destroy_write_struct(&png,info?&info:NULL);free(row);fclose(f);return -1;}
 if(setjmp(png_jmpbuf(png))){png_destroy_write_struct(&png,&info);free(row);fclose(f);return -1;}
 png_init_io(png,f);
 png_set_IHDR(png,info,(png_uint_32)W,(png_uint_32)H,16,PNG_COLOR_TYPE_RGB,PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
 png_set_sRGB_gAMA_and_cHRM(png,info,PNG_sRGB_INTENT_PERCEPTUAL);
 png_write_info(png,info);
 for(int y=0;y<H;y++){
  const float*q=rgb+(size_t)(H-1-y)*W*3;               /* GL rows are bottom-up */
  for(int x=0;x<W*3;x++){float v=q[x];v=v<0?0:v>1?1:v;unsigned u=(unsigned)(v*65535.f+.5f);row[2*x]=(unsigned char)(u>>8);row[2*x+1]=(unsigned char)u;}
  png_write_row(png,row);
 }
 png_write_end(png,NULL);png_destroy_write_struct(&png,&info);free(row);fclose(f);return 0;
}
static int bh_capture_still(State*s){
 static int init=-1;
 if(init<0){
  init=0;
  const char*p=ncz_opts_get(&g_o,"capture");
  if(!p||!*p)return 0;
  g_cap.on=1;snprintf(g_cap.pat,sizeof g_cap.pat,"%s",p);
  g_cap.at=ncz_opts_get_float(&g_o,"capture-at");g_cap.fps=ncz_opts_get_float(&g_o,"capture-fps");
  g_cap.n=(int)ncz_opts_get_float(&g_o,"capture-frames");g_cap.ss=(int)ncz_opts_get_float(&g_o,"capture-supersample");
  g_cap.steps=(int)ncz_opts_get_float(&g_o,"capture-steps");
  if(sscanf(ncz_opts_get(&g_o,"capture-size"),"%dx%d",&g_cap.W,&g_cap.H)!=2||g_cap.W<16||g_cap.H<16){g_cap.W=3840;g_cap.H=2160;}
  GLint mx=0;glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&mx);
  if(mx>0&&(g_cap.W>mx||g_cap.H>mx)){fprintf(stderr,"blackhole: capture size %dx%d exceeds the GPU limit %d\n",g_cap.W,g_cap.H,mx);ncz_harness_die(1);return 1;}
  /* True fp32 linear sample accumulation (32-64 sample artist masters need
   * fp32 to avoid the banding / dark-side precision loss of half-float when
   * summing many HDR radiance samples per pixel). We prefer GL_RGBA32F when
   * the GPU exposes it, fall back to GL_RGBA16F (Mali-G720 / older iGPUs),
   * then GL_RGBA8 as a last resort (no FBO completeness is a hard error).
   * Memory bound: W*H*3*sizeof(float) accumulator + 64-row RGBA strip. */
  glGenTextures(1,&g_cap.tex);glBindTexture(GL_TEXTURE_2D,g_cap.tex);
  glGenFramebuffers(1,&g_cap.fbo);glBindFramebuffer(GL_FRAMEBUFFER,g_cap.fbo);
  g_cap.fl=0;g_cap.fp32=0;g_cap.fp16=0;
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,g_cap.W,g_cap.H,0,GL_RGBA,GL_FLOAT,NULL);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g_cap.tex,0);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE){
   g_cap.fl=1;g_cap.fp32=1;
  }else{
   glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,g_cap.W,g_cap.H,0,GL_RGBA,GL_HALF_FLOAT,NULL);
   if(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE){g_cap.fl=1;g_cap.fp16=1;}
   else{
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,g_cap.W,g_cap.H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){fprintf(stderr,"blackhole: capture framebuffer incomplete\n");ncz_harness_die(1);return 1;}
   }
  }
  glBindFramebuffer(GL_FRAMEBUFFER,0);
  g_cap.acc=calloc((size_t)g_cap.W*g_cap.H*3,sizeof(float));
  g_cap.row=malloc((size_t)g_cap.W*64*4*(g_cap.fl?sizeof(float):1));   /* 64-row strips */
  if(!g_cap.acc||!g_cap.row){fprintf(stderr,"blackhole: capture buffers\n");ncz_harness_die(1);return 1;}
  fprintf(stderr,"[capture] %dx%d, %dx%d samples, %d steps, %s, frames %d from t=%.3f at %.3f fps -> %s\n",
   g_cap.W,g_cap.H,g_cap.ss,g_cap.ss,g_cap.steps,g_cap.fp32?"fp32 linear accumulation":g_cap.fp16?"half-float linear accumulation":"8-bit display accumulation",g_cap.n,g_cap.at,g_cap.fps,g_cap.pat);
 }
 if(!g_cap.on)return 0;
 /* NCZ_BLACKHOLE_CAPTURE_SHADER_TAIL=1: let the shader apply its own display
  * transform (as on screen) instead of the C copy; tools/blackhole-eval/
  * capture_parity.py compares the two to prove they are the same curve. */
 static int shtail=-1;if(shtail<0){const char*e=getenv("NCZ_BLACKHOLE_CAPTURE_SHADER_TAIL");shtail=e&&*e&&*e!='0';}
 int lin=g_cap.fl&&!shtail;
 double bt=g_cap.at+g_cap.i/g_cap.fps,treal=bt/(g_b.speed>0?g_b.speed:1);
 size_t np=(size_t)g_cap.W*g_cap.H;
 memset(g_cap.acc,0,np*3*sizeof(float));
 glBindFramebuffer(GL_FRAMEBUFFER,g_cap.fbo);
 int ns=g_cap.ss*g_cap.ss;
 for(int k=0;k<ns;k++){
  float jx=((k%g_cap.ss)+.5f)/g_cap.ss-.5f,jy=((k/g_cap.ss)+.5f)/g_cap.ss-.5f;
  glViewport(0,0,g_cap.W,g_cap.H);glClear(GL_COLOR_BUFFER_BIT);
  bh_draw_pass(s,g_cap.W,g_cap.H,bt,treal,g_cap.steps,jx,jy,lin?-1.f:0.f);
  for(int y0=0;y0<g_cap.H;y0+=64){                     /* 64-row strips: bounded memory */
   int nr=g_cap.H-y0<64?g_cap.H-y0:64;
   float*a=g_cap.acc+(size_t)y0*g_cap.W*3;size_t n=(size_t)nr*g_cap.W;
   if(g_cap.fl){
    glReadPixels(0,y0,g_cap.W,nr,GL_RGBA,GL_FLOAT,g_cap.row);
    const float*q=(const float*)g_cap.row;
    for(size_t x=0;x<n;x++){a[3*x]+=q[4*x];a[3*x+1]+=q[4*x+1];a[3*x+2]+=q[4*x+2];}
   }else{
    glReadPixels(0,y0,g_cap.W,nr,GL_RGBA,GL_UNSIGNED_BYTE,g_cap.row);
    const unsigned char*q=(const unsigned char*)g_cap.row;
    for(size_t x=0;x<n;x++){a[3*x]+=q[4*x]/255.f;a[3*x+1]+=q[4*x+1]/255.f;a[3*x+2]+=q[4*x+2]/255.f;}
   }
  }
 }
 for(size_t i=0;i<np*3;i++)g_cap.acc[i]/=(float)ns;
 if(lin){
  /* The shader's display transform, applied after averaging in linear radiance. */
  float kc=s->v[21]<.85f?.85f:s->v[21]>1.35f?1.35f:s->v[21];
  int classic=g_b.tonemap>.5f;
  for(size_t i=0;i<np;i++){
   float*c=g_cap.acc+3*i;
   if(classic){for(int j=0;j<3;j++)c[j]=c[j]/(1.f+c[j]);}
   else{
    float L=.2126f*c[0]+.7152f*c[1]+.0722f*c[2];
    for(int j=0;j<3;j++)c[j]/=1.f+L;
    float mc=fmaxf(c[0],fmaxf(c[1],c[2]));if(mc>1.f)for(int j=0;j<3;j++)c[j]/=mc;
    float w=sstep(2.5f,14.f,L)*.55f;for(int j=0;j<3;j++)c[j]+=(1.f-c[j])*w;
   }
   for(int j=0;j<3;j++){float v=powf(fmaxf(c[j],0.f),1.f/2.2f);v=(v-.5f)*kc+.5f;v=fmaxf(v-.018f,0.f);c[j]=v;}
  }
 }
 glBindFramebuffer(GL_FRAMEBUFFER,0);
 char path[512];snprintf(path,sizeof path,g_cap.pat,g_cap.i);
 if(png16(path,g_cap.acc,g_cap.W,g_cap.H))fprintf(stderr,"[capture] cannot write %s\n",path);
 else fprintf(stderr,"[capture] frame %d t=%.3f seed=%u -> %s\n",g_cap.i,bt,s->launch_seed,path);
 if(++g_cap.i>=g_cap.n){fprintf(stderr,"[capture] done\n");fflush(stderr);exit(0);}
 glViewport(0,0,16,16);glClear(GL_COLOR_BUFFER_BIT);   /* keep the swap chain moving */
 return 1;
}
static void free_blackhole(ModeInfo*m){State*s=m->data;if(!s)return;if(s->kerr_tx)glDeleteTextures(1,&s->kerr_tx);if(s->vbo)glDeleteBuffers(1,&s->vbo);if(s->program)glDeleteProgram(s->program);free(s);m->data=0;}
static void reshape_blackhole(ModeInfo*m,int w,int h){(void)m;(void)w;(void)h;}
static Bool blackhole_handle_event(ModeInfo*m,XEvent*e){(void)m;(void)e;return False;}
static void release_blackhole(ModeInfo*m){(void)m;}
static ModeSpecOpt blackhole_opts={0,NULL,0,NULL,NULL};
struct xscreensaver_function_table blackhole_xscreensaver_function_table={.name="blackhole",.class_="BlackHole",.init_cb=init_blackhole,.draw_cb=draw_blackhole,.reshape_cb=reshape_blackhole,.event_cb=blackhole_handle_event,.free_cb=free_blackhole,.release_cb=release_blackhole,.opts=&blackhole_opts,.defaults_str=DEFAULTS};
