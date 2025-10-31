#ifndef ADPCM_H
#define ADPCM_H

#include <stdint.h>

int adpcm_encode(int16_t *indata, uint8_t *outdata, int len, int16_t *predsample, int16_t *index);

#endif
