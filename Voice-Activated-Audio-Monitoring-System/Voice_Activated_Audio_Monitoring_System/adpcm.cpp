#include "adpcm.h"

static const int indexTable[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8,
  -1, -1, -1, -1, 2, 4, 6, 8,
};

static const int stepSizeTable[89] = {
   7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
   19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
   50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
   130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
   337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
   876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
   2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
   5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
   15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

int adpcm_encode(int16_t *indata, uint8_t *outdata, int len, int16_t *predsample, int16_t *index)
{
  int i;
  int bufferstep = 0;
  uint8_t outputbuffer = 0;
  int diff, step, delta, sign, vpdiff;

  for (i = 0; i < len; i++) {
    step = stepSizeTable[*index];
    diff = indata[i] - *predsample;
    sign = (diff < 0) ? 8 : 0;
    if (sign) diff = -diff;

    delta = 0;
    vpdiff = step >> 3;

    if (diff >= step) {
      delta |= 4;
      diff -= step;
      vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
      delta |= 2;
      diff -= step;
      vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
      delta |= 1;
      vpdiff += step;
    }

    if (sign)
      *predsample -= vpdiff;
    else
      *predsample += vpdiff;

    if (*predsample > 32767) *predsample = 32767;
    else if (*predsample < -32768) *predsample = -32768;

    delta |= sign;

    *index += indexTable[delta];
    if (*index < 0) *index = 0;
    if (*index > 88) *index = 88;

    if (bufferstep) {
      outputbuffer |= delta & 0x0F;
      outdata[i >> 1] = outputbuffer;
    } else {
      outputbuffer = (delta << 4) & 0xF0;
    }
    bufferstep = !bufferstep;
  }

  return len / 2;  // returns number of bytes written
}
