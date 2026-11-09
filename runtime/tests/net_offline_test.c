/* net_offline_test.c -- SceNet for a game run offline (net/net_offline.c): what a Vita without a network connection answers.
 *
 * The contract: sockets are created and bound (INADDR_ANY or 127.x; another local address EADDRNOTAVAIL; a port twice
 * EADDRINUSE); a datagram sent to 127.0.0.1:<port> arrives at the socket bound to it, from 127.0.0.1:<the sender's port>;
 * a receive with nothing queued is EAGAIN at once when non-blocking (SO_NBIO or MSG_DONTWAIT), EAGAIN after SO_RCVTIMEO
 * otherwise; anything addressed beyond the machine is ENETUNREACH (send and connect) and a TCP connect to loopback is
 * ECONNREFUSED; epoll reports a socket with a queued datagram readable and waits out its timeout when nothing is; the
 * resolver finds no host; the byte-order helpers swap. Results are 0x80410100 | errno, the errno also in sceNetErrnoLoc.
 *
 * Before: offline every SceNet import was the generic stub (0x80410101, sceNetHtons included) -- fail-before: hle_net_offline
 * does not exist (does not compile). No host socket is ever opened (the file opens none; the test runs without a network).
 *
 * Run (WSL, from this folder):
 *   cc -O1 -g -Wall -D_GNU_SOURCE -I ../core -I ../net -I ../platform -I ../../recompiler -I ../.. net_offline_test.c -o /tmp/net_offline_test -lpthread && /tmp/net_offline_test
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include "hle.h"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo = 0, recomp_watch_hi = 0;
void recomp_watch_write(uint32_t a, uint32_t v, int width) { (void)a; (void)v; (void)width; }
uint32_t guest_alloc(uint32_t size, uint32_t align) { (void)align; static uint32_t heap = 0x81000000; uint32_t a = heap; heap += (size + 15) & ~15u; return a; }
int sched_block_begin(void) { return 0; }
void sched_block_end(void) {}

#include "../net/net_offline.c"

static int fails, checks;
static void check(const char *what, int ok) { checks++; printf("%-74s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
static HleFn find(const char *n) { for (const HleEntry *e = hle_net_offline; e->name; e++) if (!strcmp(e->name, n)) return e->fn; return 0; }
static Cpu cpu;
#define SP 0x10000
/* a call with up to 4 register and 2 stack arguments */
static uint32_t call6(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t s0, uint32_t s1) {
	HleFn f = find(name); if (!f) { printf("  missing %s\n", name); return 0xdeadu; }
	memset(&cpu, 0, sizeof cpu); cpu.r[0] = a0; cpu.r[1] = a1; cpu.r[2] = a2; cpu.r[3] = a3; cpu.r[13] = SP;
	WR32(SP, s0); WR32(SP + 4, s1);
	f(&cpu); return cpu.r[0];
}
#define call(n, a0, a1, a2, a3) call6(n, a0, a1, a2, a3, 0, 0)
static uint64_t ms_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000; }
enum { ADDR = 0x20000, ADDR2 = 0x20100, BUF = 0x21000, OUT = 0x22000, FROM = 0x23000, FROMLEN = 0x23100, OPT = 0x23200, EV = 0x24000, EVD = 0x24100 };
static void sockaddr(uint32_t at, const char *ip, uint16_t port) {   /* SceNetSockaddrIn {len 16, family 2, port (net), addr, vport, zero} */
	memset(G(at), 0, 16); WR8(at, 16); WR8(at + 1, 2); WR8(at + 2, port >> 8); WR8(at + 3, port & 0xff);
	unsigned a, b, c, d; sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d); WR8(at + 4, a); WR8(at + 5, b); WR8(at + 6, c); WR8(at + 7, d);
}

int main(void) {
	recomp_mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (recomp_mem == MAP_FAILED) { perror("mmap"); return 2; }
	const uint32_t AGAIN = 0x80410123u, NETUNREACH = 0x80410133u;

	check("sceNetInit -> 0", call("sceNetInit", 0, 0, 0, 0) == 0);
	const uint32_t a = call("sceNetSocket", 0, 2, 2, 0), b = call("sceNetSocket", 0, 2, 2, 0);
	check("two UDP sockets (AF_INET, DGRAM) -> ids", a > 0 && a < 0x80000000u && b > 0 && b < 0x80000000u && a != b);
	sockaddr(ADDR, "0.0.0.0", 5000);
	check("bind a to INADDR_ANY:5000 -> 0", call("sceNetBind", a, ADDR, 16, 0) == 0);
	check("bind b to the same port -> EADDRINUSE (0x80410130)", call("sceNetBind", b, ADDR, 16, 0) == 0x80410130u);
	sockaddr(ADDR2, "192.168.1.20", 5001);
	check("bind b to a LAN address -> EADDRNOTAVAIL (0x80410131): only loopback", call("sceNetBind", b, ADDR2, 16, 0) == 0x80410131u);
	check("  and sceNetErrnoLoc holds 49", RD32(call("sceNetErrnoLoc", 0, 0, 0, 0)) == 49);

	WR32(OPT, 1); call6("sceNetSetsockopt", a, 0xffff, 0x1100, OPT, 4, 0);   /* SO_NBIO */
	check("non-blocking receive, nothing queued -> EAGAIN (0x80410123)", call6("sceNetRecvfrom", a, OUT, 64, 0, FROM, FROMLEN) == AGAIN);
	check("  MSG_DONTWAIT on a blocking socket -> EAGAIN", call6("sceNetRecvfrom", b, OUT, 64, 0x80, FROM, FROMLEN) == AGAIN);

	memcpy(G(BUF), "hello loopback", 14); sockaddr(ADDR2, "127.0.0.1", 5000);
	check("b sends 14 bytes to 127.0.0.1:5000 -> 14", call6("sceNetSendto", b, BUF, 14, 0, ADDR2, 16) == 14);
	memset(G(OUT), 0, 64);
	const uint32_t got = call6("sceNetRecvfrom", a, OUT, 64, 0, FROM, FROMLEN);
	check("a receives it (14 bytes, the same data)", got == 14 && !memcmp(G(OUT), "hello loopback", 14));
	call("sceNetGetsockname", b, ADDR, FROMLEN, 0);
	check("  from 127.0.0.1:<b's port> (b bound itself to an ephemeral port)", RD8(FROM + 4) == 127 && RD8(FROM + 7) == 1 && !memcmp(G(FROM + 2), G(ADDR + 2), 2) && (RD8(ADDR + 2) << 8 | RD8(ADDR + 3)) >= 49152);

	sockaddr(ADDR2, "8.8.8.8", 53);
	check("send to 8.8.8.8:53 -> ENETUNREACH (0x80410133): nothing leaves the machine", call6("sceNetSendto", b, BUF, 14, 0, ADDR2, 16) == NETUNREACH);
	const uint32_t t = call("sceNetSocket", 0, 2, 1, 0);
	check("TCP connect to 93.184.216.34:80 -> ENETUNREACH", ({ sockaddr(ADDR2, "93.184.216.34", 80); call("sceNetConnect", t, ADDR2, 16, 0); }) == NETUNREACH);
	check("TCP connect to 127.0.0.1:80 -> ECONNREFUSED (0x8041013d)", ({ sockaddr(ADDR2, "127.0.0.1", 80); call("sceNetConnect", t, ADDR2, 16, 0); }) == 0x8041013du);

	WR32(OPT, 50000); call6("sceNetSetsockopt", b, 0xffff, 0x1006, OPT, 4, 0);   /* SO_RCVTIMEO 50 ms */
	uint64_t t0 = ms_now(); uint32_t r = call6("sceNetRecvfrom", b, OUT, 64, 0, FROM, FROMLEN); uint64_t dt = ms_now() - t0;
	char w[160]; snprintf(w, sizeof w, "blocking receive with SO_RCVTIMEO 50 ms -> EAGAIN after it (%llu ms)", (unsigned long long)dt);
	check(w, r == AGAIN && dt >= 40 && dt < 1000);

	const uint32_t ep = call("sceNetEpollCreate", 0, 0, 0, 0);
	memset(G(EV), 0, 0x20); WR32(EV, 1); memcpy(G(EV + 24), "DATAWORD", 8);
	check("epoll create + add a (IN, data word) -> ids / 0", ep > 0 && ep < 0x80000000u && call("sceNetEpollControl", ep, 1, a, EV) == 0);
	t0 = ms_now(); r = call("sceNetEpollWait", ep, EVD, 4, 30000); dt = ms_now() - t0;
	snprintf(w, sizeof w, "epoll wait, nothing queued, 30 ms -> 0 events after the timeout (%llu ms)", (unsigned long long)dt);
	check(w, r == 0 && dt >= 25 && dt < 1000);
	sockaddr(ADDR2, "127.0.0.1", 5000); call6("sceNetSendto", b, BUF, 3, 0, ADDR2, 16);
	r = call("sceNetEpollWait", ep, EVD, 4, 30000);
	check("after a datagram for a: 1 event, IN, its data word", r == 1 && RD32(EVD) == 1 && !memcmp(G(EVD + 24), "DATAWORD", 8));

	check("resolver: no host without a network (0x804101E6)", call("sceNetResolverStartNtoa", call("sceNetResolverCreate", 0, 0, 0, 0), BUF, OUT, 0) == 0x804101E6u);
	check("sceNetHtons(0xcd05) -> 0x05cd (was the stub's 0x80410101)", call("sceNetHtons", 0xcd05, 0, 0, 0) == 0x05cdu);
	check("sceNetHtonl(0x7f000001) -> 0x0100007f", call("sceNetHtonl", 0x7f000001u, 0, 0, 0) == 0x0100007fu);
	check("sceNetSocketClose(a) -> 0, then EBADF (0x80410109)", call("sceNetSocketClose", a, 0, 0, 0) == 0 && call("sceNetSocketClose", a, 0, 0, 0) == 0x80410109u);

	printf("%d checks, %d failed\n", checks, fails);
	if (fails) { printf("FAILED\n"); return 1; }
	printf("all passed\n");
	return 0;
}
