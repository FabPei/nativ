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


/* FEAT-ALL-01 (local only): P4D's eight places that build the native screen aspect 960/544 (0x3fe1e1e2, movw/movt) --
 * camera/projection setups (three pass it to fn_81129cca, two to fn_8100177a): IHOOKS on each movt, widened for wider
 * aspect ratios by the shared widen_aspect_reg() (RECOMP_ASPECT_HOOK=1; other values untouched). One name per site
 * (1..8 in pack.toml's order) so a site can be left native: RECOMP_P4D_ASPECT_SKIP="3,5" (diagnostic). */
static void p4d_aspect_site(Cpu *c, int reg, int site) {
	static int mask = -1;
	if (mask < 0) { mask = 0; const char *e = getenv("RECOMP_P4D_ASPECT_SKIP"); for (; e && *e; e++) if (*e >= '1' && *e <= '9') mask |= 1 << (*e - '0'); }
	static int seen; if (!(seen & (1 << site))) { seen |= 1 << site; fprintf(stderr, "p4d_aspect: site %d first run, value %#x, %s\n", site, c->r[reg & 15], mask & (1 << site) ? "skipped" : "widened"); }
	if (!(mask & (1 << site))) widen_aspect_reg(c, reg);
}
void recomp_ihook_p4d_aspect1(Cpu *c, int reg) { p4d_aspect_site(c, reg, 1); }
void recomp_ihook_p4d_aspect2(Cpu *c, int reg) { p4d_aspect_site(c, reg, 2); }
void recomp_ihook_p4d_aspect3(Cpu *c, int reg) { p4d_aspect_site(c, reg, 3); }
void recomp_ihook_p4d_aspect4(Cpu *c, int reg) { p4d_aspect_site(c, reg, 4); }
void recomp_ihook_p4d_aspect5(Cpu *c, int reg) { p4d_aspect_site(c, reg, 5); }
void recomp_ihook_p4d_aspect6(Cpu *c, int reg) { p4d_aspect_site(c, reg, 6); }
void recomp_ihook_p4d_aspect7(Cpu *c, int reg) { p4d_aspect_site(c, reg, 7); }
void recomp_ihook_p4d_aspect8(Cpu *c, int reg) { p4d_aspect_site(c, reg, 8); }
