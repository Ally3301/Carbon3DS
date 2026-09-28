#include "engine_audio.h"
#include "audio.h"
#include "wav.h"
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EA_FIRST_CH 2
#define EA_SLOTS 4
#define EA_ENG_A 0
#define EA_ENG_B 1
#define EA_EXH_A 2
#define EA_EXH_B 3

typedef struct EngineSample {
    unsigned char *pcm;
    uint32_t bytes;
    WavInfo wav;
    ndspWaveBuf wave;
    int anchor;
    int loaded;
} EngineSample;

static EngineSample s[EA_SLOTS];
static int active, bank=47;
static char profile[32]="tuner_i4";
static const int anchor_id[4]={1,3,5,8};
static const float anchor_rpm[4]={900.0f,3200.0f,5400.0f,8000.0f};

/* Shared-bank policy: exact original per-car IDs are not known from the raw WAV
   names alone. These assignments deliberately follow engine family/timbre and
   can be tuned without touching the mixer. Traffic shares conservative banks. */
typedef struct Map { const char *id; int bank; const char *profile; } Map;
static const Map maps[]={
 {"MAZDA3",12,"economy_i4"},{"GOLF",12,"economy_i4"},{"COBALTSS",47,"sport_i4"},
 {"ECLIPSE",47,"turbo_i4"},{"LANCER",47,"turbo_i4"},{"WRXSTI",62,"turbo_flat4"},
 {"RX7",82,"rotary"},{"RX8",82,"rotary"},{"240SX",47,"tuner_i4"},
 {"MR2",47,"tuner_i4"},{"SUPRA",62,"turbo_i6"},{"SKYLINE",62,"turbo_i6"},
 {"350Z",76,"sport_v6"},{"TT",76,"sport_v6"},{"SOLSTICE",47,"sport_i4"},
 {"ELISE",47,"light_sport_i4"},{"300C",88,"v8"},{"GTO",88,"v8"},
 {"MUSTANGGT",88,"v8_muscle"},{"MUSTANG67",11,"classic_v8"},{"FIREBIRD",11,"classic_v8"},
 {"CORVETTE",88,"v8_sport"},{"FORDGT",88,"v8_sport"},
 {"DB9",76,"gt_v12"},{"SL65AMG",76,"gt_v12"},{"CARRERA4S",76,"flat6"},
 {"CARRERAGT",76,"exotic"},{"GALLARDO",76,"exotic"},{"MURCIELAGO",76,"exotic"},
 {"TAXI",12,"traffic_economy"},{"SEDAN",12,"traffic_economy"},{"SMALLSUV",12,"traffic_economy"},
 {"LARGESUV",88,"traffic_v8"},{"PICKUP",88,"traffic_v8"},{"CUBEVAN",12,"traffic_van"},
 {"PURSUITSEDAN1",88,"police_v8"},{"PURSUITSEDAN2",88,"police_v8"},
 {"PURSUITMUSCLE1",88,"police_v8"},{"PURSUITMUSCLE2",88,"police_v8"},
 {"PURSUITWAGON1",88,"police_v8"},{"PURSUITWAGON2",88,"police_v8"},
 {"PURSUITSUV1",88,"police_v8"},{"PURSUITSUV2",88,"police_v8"},
};

static void set_mix(int slot,float gain)
{
    float m[12]={0}; m[0]=m[1]=gain; ndspChnSetMix(EA_FIRST_CH+slot,m);
}
static void unload(int slot)
{
    int ch=EA_FIRST_CH+slot; ndspChnWaveBufClear(ch);
    if(s[slot].pcm) linearFree(s[slot].pcm);
    memset(&s[slot],0,sizeof(s[slot]));
}
static int load_slot(int slot,const char *layer,int anchor)
{
    if(!audio_available()) return -1;
    if(s[slot].loaded && s[slot].anchor==anchor) return 0;
    unload(slot);
    char path[128]; snprintf(path,sizeof(path),"romfs:/audio/engines/bank_%02d/%s_%d.wav",bank,layer,anchor_id[anchor]);
    FILE *fp=fopen(path,"rb"); if(!fp) return -1;
    WavInfo wi; if(wav_read_header(fp,&wi) || wi.channels!=1) { fclose(fp); return -1; }
    unsigned char *pcm=linearAlloc(wi.bytes); if(!pcm){fclose(fp);return -1;}
    if(fread(pcm,1,wi.bytes,fp)!=wi.bytes){fclose(fp);linearFree(pcm);return -1;} fclose(fp);
    s[slot].pcm=pcm;s[slot].bytes=wi.bytes;s[slot].wav=wi;s[slot].anchor=anchor;s[slot].loaded=1;
    memset(&s[slot].wave,0,sizeof(s[slot].wave)); s[slot].wave.data_vaddr=pcm;
    s[slot].wave.nsamples=wi.bytes/2; s[slot].wave.looping=true;
    DSP_FlushDataCache(pcm,wi.bytes);
    int ch=EA_FIRST_CH+slot; ndspChnReset(ch); ndspChnSetInterp(ch,NDSP_INTERP_LINEAR);
    ndspChnSetFormat(ch,NDSP_FORMAT_MONO_PCM16); ndspChnSetRate(ch,wi.rate); set_mix(slot,0);
    ndspChnWaveBufAdd(ch,&s[slot].wave); return 0;
}
static void pair_for_rpm(float rpm,int *lo,int *hi,float *t)
{
    if(rpm<=anchor_rpm[0]){*lo=*hi=0;*t=0;return;}
    if(rpm>=anchor_rpm[3]){*lo=*hi=3;*t=0;return;}
    int i=0; while(i<3 && rpm>anchor_rpm[i+1]) ++i;
    *lo=i;*hi=i+1;*t=(rpm-anchor_rpm[i])/(anchor_rpm[i+1]-anchor_rpm[i]);
}
static void update_layer(int a,int b,const char *layer,float rpm,float gain)
{
    int lo,hi;float t;pair_for_rpm(rpm,&lo,&hi,&t);
    load_slot(a,layer,lo); load_slot(b,layer,hi);
    float ga=(lo==hi)?gain:gain*cosf(t*1.57079632679f);
    float gb=(lo==hi)?0.0f:gain*sinf(t*1.57079632679f);
    set_mix(a,active?ga:0);set_mix(b,active?gb:0);
    /* Each recording is already an RPM anchor. Pitch only inside the interval;
       clamp it so timbre stays natural instead of becoming a chipmunk/drone. */
    float pa=fmaxf(.88f,fminf(1.12f,rpm/anchor_rpm[lo]));
    float pb=fmaxf(.88f,fminf(1.12f,rpm/anchor_rpm[hi]));
    if(s[a].loaded)ndspChnSetRate(EA_FIRST_CH+a,s[a].wav.rate*pa);
    if(s[b].loaded)ndspChnSetRate(EA_FIRST_CH+b,s[b].wav.rate*pb);
}
void engine_audio_init(void){active=0;}
void engine_audio_select_vehicle(const char *id)
{
    int next=47; const char *name="tuner_i4";
    for(unsigned i=0;i<sizeof(maps)/sizeof(maps[0]);++i) if(!strcmp(id,maps[i].id)){next=maps[i].bank;name=maps[i].profile;break;}
    if(next!=bank) for(int i=0;i<EA_SLOTS;++i) unload(i);
    bank=next;snprintf(profile,sizeof(profile),"%s/b%02d",name,bank);
}
void engine_audio_set_active(int on){active=on!=0;if(!active)for(int i=0;i<EA_SLOTS;++i)set_mix(i,0);}
void engine_audio_update(float rpm,float throttle)
{
    if (!audio_available())
        return;
    rpm = fmaxf(900.0f, fminf(8000.0f, rpm));
    throttle = fmaxf(0.0f, fminf(1.0f, throttle));
    /* RPM is not load. At high RPM on overrun keep mechanical engine audible,
       while exhaust retreats; under load the exhaust becomes dominant. */
    float eng=.20f+.18f*throttle;
    float exh=.05f+.46f*powf(throttle,.72f);
    update_layer(EA_ENG_A,EA_ENG_B,"eng",rpm,eng);
    update_layer(EA_EXH_A,EA_EXH_B,"exh",rpm,exh);
}
void engine_audio_pause(int p){if(audio_available())for(int i=0;i<EA_SLOTS;++i)ndspChnSetPaused(EA_FIRST_CH+i,p);}
void engine_audio_shutdown(void){for(int i=0;i<EA_SLOTS;++i)unload(i);}
const char *engine_audio_profile(void){return profile;}
