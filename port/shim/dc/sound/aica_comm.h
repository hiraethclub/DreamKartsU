/* DKU shim: the AICA channel command packet audio.c builds to change a
   playing channel's pitch, volume or pan in place. snd_sh4_to_aica()
   applies it to the port's mixer. */
#ifndef DKU_SHIM_DC_SOUND_AICA_COMM_H
#define DKU_SHIM_DC_SOUND_AICA_COMM_H

#include <stdint.h>
#include <dc/sound/sfxmgr.h>

typedef struct {
    uint32_t size;      /* in 32-bit words */
    uint32_t cmd;
    uint32_t timestamp;
    uint32_t cmd_id;    /* the channel */
    uint32_t misc[4];
    uint8_t cmd_data[];
} aica_cmd_t;

typedef struct {
    uint32_t cmd;
    uint32_t base;
    uint32_t type;
    uint32_t length;
    uint32_t loop;
    uint32_t loopstart;
    uint32_t loopend;
    uint32_t freq;
    uint32_t vol;
    uint32_t pan;
    uint32_t pos;
    uint32_t pad[5];
} aica_channel_t;

#define AICA_CMD_NONE 0
#define AICA_CMD_PING 1
#define AICA_CMD_CHAN 2

#define AICA_CH_CMD_NONE   0x00000000
#define AICA_CH_CMD_START  0x00000001
#define AICA_CH_CMD_STOP   0x00000002
#define AICA_CH_CMD_UPDATE 0x00000003
#define AICA_CH_CMD_MASK   0x0000000f
#define AICA_CH_UPDATE_SET_FREQ 0x00000100
#define AICA_CH_UPDATE_SET_VOL  0x00000200
#define AICA_CH_UPDATE_SET_PAN  0x00000400

#define AICA_CMDSTR_CHANNEL(T, CMDR, CHANR) \
    uint32_t T[(sizeof(aica_cmd_t) + sizeof(aica_channel_t)) / 4]; \
    aica_cmd_t *CMDR = (aica_cmd_t *)T; \
    aica_channel_t *CHANR = (aica_channel_t *)(CMDR->cmd_data)
#define AICA_CMDSTR_CHANNEL_SIZE ((sizeof(aica_cmd_t) + sizeof(aica_channel_t)) / 4)

#endif
