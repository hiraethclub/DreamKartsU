/* DKU shim: sound system init (port/audio_mix.c). */
#ifndef DKU_SHIM_DC_SOUND_SOUND_H
#define DKU_SHIM_DC_SOUND_SOUND_H

#include <stddef.h>
#include <stdint.h>

int snd_init(void);
void snd_shutdown(void);
size_t snd_mem_available(void);
/* Batch channel updates: the mixer applies them together at start. */
void snd_sh4_to_aica_stop(void);
void snd_sh4_to_aica_start(void);
void snd_sh4_to_aica(void *packet, uint32_t size);

#endif
