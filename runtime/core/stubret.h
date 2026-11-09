/* stubret.h -- what an import with no implementation and no module export returns.
 *
 * 0 -- or, for the device/network families the host doesn't emulate (NP, networking, HTTP/SSL, voice chat, Near,
 * RUDP), that family's "not initialised" error: a success that did nothing left games polling forever (Uncharted's
 * MP4 intro), an error lets them take their offline path. zero (RECOMP_STUB_ZERO=1): 0 for all.
 *
 * Two kinds of call still return 0 in those families:
 *  - set-up and tear-down: Shovel Knight asserts on sceNpInit's result ("if(!(ret == 0))") and stops at boot; it is
 *    the operational calls (state queries, requests, matching, scores) whose fake success left games waiting;
 *  - a common dialog's GetStatus: a dialog the host never opens is SCE_COMMON_DIALOG_STATUS_NONE (0). The family
 *    error read as "a dialog is still running" to games that poll every dialog's status before opening their own
 *    (Soul Sacrifice Delta polls the NP message/profile dialogs and never opened its save-data dialog).
 */
#ifndef STUBRET_H
#define STUBRET_H
#include <stdint.h>
#include <string.h>

static inline int stubret_ends(const char *name, size_t nl, const char *tail) {
	size_t tl = strlen(tail);
	return nl >= tl && !strcmp(name + nl - tl, tail);
}

static inline uint32_t stub_family_result(const char *name, int zero) {
	static const struct { const char *prefix; uint32_t err; } stub_errs[] = {
		{"sceNetCtl", 0x80412101u}, {"sceNet", 0x80410101u}, {"sceNp", 0x80550002u}, {"sceHttp", 0x80431001u}, {"sceSsl", 0x80435001u}, {"sceVoice", 0x80451001u}, {"SceNearUtil", 0x80820001u},
		{"sceRudp", 0x80770001u},   /* SCE_RUDP_ERROR_NOT_INITIALIZED: the reliable-UDP transport of online play, which never connects */
		{0, 0} };
	if (zero) return 0;
	size_t nl = strlen(name);
	int setup = stubret_ends(name, nl, "Init") || stubret_ends(name, nl, "Initialize") || stubret_ends(name, nl, "Term")
	         || stubret_ends(name, nl, "Terminate") || stubret_ends(name, nl, "Shutdown") || stubret_ends(name, nl, "Finalize")
	         || strstr(name, "Callback") || strstr(name, "RegisterHandler") || strstr(name, "InitWithParam") || strstr(name, "SetParam") || strstr(name, "Create") || strstr(name, "Delete") || strstr(name, "Destroy") || strstr(name, "Abort");
	if (setup || stubret_ends(name, nl, "DialogGetStatus")) return 0;
	for (int i = 0; stub_errs[i].prefix; i++) if (!strncmp(name, stub_errs[i].prefix, strlen(stub_errs[i].prefix))) return stub_errs[i].err;
	return 0;
}
/* (AUTO-01 LBP-8) the microseconds an unimplemented import waits before it returns: a wait call whose console version
 * blocks until its timeout when nothing is ready must not return at once -- the loop around it spins. Offline,
 * sceNetEpollWait(eid, events, max, timeout_us) waits out r3 (an infinite or longer one: 1 s, then the family error);
 * LittleBigPlanet's 'game host thread' (prio 84) called it 10000s of times in seconds and no frame was presented after.
 * 0 for everything else, and with RECOMP_STUB_ZERO */
#define STUB_WAIT_MAX_US 1000000u
static inline uint32_t stub_wait_us(const char *name, uint32_t a3, int zero) {
	if (zero || strcmp(name, "sceNetEpollWait")) return 0;
	return (int32_t)a3 < 0 || a3 > STUB_WAIT_MAX_US ? STUB_WAIT_MAX_US : a3;
}
#endif
