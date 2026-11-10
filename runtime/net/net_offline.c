/*
 * net_offline.c: SceNet for a game run offline (net_online() off, the default) -- what a Vita without a network
 * connection answers. No host socket is ever opened here: nothing a game does offline can reach the network.
 *
 *   sockets   sceNetSocket/Bind/Connect/Listen/Accept/Send(to)/Recv(from)/Setsockopt/Getsockopt/Getsockname/SocketClose
 *             over an in-memory loopback. A console without a connection still has its loopback interface: sockets are
 *             created and bound (to INADDR_ANY or 127.x; any other local address is EADDRNOTAVAIL), a datagram sent to
 *             127.x arrives at the socket bound to that port, a receive with nothing queued would block -- EAGAIN at
 *             once on a non-blocking socket (SO_NBIO or MSG_DONTWAIT), after SO_RCVTIMEO otherwise, or it waits.
 *             Anything addressed beyond the machine fails as the console's stack fails it without a route:
 *             ENETUNREACH (51); a TCP connect to loopback finds no listener: ECONNREFUSED (61).
 *   epoll     sceNetEpoll* over the same sockets: a socket with a datagram queued is readable, a datagram socket writable;
 *             a wait without events waits its timeout (microseconds; negative: until an event).
 *   resolver  no DNS without a network: RESOLVER_ENOHOST (0x804101E6), as net.c answers an unknown name.
 *   helpers   sceNetHtons/Htonl/Htonll and their inverses, sceNetInetPton/Ntop: pure, as online.
 *
 * Results follow net.c: 0x80410100 | the BSD errno number, the number also in the thread's sceNetErrnoLoc word.
 * Before: offline every one of these was the generic stub -- 0x80410101 for sockets and byte swaps alike -- and one title's
 * loading screen waited for its local network host for ever.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hle.h"

enum { NO_MAXSOCK = 128, NO_QUEUE = 64, NO_MAXDGRAM = 65535, NO_MAXEPOLL = 32, NO_EPOLL_IDS = 64, NO_EPHEMERAL = 49152 };
enum { E_BADF = 9, E_AGAIN = 35, E_DESTADDRREQ = 39, E_PROTONOSUPPORT = 43, E_OPNOTSUPP = 45, E_AFNOSUPPORT = 47, E_ADDRINUSE = 48,
       E_ADDRNOTAVAIL = 49, E_NETUNREACH = 51, E_ENOBUFS = 55, E_NOTCONN = 57, E_CONNREFUSED = 61, E_INVAL = 22, E_MFILE = 24 };
typedef struct { uint8_t *data; uint32_t len, from_ip; uint16_t from_port; } NoDgram;   /* (addresses in network order) */
static struct {
	int used, type, nbio, bound, listening;
	uint32_t ip; uint16_t port;                 /* bound: ip (network order: 0 or 127.x), port (host order) */
	uint32_t peer_ip; uint16_t peer_port;       /* a connected datagram socket's peer (0: none) */
	uint32_t rcvtimeo_us;
	NoDgram q[NO_QUEUE]; int qhead, qn;
} nos[NO_MAXSOCK];
static struct { int used, n; struct { uint32_t id, events; uint8_t data[8]; } e[NO_EPOLL_IDS]; } noep[NO_MAXEPOLL];
static pthread_mutex_t no_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t no_cond = PTHREAD_COND_INITIALIZER;   /* a datagram arrived, a socket closed */

static __thread uint32_t no_errno_word;
static uint32_t no_errno_addr(void) { if (!no_errno_word) { no_errno_word = guest_alloc(4, 4); WR32(no_errno_word, 0); } return no_errno_word; }
static uint32_t no_fail(int e) { WR32(no_errno_addr(), (uint32_t)e); return 0x80410100u | (uint32_t)e; }
static int no_valid(uint32_t s) { return s > 0 && s < NO_MAXSOCK && nos[s].used; }
static int is_loopback(uint32_t ip_n) { return (((const uint8_t *)&ip_n)[0]) == 127; }
static int is_dgram(int type) { return type == 2 || type == 6 || type == 3; }
static uint16_t rd_port(uint32_t ga) { const uint8_t *p = G(ga + 2); return (uint16_t)(p[0] << 8 | p[1]); }   /* sin_port, network order */
static uint32_t rd_ip(uint32_t ga) { uint32_t v; memcpy(&v, G(ga + 4), 4); return v; }
static void wr_addr(uint32_t ga, uint32_t lenp, uint32_t ip_n, uint16_t port) {
	if (!ga) return;
	uint8_t b[16] = { 16, 2, (uint8_t)(port >> 8), (uint8_t)port }; memcpy(b + 4, &ip_n, 4); memcpy(G(ga), b, 16);
	if (lenp) WR32(lenp, 16);
}
static int port_taken(uint16_t port, int except) {   /* (no_lock) a datagram socket already bound to the port */
	for (int i = 1; i < NO_MAXSOCK; i++) if (i != except && nos[i].used && nos[i].bound && nos[i].port == port && is_dgram(nos[i].type) == is_dgram(nos[except].type)) return 1;
	return 0;
}
static int bind_ephemeral(int s) {   /* (no_lock) */
	for (uint32_t p = NO_EPHEMERAL; p < 65536; p++) if (!port_taken((uint16_t)p, s)) { nos[s].bound = 1; nos[s].ip = 0; nos[s].port = (uint16_t)p; return 0; }
	return -1;
}
static void drop_queue(int s) { while (nos[s].qn) { free(nos[s].q[nos[s].qhead].data); nos[s].qhead = (nos[s].qhead + 1) % NO_QUEUE; nos[s].qn--; } nos[s].qhead = 0; }
/* wait on no_cond (no_lock held) until `deadline_us` (0: no deadline); returns 0 at the deadline */
static uint64_t no_now_us(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u; }
static int no_wait(uint64_t deadline_us) {
	int timed_out = 0;
	const int b = sched_block_begin();
	if (!deadline_us) pthread_cond_wait(&no_cond, &no_lock);
	else {
		struct timespec ts = { (time_t)(deadline_us / 1000000u), (long)(deadline_us % 1000000u) * 1000 };
		timed_out = pthread_cond_timedwait(&no_cond, &no_lock, &ts) != 0 && no_now_us() >= deadline_us;
	}
	if (b) { pthread_mutex_unlock(&no_lock); sched_block_end(); pthread_mutex_lock(&no_lock); }
	return !timed_out;
}

static void hn_init(Cpu *c) { RET(0); }
static void hn_errno_loc(Cpu *c) { RET(no_errno_addr()); }
static void hn_socket(Cpu *c) {   /* (name, domain, type, protocol) */
	const int type = (int)A2;
	if (A1 != 2) { RET(no_fail(E_AFNOSUPPORT)); return; }
	if (!(type == 1 || type == 2 || type == 6 || type == 10 || (type == 3 && A3 == 1))) { RET(no_fail(E_PROTONOSUPPORT)); return; }
	pthread_mutex_lock(&no_lock);
	int s; for (s = 1; s < NO_MAXSOCK && nos[s].used; s++) {}
	if (s < NO_MAXSOCK) { memset(&nos[s], 0, sizeof nos[s]); nos[s].used = 1; nos[s].type = type; }
	pthread_mutex_unlock(&no_lock);
	RET(s < NO_MAXSOCK ? (uint32_t)s : no_fail(E_MFILE));
}
static void hn_close(Cpu *c) {
	pthread_mutex_lock(&no_lock);
	const int ok = no_valid(A0); if (ok) { drop_queue((int)A0); nos[A0].used = 0; pthread_cond_broadcast(&no_cond); }
	pthread_mutex_unlock(&no_lock);
	RET(ok ? 0 : no_fail(E_BADF));
}
static void hn_bind(Cpu *c) {   /* (s, addr, addrlen) */
	pthread_mutex_lock(&no_lock);
	uint32_t r = 0;
	if (!no_valid(A0)) r = no_fail(E_BADF);
	else if (!A1) r = no_fail(E_INVAL);
	else {
		const uint32_t ip = rd_ip(A1); const uint16_t port = rd_port(A1);
		if (ip && !is_loopback(ip)) r = no_fail(E_ADDRNOTAVAIL);   /* no interface but loopback */
		else if (port && port_taken(port, (int)A0)) r = no_fail(E_ADDRINUSE);
		else if (!port) { if (bind_ephemeral((int)A0)) r = no_fail(E_ADDRINUSE); else nos[A0].ip = ip; }
		else { nos[A0].bound = 1; nos[A0].ip = ip; nos[A0].port = port; }
	}
	pthread_mutex_unlock(&no_lock);
	RET(r);
}
static void hn_connect(Cpu *c) {   /* (s, addr, addrlen) */
	pthread_mutex_lock(&no_lock);
	uint32_t r = 0;
	if (!no_valid(A0)) r = no_fail(E_BADF);
	else if (!A1) r = no_fail(E_INVAL);
	else if (!is_loopback(rd_ip(A1))) r = no_fail(E_NETUNREACH);
	else if (!is_dgram(nos[A0].type)) r = no_fail(E_CONNREFUSED);   /* (no TCP listener on the loopback here) */
	else { nos[A0].peer_ip = rd_ip(A1); nos[A0].peer_port = rd_port(A1); if (!nos[A0].bound && bind_ephemeral((int)A0)) r = no_fail(E_ADDRINUSE); }
	pthread_mutex_unlock(&no_lock);
	RET(r);
}
static void hn_listen(Cpu *c) {
	pthread_mutex_lock(&no_lock);
	uint32_t r = !no_valid(A0) ? no_fail(E_BADF) : is_dgram(nos[A0].type) ? no_fail(E_OPNOTSUPP) : 0;
	if (!r) nos[A0].listening = 1;
	pthread_mutex_unlock(&no_lock);
	RET(r);
}
/* a socket's datagram send: to `ip`:`port` (network / host order) */
static uint32_t send_dgram(int s, uint32_t buf, uint32_t len, uint32_t ip, uint16_t port) {   /* (no_lock) */
	if (!is_loopback(ip)) return no_fail(E_NETUNREACH);
	if (len > NO_MAXDGRAM) return no_fail(40);   /* EMSGSIZE */
	if (!nos[s].bound && bind_ephemeral(s)) return no_fail(E_ADDRINUSE);
	int to = -1; for (int i = 1; i < NO_MAXSOCK && to < 0; i++) if (nos[i].used && nos[i].bound && is_dgram(nos[i].type) && nos[i].port == port && (!nos[i].ip || nos[i].ip == ip)) to = i;
	if (to > 0 && nos[to].qn < NO_QUEUE) {   /* (no receiver, or a full queue: the datagram is lost, as UDP's is) */
		uint8_t *d = malloc(len ? len : 1); if (!d) return no_fail(E_ENOBUFS);
		if (len) memcpy(d, G(buf), len);
		uint32_t lo = 0; ((uint8_t *)&lo)[0] = 127; ((uint8_t *)&lo)[3] = 1;
		NoDgram *g = &nos[to].q[(nos[to].qhead + nos[to].qn) % NO_QUEUE]; g->data = d; g->len = len; g->from_ip = nos[s].ip ? nos[s].ip : lo; g->from_port = nos[s].port;
		nos[to].qn++; pthread_cond_broadcast(&no_cond);
	}
	return len;
}
static void hn_sendto(Cpu *c) {   /* (s, buf, len, flags, to, tolen) */
	const uint32_t to = STK(0);
	pthread_mutex_lock(&no_lock);
	uint32_t r;
	if (!no_valid(A0)) r = no_fail(E_BADF);
	else if (!is_dgram(nos[A0].type)) r = no_fail(E_NOTCONN);
	else if (to) r = send_dgram((int)A0, A1, A2, rd_ip(to), rd_port(to));
	else if (nos[A0].peer_port) r = send_dgram((int)A0, A1, A2, nos[A0].peer_ip, nos[A0].peer_port);
	else r = no_fail(E_DESTADDRREQ);
	pthread_mutex_unlock(&no_lock);
	RET(r);
}
static void hn_send(Cpu *c) {   /* (s, buf, len, flags) */
	pthread_mutex_lock(&no_lock);
	uint32_t r;
	if (!no_valid(A0)) r = no_fail(E_BADF);
	else if (!is_dgram(nos[A0].type) || !nos[A0].peer_port) r = no_fail(E_NOTCONN);
	else r = send_dgram((int)A0, A1, A2, nos[A0].peer_ip, nos[A0].peer_port);
	pthread_mutex_unlock(&no_lock);
	RET(r);
}
/* a receive: the oldest queued datagram, or EAGAIN at once (non-blocking), after SO_RCVTIMEO, or a wait */
static uint32_t recv_dgram(uint32_t s, uint32_t buf, uint32_t len, uint32_t flags, uint32_t from, uint32_t fromlen) {   /* (no_lock) */
	if (!no_valid(s)) return no_fail(E_BADF);
	if (!is_dgram(nos[s].type) && !nos[s].listening) return no_fail(E_NOTCONN);
	const int nonblock = nos[s].nbio || (flags & 0x80u);   /* MSG_DONTWAIT */
	const uint64_t deadline = nos[s].rcvtimeo_us ? no_now_us() + nos[s].rcvtimeo_us : 0;
	while (no_valid(s) && !nos[s].qn) {
		if (nonblock) return no_fail(E_AGAIN);
		if (!no_wait(deadline)) return no_fail(E_AGAIN);   /* SO_RCVTIMEO ran out */
	}
	if (!no_valid(s)) return no_fail(E_BADF);
	NoDgram *g = &nos[s].q[nos[s].qhead];
	const uint32_t n = g->len < len ? g->len : len;
	if (n) memcpy(G(buf), g->data, n);
	wr_addr(from, fromlen, g->from_ip, g->from_port);
	free(g->data); nos[s].qhead = (nos[s].qhead + 1) % NO_QUEUE; nos[s].qn--;
	return n;
}
static void hn_recvfrom(Cpu *c) { const uint32_t from = STK(0), fromlen = STK(1); pthread_mutex_lock(&no_lock); uint32_t r = recv_dgram(A0, A1, A2, A3, from, fromlen); pthread_mutex_unlock(&no_lock); RET(r); }
static void hn_recv(Cpu *c) { pthread_mutex_lock(&no_lock); uint32_t r = recv_dgram(A0, A1, A2, A3, 0, 0); pthread_mutex_unlock(&no_lock); RET(r); }
static void hn_accept(Cpu *c) {   /* (s, addr, addrlen): no connection ever arrives */
	pthread_mutex_lock(&no_lock);
	uint32_t r;
	if (!no_valid(A0)) r = no_fail(E_BADF);
	else if (!nos[A0].listening) r = no_fail(E_INVAL);
	else {
		const uint64_t deadline = nos[A0].rcvtimeo_us ? no_now_us() + nos[A0].rcvtimeo_us : 0;
		r = 0;
		while (!r) {
			if (nos[A0].nbio) r = no_fail(E_AGAIN);
			else if (!no_wait(deadline)) r = no_fail(E_AGAIN);
			else if (!no_valid(A0)) r = no_fail(E_BADF);
		}
	}
	pthread_mutex_unlock(&no_lock);
	RET(r);
}
static void hn_setsockopt(Cpu *c) {   /* (s, level, optname, optval, optlen): SO_NBIO and SO_RCVTIMEO kept, the rest accepted */
	pthread_mutex_lock(&no_lock);
	uint32_t r = 0;
	if (!no_valid(A0)) r = no_fail(E_BADF);
	else if (A1 == 0xffff && A2 == 0x1100) nos[A0].nbio = A3 ? RD32(A3) != 0 : 0;
	else if (A1 == 0xffff && A2 == 0x1006) nos[A0].rcvtimeo_us = A3 ? RD32(A3) : 0;
	pthread_mutex_unlock(&no_lock);
	RET(r);
}
static void hn_getsockopt(Cpu *c) {   /* (s, level, optname, optval, optlen) */
	const uint32_t lenp = STK(0);
	pthread_mutex_lock(&no_lock);
	uint32_t r = 0, v = 0;
	if (!no_valid(A0)) r = no_fail(E_BADF);
	else if (A1 == 0xffff && A2 == 0x1008) v = (uint32_t)nos[A0].type;
	else if (A1 == 0xffff && A2 == 0x1100) v = (uint32_t)nos[A0].nbio;
	else if (A1 == 0xffff && A2 == 0x1006) v = nos[A0].rcvtimeo_us;
	pthread_mutex_unlock(&no_lock);
	if (!r && A3) WR32(A3, v);
	if (!r && lenp) WR32(lenp, 4);
	RET(r);
}
static void hn_getsockname(Cpu *c) {   /* (s, addr, addrlen) */
	pthread_mutex_lock(&no_lock);
	uint32_t r = no_valid(A0) ? 0 : no_fail(E_BADF);
	if (!r) wr_addr(A1, A2, nos[A0].ip, nos[A0].bound ? nos[A0].port : 0);
	pthread_mutex_unlock(&no_lock);
	RET(r);
}

/* ---- epoll */
static void hn_epoll_create(Cpu *c) {
	pthread_mutex_lock(&no_lock);
	int id; for (id = 1; id < NO_MAXEPOLL && noep[id].used; id++) {}
	if (id < NO_MAXEPOLL) { noep[id].used = 1; noep[id].n = 0; }
	pthread_mutex_unlock(&no_lock);
	RET(id < NO_MAXEPOLL ? (uint32_t)id : no_fail(E_MFILE));
}
static void hn_epoll_destroy(Cpu *c) {
	pthread_mutex_lock(&no_lock);
	const int ok = A0 > 0 && A0 < NO_MAXEPOLL && noep[A0].used; if (ok) { noep[A0].used = 0; pthread_cond_broadcast(&no_cond); }
	pthread_mutex_unlock(&no_lock);
	RET(ok ? 0 : no_fail(E_BADF));
}
static void hn_epoll_control(Cpu *c) {   /* (eid, op, id, event): ADD 1, MOD 2, DEL 3 */
	pthread_mutex_lock(&no_lock);
	uint32_t r = 0;
	if (!(A0 > 0 && A0 < NO_MAXEPOLL && noep[A0].used)) r = no_fail(E_BADF);
	else {
		__typeof__(noep[0]) *ep = &noep[A0]; int k; for (k = 0; k < ep->n && ep->e[k].id != A2; k++) {}
		if (A1 == 1 || A1 == 2) {
			if (k == ep->n) { if (A1 == 2 || ep->n == NO_EPOLL_IDS) r = no_fail(A1 == 2 ? 2 : 12); else ep->n++; }
			if (!r) { ep->e[k].id = A2; ep->e[k].events = A3 ? RD32(A3) : 0; if (A3) memcpy(ep->e[k].data, G(A3 + 24), 8); else memset(ep->e[k].data, 0, 8); }
		} else if (A1 == 3) { if (k < ep->n) ep->e[k] = ep->e[--ep->n]; else r = no_fail(2); }
		else r = no_fail(E_INVAL);
	}
	pthread_mutex_unlock(&no_lock);
	RET(r);
}
static void hn_epoll_wait(Cpu *c) {   /* (eid, events, maxevents, timeout us): SceNetEpollEvent 0x20 bytes {events, ..., data at +24} */
	pthread_mutex_lock(&no_lock);
	if (!(A0 > 0 && A0 < NO_MAXEPOLL && noep[A0].used)) { pthread_mutex_unlock(&no_lock); RET(no_fail(E_BADF)); return; }
	const uint64_t deadline = (int32_t)A3 < 0 ? 0 : no_now_us() + A3;
	int out = 0;
	for (;;) {
		out = 0;
		for (int k = 0; k < noep[A0].n && out < (int)A2; k++) {
			const uint32_t s = noep[A0].e[k].id, want = noep[A0].e[k].events; uint32_t ev = 0;
			if (!no_valid(s)) ev = 0x10;   /* HUP */
			else { if ((want & 1) && nos[s].qn) ev |= 1; if ((want & 2) && is_dgram(nos[s].type)) ev |= 2; }
			if (!ev) continue;
			const uint32_t at = A1 + 0x20u * (uint32_t)out; memset(G(at), 0, 0x20); WR32(at, ev); memcpy(G(at + 24), noep[A0].e[k].data, 8);
			out++;
		}
		if (out || !noep[A0].used) break;
		if (A3 == 0 || !no_wait(deadline)) break;   /* (a zero timeout polls once) */
	}
	pthread_mutex_unlock(&no_lock);
	RET((uint32_t)out);
}
static void hn_zero(Cpu *c) { RET(0); }

/* ---- resolver and pure helpers */
static void hn_resolver_create(Cpu *c) { static uint32_t next = 1; RET(__atomic_fetch_add(&next, 1, __ATOMIC_RELAXED)); }
static void hn_resolver_fail(Cpu *c) { RET(0x804101E6u); }   /* SCE_NET_ERROR_RESOLVER_ENOHOST: no DNS without a network */
static void hn_htons(Cpu *c) { const uint16_t v = (uint16_t)A0; RET((uint16_t)((v >> 8) | (v << 8))); }
static void hn_htonl(Cpu *c) { RET(__builtin_bswap32(A0)); }
static void hn_swap64(Cpu *c) { const uint32_t lo = c->r[0], hi = c->r[1]; c->r[0] = __builtin_bswap32(hi); c->r[1] = __builtin_bswap32(lo); }
static void hn_inet_pton(Cpu *c) {   /* (af, src, dst): 1 parsed, 0 not (IPv4 dotted quad) */
	unsigned a, b, d, e; char tail;
	if (A0 != 2 || !A1 || !A2 || sscanf((const char *)G(A1), "%u.%u.%u.%u%c", &a, &b, &d, &e, &tail) != 4 || a > 255 || b > 255 || d > 255 || e > 255) { RET(0); return; }
	uint8_t q[4] = { (uint8_t)a, (uint8_t)b, (uint8_t)d, (uint8_t)e }; memcpy(G(A2), q, 4); RET(1);
}
static void hn_inet_ntop(Cpu *c) {   /* (af, src, dst, size): dst or 0 */
	if (A0 != 2 || !A1 || !A2) { RET(0); return; }
	const uint8_t *q = G(A1); char b[16]; snprintf(b, sizeof b, "%u.%u.%u.%u", q[0], q[1], q[2], q[3]);
	if (strlen(b) + 1 > A3) { RET(0); return; }
	memcpy(G(A2), b, strlen(b) + 1); RET(A2);
}

const HleEntry hle_net_offline[] = {
	{"sceNetInit", hn_init}, {"sceNetTerm", hn_zero}, {"sceNetErrnoLoc", hn_errno_loc},
	{"sceNetSocket", hn_socket}, {"sceNetSocketClose", hn_close}, {"sceNetBind", hn_bind}, {"sceNetConnect", hn_connect},
	{"sceNetListen", hn_listen}, {"sceNetAccept", hn_accept}, {"sceNetSend", hn_send}, {"sceNetSendto", hn_sendto},
	{"sceNetRecv", hn_recv}, {"sceNetRecvfrom", hn_recvfrom}, {"sceNetSetsockopt", hn_setsockopt}, {"sceNetGetsockopt", hn_getsockopt},
	{"sceNetGetsockname", hn_getsockname},
	{"sceNetEpollCreate", hn_epoll_create}, {"sceNetEpollDestroy", hn_epoll_destroy}, {"sceNetEpollControl", hn_epoll_control},
	{"sceNetEpollWait", hn_epoll_wait}, {"sceNetEpollAbort", hn_zero},
	{"sceNetResolverCreate", hn_resolver_create}, {"sceNetResolverDestroy", hn_zero}, {"sceNetResolverAbort", hn_zero},
	{"sceNetResolverStartNtoa", hn_resolver_fail}, {"sceNetResolverStartAton", hn_resolver_fail},
	{"sceNetHtons", hn_htons}, {"sceNetNtohs", hn_htons}, {"sceNetHtonl", hn_htonl}, {"sceNetNtohl", hn_htonl},
	{"sceNetHtonll", hn_swap64}, {"sceNetNtohll", hn_swap64},
	{"sceNetInetPton", hn_inet_pton}, {"sceNetInetNtop", hn_inet_ntop},
	{0, 0},
};
