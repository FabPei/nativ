/* netinfo.c -- the network facts a game reads offline: the device's own MAC address.
 *
 * Contract (the console's): sceNetGetMacAddress(SceNetEtherAddr *addr, int flags) and
 * sceNetAdhocctlGetEtherAddr(SceNetEtherAddr *addr) write the device's 6-byte Ethernet address and return 0 once
 * sceNetInit / sceNetAdhocctlInit have run; with no connection at all they still answer (the address is the
 * hardware's, not a link's). A NULL address is SCE_NET_ERROR_EINVAL (0x80410116).
 *
 * Before: both were unimplemented imports in the networking family, so they returned its "not initialised" error
 * (runtime/core/stubret.h, 0x80410101) right after the game's sceNetInit had succeeded; a game's network layer asked
 * one and then the other and stopped at its own assert when both failed (a module's bkpt, NIGHT-01 batch B).
 *
 * The address: fixed, locally administered and unicast (02:00:00:00:00:01; RECOMP_NET_MAC=aa:bb:cc:dd:ee:ff sets
 * another) -- never the host adapter's, which a game could send to its servers. Always on, online or offline: net.c
 * (online runs only) has neither call.
 */
#include <stdio.h>
#include <stdlib.h>
#include "hle.h"
#include "netinfo.h"

static void netinfo_mac(uint8_t mac[6]) {
	static const uint8_t def[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
	if (!netinfo_parse_mac(getenv("RECOMP_NET_MAC"), mac)) for (int i = 0; i < 6; i++) mac[i] = def[i];
}

static void h_get_mac(Cpu *c) {   /* (SceNetEtherAddr *addr, int flags) / (SceNetEtherAddr *addr) */
	if (!A0) { RET(NETINFO_EINVAL); return; }
	uint8_t mac[6];
	netinfo_mac(mac);
	for (int i = 0; i < 6; i++) WR8(A0 + i, mac[i]);
	RET(0);
}

const HleEntry hle_netinfo[] = {
	{"sceNetGetMacAddress", h_get_mac},
	{"sceNetAdhocctlGetEtherAddr", h_get_mac},
	{0, 0},
};
