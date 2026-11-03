/* DKU shim: KOS's sound effect manager on the port's mixer. */
#ifndef DKU_SHIM_DC_SOUND_SFXMGR_H
#define DKU_SHIM_DC_SOUND_SFXMGR_H

#include <stdint.h>
#include <dc/sound/sound.h>

typedef uint32_t sfxhnd_t;
#define SFXHND_INVALID 0

typedef struct {
    int chn;            /* -1: any free channel */
    sfxhnd_t idx;
    int vol;            /* 0..255 */
    int pan;            /* 0..255, 128 centre */
    int loop;
    int freq;           /* Hz */
    unsigned int loopstart, loopend;   /* samples */
} sfx_play_data_t;

/* buf holds little-endian PCM as on the Dreamcast; it is copied. */
sfxhnd_t snd_sfx_load_raw_buf(char *buf, size_t len, uint32_t rate, uint16_t bitsize, uint16_t channels);
void snd_sfx_unload(sfxhnd_t idx);
void snd_sfx_unload_all(void);
int snd_sfx_play(sfxhnd_t idx, int vol, int pan);
int snd_sfx_play_chn(int chn, sfxhnd_t idx, int vol, int pan);
int snd_sfx_play_ex(sfx_play_data_t *data);
void snd_sfx_stop(int chn);
void snd_sfx_stop_all(void);
int snd_sfx_chn_alloc(void);
void snd_sfx_chn_free(int chn);

#endif
