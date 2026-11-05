/*
 * Persona 4: Dancing All Night (PCSE00764): its hooks (runtime/build.py RECOMP_PACK_HOOKS), placed by pack.toml's HOOKS=.
 */
#include "hooks.h"
#include <stdio.h>
#include <stdlib.h>

/* CRI's error notifier (CRIWARE's criErr_Notify: r0 the level, r1 an "E<id>:<message>" format, r2.. its arguments), and its
 * one-argument form. P4D_CRI_LOG=1 (a regenerate): each message printed once per caller -- CRI's middleware says why a
 * player, a voice or a file failed here and nowhere else */
static void cri_say(Cpu *c, int nargs) {
	static uint32_t seen[256]; static int nseen;
	uint32_t lr = c->r[14];
	for (int i = 0; i < nseen; i++) if (seen[i] == lr) return;
	if (nseen < 256) seen[nseen++] = lr;
	char msg[256]; uint32_t f = c->r[1]; int n = 0;
	for (; n < 255 && RD8(f + (uint32_t)n); n++) msg[n] = (char)RD8(f + (uint32_t)n);
	msg[n] = 0;
	if (nargs) fprintf(stderr, "    cri: level %u: %s  [arg %#x %d] (lr %#x)\n", c->r[0], msg, c->r[2], (int)c->r[2], lr);
	else fprintf(stderr, "    cri: level %u: %s (lr %#x)\n", c->r[0], msg, lr);
}
void recomp_hook_p4d_cri_err(Cpu *c) { cri_say(c, 0); }
void recomp_hook_p4d_cri_err1(Cpu *c) { cri_say(c, 1); }

