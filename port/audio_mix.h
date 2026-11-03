/* DKU: software mixer behind the snd_sfx_* shim, shared by every platform.
   The platform pulls stereo s16 frames at AUDIO_MIX_RATE from its audio
   callback (or not at all, headless). */
#ifndef DKU_AUDIO_MIX_H
#define DKU_AUDIO_MIX_H

#include <stdint.h>

#define AUDIO_MIX_RATE 48000
#define AUDIO_MIX_CHANNELS 64

void audio_mix_init(void);
/* Mix n stereo frames (interleaved L, R) into out. Thread safe. */
void audio_mix(int16_t *out, int n);
/* Music streamed by the CD-DA shim: called inside audio_mix to add n
   stereo frames at AUDIO_MIX_RATE, scaled by gain. */
typedef void (*audio_music_fn)(int32_t *acc, int n, float gain);
void audio_mix_set_music(audio_music_fn fn);
void audio_mix_music_gain(float gain);

#endif
