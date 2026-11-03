/* DKU shim: the network interface net.c mixes into its session token.
   The platform fills in a MAC and address where it knows them. */
#ifndef DKU_SHIM_KOS_NET_H
#define DKU_SHIM_KOS_NET_H

#include <stdint.h>

typedef struct netif {
    uint8_t mac_addr[6];
    uint8_t ip_addr[4];
} netif_t;

extern netif_t *net_default_dev;

#endif
