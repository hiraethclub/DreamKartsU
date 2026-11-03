/* DKU shim: the CD drive. Music files are an M4 task (PORT_PLAN.md); until
   then there is no disc, which audio.c handles as silent music. */
#include <dc/cdrom.h>
#include <string.h>

int cdrom_reinit(void) { return ERR_OK; }

int cdrom_get_status(int *status, int *disc_type) {
    if(status) *status = CD_STATUS_NO_DISC;
    if(disc_type) *disc_type = 0;
    return 0;
}

int cdrom_read_toc(cd_toc_t *toc_buffer, bool high_density) {
    (void)high_density;
    memset(toc_buffer, 0, sizeof(*toc_buffer));
    return ERR_NO_DISC;
}

int cdrom_cdda_play(uint32_t start, uint32_t end, uint32_t loops, int mode) {
    (void)start; (void)end; (void)loops; (void)mode;
    return ERR_NO_DISC;
}

int cdrom_cdda_pause(void) { return ERR_OK; }
int cdrom_cdda_resume(void) { return ERR_OK; }
