#include "audio.h"
#include "wav.h"
#include "engine_audio.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#define BLOCK_BYTES 16384
#define BUFFERS 3
typedef struct Stream {
    FILE *fp;
    WavInfo wav;
    uint32_t remaining;
    int loop;
    ndspWaveBuf buf[BUFFERS];
    unsigned char *pcm;
} Stream;
static Stream streams[4];
static const int stream_channel[4] = {0, 1, 6, 7};
static int ready;
static const char *status = "Audio unavailable";
static void stop(int id)
{
    Stream *s=&streams[id];
    if (ready) ndspChnWaveBufClear(stream_channel[id]);
    if (s->fp) fclose(s->fp);
    if (s->pcm) linearFree(s->pcm);
    memset(s,0,sizeof(*s));
}
static void pump(int id)
{
    Stream *s=&streams[id];
    if (!s->fp) return;
    for (int i=0;i<BUFFERS;++i) {
        ndspWaveBuf *b=&s->buf[i];
        if (b->status!=NDSP_WBUF_FREE && b->status!=NDSP_WBUF_DONE) continue;
        if (!s->remaining && s->loop) {
            if (fseek(s->fp,s->wav.offset,SEEK_SET)) { stop(id); return; }
            s->remaining=s->wav.bytes;
        }
        if (!s->remaining) continue;
        uint32_t n=s->remaining < BLOCK_BYTES ? s->remaining : BLOCK_BYTES;
        if (fread(s->pcm+i*BLOCK_BYTES,1,n,s->fp)!=n) { stop(id); return; }
        s->remaining-=n;
        memset(b,0,sizeof(*b));
        b->data_vaddr=s->pcm+i*BLOCK_BYTES;
        b->nsamples=n/(2*s->wav.channels);
        DSP_FlushDataCache((void *)b->data_vaddr,n);
        ndspChnWaveBufAdd(stream_channel[id],b);
    }
}
static void play(int id,const char *path,int loop)
{
    if (!ready) return;
    stop(id);
    Stream *s=&streams[id];
    s->fp=fopen(path,"rb");
    if (!s->fp || wav_read_header(s->fp,&s->wav)) { stop(id); status="Audio file failed"; return; }
    s->pcm=linearAlloc(BLOCK_BYTES*BUFFERS);
    if (!s->pcm) { stop(id); status="Audio memory failed"; return; }
    s->remaining=s->wav.bytes; s->loop=loop;
    int channel = stream_channel[id];
    ndspChnReset(channel);
    ndspChnSetInterp(channel,NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel,s->wav.rate);
    ndspChnSetFormat(channel,s->wav.channels==1 ? NDSP_FORMAT_MONO_PCM16 : NDSP_FORMAT_STEREO_PCM16);
    float mix[12]={0}; mix[0]=mix[1]=id==0 ? 0.35f : id==1 ? 0.8f : 0.58f;
    ndspChnSetMix(channel,mix);
    pump(id);
    status="NDSP PCM16 streaming";
}
int audio_init(void)
{
    ready=R_SUCCEEDED(ndspInit());
    status=ready ? "NDSP ready" : "No DSP: continuing silently";
    if (ready) ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    return ready ? 0 : -1;
}
int audio_available(void) { return ready; }
void audio_play(const char *path) { play(1,path,0); }
void audio_music(const char *path) { play(0,path,1); }
void audio_loop_effect(const char *path) { play(2,path,1); }
void audio_stop_loop_effect(void) { stop(2); }
void audio_loop_skid(const char *path) { play(3,path,1); }
void audio_stop_skid(void) { stop(3); }
void audio_update(void) { if (ready) for (int i=0;i<4;++i) pump(i); }
void audio_pause(int paused) { if (ready) { for (int i=0;i<4;++i) ndspChnSetPaused(stream_channel[i],paused); engine_audio_pause(paused); } }
const char *audio_status(void) { return status; }
void audio_shutdown(void) { stop(0); stop(1); stop(2); stop(3); if (ready) ndspExit(); ready=0; }
