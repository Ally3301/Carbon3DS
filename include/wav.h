#pragma once
#include <stdio.h>
#include <stdint.h>
typedef struct WavInfo { uint32_t rate, offset, bytes; uint16_t channels; } WavInfo;
int wav_read_header(FILE *fp, WavInfo *out);
