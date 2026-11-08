/*
 * Soul Sacrifice Delta (PCSF00532): its hooks, built into this game alone (runtime/build.py RECOMP_PACK_SOURCES).
 * packs/PCSF00532/pack.toml's IHOOKS= place them on the game's instructions. FEAT-ALL-01 (local branch only).
 */
#include "hooks.h"
#include <stdio.h>
#include <stdlib.h>

/* Soul Sacrifice Delta (PCSF00532): its frame pacer fn_817c932c(obj) returns period - elapsed on the kernel timer (us)
 * and the main loop (0x8127e42a..0x8127e52c) sleeps that long before the next frame. IHOOKS on the `ldm.w r8, {sl, fp}`
 * at 0x817c938e, which loads the 64-bit period: a 1/30 s period (33000..33400 us) becomes 1/rate at 60/120 fps.
 * Other periods (other pacers, if any) stay as they are. FEAT-ALL-01, local only. */
void recomp_ihook_ssd_pace(Cpu *c, int reg) {
	(void)reg;
	static int logged;
	if (!logged) { logged = 1; fprintf(stderr, "ssd_pace: period %u:%u us, frame rate %d\n", c->r[11], c->r[10], display_frame_rate()); }
	if (c->r[11] != 0 || c->r[10] < 33000u || c->r[10] > 33400u || !display_frame_rate_60()) return;
	c->r[10] = c->r[10] * 30u / (uint32_t)display_frame_rate();
}

/* SSD's camera setups store the native screen aspect 960/544 (0x3fe1e1e2, movw/movt) beside a 10000 far plane:
 * fn_81058a36, fn_8162c6c6, fn_816d0784, fn_81759806, fn_817bf7ec, fn_817c1d1c, fn_812898a8. IHOOKS on each movt:
 * widened for wider aspect ratios by the shared widen_aspect_reg() (RECOMP_ASPECT_HOOK=1; other values untouched). */
/* one name per site (1..7 in the order above) so a site can be left native: RECOMP_SSD_ASPECT_SKIP="3,5" (diagnostic).
 * Boot to main menu runs site 3 alone (fn_816d0784); skipping it brings the menu's smoke panel to the right edge at 21:9
 * but also the whole scene back to native: the panel is world geometry in the same camera, authored for 960/544. */
static void ssd_aspect_site(Cpu *c, int reg, int site) {
	static int mask = -1;
	if (mask < 0) { mask = 0; const char *e = getenv("RECOMP_SSD_ASPECT_SKIP"); for (; e && *e; e++) if (*e >= '1' && *e <= '9') mask |= 1 << (*e - '0'); }
	static int seen; if (!(seen & (1 << site))) { seen |= 1 << site; fprintf(stderr, "ssd_aspect: site %d first run, value %#x, %s\n", site, c->r[reg & 15], mask & (1 << site) ? "skipped" : "widened"); }
	if (!(mask & (1 << site))) widen_aspect_reg(c, reg);
}
void recomp_ihook_ssd_aspect1(Cpu *c, int reg) { ssd_aspect_site(c, reg, 1); }
void recomp_ihook_ssd_aspect2(Cpu *c, int reg) { ssd_aspect_site(c, reg, 2); }
void recomp_ihook_ssd_aspect3(Cpu *c, int reg) { ssd_aspect_site(c, reg, 3); }
void recomp_ihook_ssd_aspect4(Cpu *c, int reg) { ssd_aspect_site(c, reg, 4); }
void recomp_ihook_ssd_aspect5(Cpu *c, int reg) { ssd_aspect_site(c, reg, 5); }
void recomp_ihook_ssd_aspect6(Cpu *c, int reg) { ssd_aspect_site(c, reg, 6); }
void recomp_ihook_ssd_aspect7(Cpu *c, int reg) { ssd_aspect_site(c, reg, 7); }
