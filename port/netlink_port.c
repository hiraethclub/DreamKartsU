/* DKU: replaces upstream src/netlink.c. The console's network is up before
   the game starts, so there is no adapter or modem to bring up: the link
   only resolves the lobby server's name on a worker thread.

   The server comes from DK_NET_TEST_HOST at build time, else the first
   line of server.txt in the save folder, else 127.0.0.1. Until Aisling has
   agreed cross-play with Rich (PORT_PLAN.md M6), the live server is
   refused: any name containing "dcvmu" and upstream's fallback address. */
#include "netlink.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#include "plat.h"

#define DKU_BLOCKED_IP "143.110.224.215"   /* upstream DK_NET_FALLBACK_IP, dcvmu.com */

static volatile int stage = LINK_OFF;
static volatile int worker_running;
static kthread_t *worker;
static uint32_t server_ip;
static const char *failure = "";
static char status_buf[96];
static char host[128];

static void pick_host(void) {
#ifdef DK_NET_TEST_HOST
    snprintf(host, sizeof(host), "%s", DK_NET_STR(DK_NET_TEST_HOST));
#else
    snprintf(host, sizeof(host), "127.0.0.1");
    char path[512];
    snprintf(path, sizeof(path), "%sserver.txt", plat_save_dir());
    FILE *f = fopen(path, "r");
    if(f) {
        char line[128];
        if(fgets(line, sizeof(line), f)) {
            line[strcspn(line, " \t\r\n")] = 0;
            if(line[0]) snprintf(host, sizeof(host), "%s", line);
        }
        fclose(f);
    }
#endif
}

static bool blocked_name(const char *h) {
    for(const char *p = h; *p; p++)
        if(strncasecmp(p, "dcvmu", 5) == 0) return true;
    return false;
}

static void *link_worker(void *unused) {
    (void)unused;
    stage = LINK_RESOLVING;
    pick_host();
    server_ip = 0;
    if(blocked_name(host)) {
        failure = "LIVE SERVER DISABLED IN THIS BUILD";
        goto failed;
    }
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if(getaddrinfo(host, NULL, &hints, &res) == 0 && res) {
        server_ip = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
        freeaddrinfo(res);
    }
    if(!server_ip) {
        failure = "SERVER NOT FOUND";
        goto failed;
    }
    if(server_ip == inet_addr(DKU_BLOCKED_IP)) {
        server_ip = 0;
        failure = "LIVE SERVER DISABLED IN THIS BUILD";
        goto failed;
    }
    struct in_addr a = {.s_addr = server_ip};
    snprintf(status_buf, sizeof(status_buf), "SERVER %s", inet_ntoa(a));
    printf("DreamKartsU netlink: server %s (%s)\n", host, inet_ntoa(a));
    stage = LINK_UP;
    worker_running = 0;
    return NULL;

failed:
    printf("DreamKartsU netlink: %s (%s)\n", failure, host);
    stage = LINK_FAILED;
    worker_running = 0;
    return NULL;
}

static void start_worker(void) {
    if(worker) {
        thd_join(worker, NULL);
        worker = NULL;
    }
    stage = LINK_STARTING;
    worker_running = 1;
    worker = thd_create(0, link_worker, NULL);
    if(!worker) {
        failure = "NO WORKER THREAD";
        stage = LINK_FAILED;
        worker_running = 0;
    }
}

void link_prepare(void) {
    if(worker_running || stage != LINK_OFF) return;
    start_worker();
}

void link_start(void) {
    if(worker_running || stage == LINK_UP) return;
    start_worker();
}

int link_stage(void) { return stage; }

const char *link_status(void) {
    switch(stage) {
        case LINK_STARTING: return "STARTING";
        case LINK_RESOLVING: return "FINDING THE SERVER";
        case LINK_UP: return status_buf;
        case LINK_FAILED: return failure;
        default: return "";
    }
}

bool link_is_modem(void) { return false; }

uint32_t link_server_ip(void) { return server_ip; }

void link_shutdown(void) {
    if(worker) {
        thd_join(worker, NULL);
        worker = NULL;
    }
    stage = LINK_OFF;
}
