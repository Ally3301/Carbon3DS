#include "wav.h"
#include <string.h>
#include <limits.h>
static uint32_t u32(const unsigned char *b) { return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24; }
static uint16_t u16(const unsigned char *b) { return b[0]|(uint16_t)b[1]<<8; }
int wav_read_header(FILE *fp, WavInfo *out)
{
    unsigned char h[16];
    memset(out, 0, sizeof(*out));
    if (fseek(fp, 0, SEEK_END)) return -1;
    long size = ftell(fp);
    rewind(fp);
    if (size < 12 || fread(h,1,12,fp)!=12 || memcmp(h,"RIFF",4) || memcmp(h+8,"WAVE",4)) return -1;
    uint32_t riff = u32(h+4);
    if (riff < 4 || riff > (unsigned long)size-8) return -1;
    long end = (long)riff+8;
    int fmt = 0;
    while (ftell(fp) <= end-8) {
        if (fread(h,1,8,fp)!=8) return -1;
        uint32_t n=u32(h+4);
        long pos=ftell(fp);
        if (n > (unsigned long)(end-pos)) return -1;
        if (!memcmp(h,"fmt ",4)) {
            if (n<16 || fread(h,1,16,fp)!=16 || u16(h)!=1 || u16(h+14)!=16) return -1;
            out->channels=u16(h+2); out->rate=u32(h+4);
            if ((out->channels!=1 && out->channels!=2) || out->rate<8000 || out->rate>48000
                || u16(h+12)!=out->channels*2 || u32(h+8)!=out->rate*out->channels*2) return -1;
            fmt=1;
        } else if (!memcmp(h,"data",4)) {
            out->offset=(uint32_t)pos; out->bytes=n;
        }
        if (fseek(fp,pos+n+(n&1),SEEK_SET)) return -1;
    }
    if (!fmt || !out->bytes || out->bytes % (out->channels*2)) return -1;
    return fseek(fp, out->offset, SEEK_SET) ? -1 : 0;
}
