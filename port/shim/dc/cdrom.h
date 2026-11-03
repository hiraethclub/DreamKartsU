/* DKU shim: CD-DA music. The port has no disc: each "audio track" is a
   music file (port/shim/cdda_shim.c), and the TOC lists one per file
   found. With none, the drive reports no disc and audio.c stays silent. */
#ifndef DKU_SHIM_DC_CDROM_H
#define DKU_SHIM_DC_CDROM_H

#include <stdbool.h>
#include <stdint.h>

#define ERR_OK        0
#define ERR_NO_DISC   1
#define ERR_DISC_CHG  2
#define ERR_SYS       3

#define CD_STATUS_READ_FAIL -1
#define CD_STATUS_BUSY      0
#define CD_STATUS_PAUSED    1
#define CD_STATUS_STANDBY   2
#define CD_STATUS_PLAYING   3
#define CD_STATUS_SEEKING   4
#define CD_STATUS_SCANNING  5
#define CD_STATUS_OPEN      6
#define CD_STATUS_NO_DISC   7

#define CDDA_TRACKS  1
#define CDDA_SECTORS 2

typedef struct {
    uint32_t entry[99];
    uint32_t first, last;
    uint32_t leadout_sector;
} cd_toc_t;

#define TOC_LBA(n)   ((n) & 0x00ffffff)
#define TOC_ADR(n)   (((n) & 0x0f000000) >> 24)
#define TOC_CTRL(n)  (((n) & 0xf0000000) >> 28)
#define TOC_TRACK(n) (((n) & 0x00ff0000) >> 16)

int cdrom_reinit(void);
int cdrom_get_status(int *status, int *disc_type);
int cdrom_read_toc(cd_toc_t *toc_buffer, bool high_density);
int cdrom_cdda_play(uint32_t start, uint32_t end, uint32_t loops, int mode);
int cdrom_cdda_pause(void);
int cdrom_cdda_resume(void);

#endif
