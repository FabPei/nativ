/*
 * Soul Sacrifice Delta (PCSF00532): its hooks, built into this game alone (runtime/build.py RECOMP_PACK_SOURCES).
 * packs/PCSF00532/pack.toml's IHOOKS= place them on the game's instructions. FEAT-ALL-01 (local branch only).
 */
#include "hooks.h"
#include <stdio.h>

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
void recomp_ihook_ssd_aspect(Cpu *c, int reg) { widen_aspect_reg(c, reg); }
