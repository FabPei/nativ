/* netinfo_test.c -- the device's MAC address offline (runtime/net/netinfo.c, see its header comment).
 *
 * What a game gets for an import is the HLE table's function, else the unimplemented-import result
 * (runtime/core/stubret.h). Fail-before: -DNETINFO_BEFORE leaves the table empty (no netinfo.c), as before:
 * sceNetGetMacAddress / sceNetAdhocctlGetEtherAddr answer the networking family's 0x80410101 and write nothing.
 *
 * Run (WSL, from this folder):
 *   cc -O1 -g -Wall -I ../core -I ../net -I ../../recompiler netinfo_test.c -o /tmp/netinfo_test && /tmp/netinfo_test
 *   cc -O1 -g -Wall -I ../core -I ../net -I ../../recompiler -DNETINFO_BEFORE netinfo_test.c -o /tmp/netinfo_before && /tmp/netinfo_before
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hle.h"
#include "stubret.h"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo = 0, recomp_watch_hi = 0;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }

#ifdef NETINFO_BEFORE
static const HleEntry hle_netinfo_t[] = {{0, 0}};
#include "netinfo.h"
#else
#include "../net/netinfo.c"
#define hle_netinfo_t hle_netinfo
#endif

static uint32_t call(const char *name, uint32_t a0, uint32_t a1) {   /* the import as a game calls it */
	Cpu c; memset(&c, 0, sizeof c); c.r[0] = a0; c.r[1] = a1;
	for (const HleEntry *e = hle_netinfo_t; e->name; e++) if (!strcmp(e->name, name)) { e->fn(&c); return c.r[0]; }
	return stub_family_result(name, 0);
}

static int fails;
static void check(const char *what, int ok) { printf("%-70s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

int main(void) {
	recomp_mem = calloc(1, 0x10000);
	const uint8_t want[6] = {0x02, 0, 0, 0, 0, 0x01};
	unsetenv("RECOMP_NET_MAC");
	memset(recomp_mem + 0x1000, 0xee, 8);
	check("sceNetGetMacAddress(addr, 0) -> 0", call("sceNetGetMacAddress", 0x1000, 0) == 0);
	check("  writes 02:00:00:00:00:01 (locally administered, unicast)", !memcmp(recomp_mem + 0x1000, want, 6));
	check("  and nothing past the 6 bytes", recomp_mem[0x1006] == 0xee);
	memset(recomp_mem + 0x2000, 0xee, 8);
	check("sceNetAdhocctlGetEtherAddr(addr) -> 0", call("sceNetAdhocctlGetEtherAddr", 0x2000, 0) == 0);
	check("  the same address", !memcmp(recomp_mem + 0x2000, want, 6));
	check("a NULL address -> SCE_NET_ERROR_EINVAL", call("sceNetGetMacAddress", 0, 0) == NETINFO_EINVAL);
	setenv("RECOMP_NET_MAC", "0a:1B:2c:3D:4e:5F", 1);
	call("sceNetGetMacAddress", 0x3000, 0);
	const uint8_t set[6] = {0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f};
	check("RECOMP_NET_MAC sets another address", !memcmp(recomp_mem + 0x3000, set, 6));
	uint8_t m[6] = {9, 9, 9, 9, 9, 9};
	check("a malformed RECOMP_NET_MAC is not taken", !netinfo_parse_mac("0a:1b:2c:3d:4e", m) && !netinfo_parse_mac("0a:1b:2c:3d:4e:5fx", m) && m[0] == 9);
	/* unchanged: the rest of the networking family still answers "not initialised" offline */
	check("sceNetCtlInetGetInfo still the family error", call("sceNetCtlInetGetInfo", 0x1000, 0) == 0x80412101u);
	if (fails) { printf("%d checks FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
