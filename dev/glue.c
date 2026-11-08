/* Host side of a recompiled module for testing (dev/difftest.py): the guest arena, and
 * calls out of recompiled code handed to a host callback. */
#include "rt.h"

uint8_t *recomp_mem;
uint32_t recomp_watch_lo, recomp_watch_hi;   /* rt.h's write watch (WR32 & co.): off here (hi 0) */
void recomp_watch_write(uint32_t a, uint32_t v, int w) { (void)a; (void)v; (void)w; }
static void (*external_cb)(Cpu *, uint32_t);
uint32_t recomp_unimpl_at;
__thread uint32_t recomp_excl_addr, recomp_excl_val;
__thread int recomp_excl_valid;
__thread uint64_t recomp_excl_val64;   /* ldrexd/strexd */
__thread uint32_t recomp_tpidruro;      /* the guest thread's TLS pointer */

void recomp_set_mem(uint8_t *m) { recomp_mem = m; }
void recomp_set_external(void (*cb)(Cpu *, uint32_t)) { external_cb = cb; }
void recomp_call_external(Cpu *c, uint32_t addr) { if (external_cb) external_cb(c, addr); else c->r[0] = 0; }
void recomp_unimplemented(Cpu *c, uint32_t addr) { recomp_unimpl_at = addr; c->stop = 1; }
/* flush-to-zero and default-NaN, like the console's RunFast FPSCR (AArch64 FPCR.FZ | DN) */
void recomp_runfast(void) {
#if defined(__aarch64__)
	uint64_t f; __asm__ volatile("mrs %0, fpcr" : "=r"(f)); f |= (1u << 24) | (1u << 25);
	__asm__ volatile("msr fpcr, %0" :: "r"(f));
#elif defined(__x86_64__)
	/* MXCSR FTZ | DAZ (x86 has no default-NaN mode) */
	uint32_t m; __asm__ volatile("stmxcsr %0" : "=m"(m)); m |= 0x8040; __asm__ volatile("ldmxcsr %0" :: "m"(m));
#endif
}
/* a module's recompiled C (recomp.py --module) registers its own lookup from a constructor instead of defining
 * recomp_lookup: kept here, and recomp_lookup answers from them (weak, so a main program's own definition wins) */
static GuestFn (*mod_lookups[8])(uint32_t); static const char *(*mod_names[8])(uint32_t); static int (*mod_arm[8])(uint32_t); static int n_mod_lookups;
void recomp_register_module(const char *name, GuestFn (*lookup)(uint32_t), const char *(*import_name)(uint32_t), int (*is_arm)(uint32_t)) {
	(void)name; if (n_mod_lookups < 8) { mod_lookups[n_mod_lookups] = lookup; mod_names[n_mod_lookups] = import_name; mod_arm[n_mod_lookups++] = is_arm; }
}
__attribute__((weak)) const char *recomp_import_name(uint32_t addr) {
	for (int i = 0; i < n_mod_lookups; i++) { const char *n = mod_names[i] ? mod_names[i](addr) : 0; if (n) return n; }
	return 0;
}
__attribute__((weak)) int recomp_is_arm(uint32_t addr) {
	for (int i = 0; i < n_mod_lookups; i++) if (mod_lookups[i](addr)) return mod_arm[i] ? mod_arm[i](addr) : 0;
	return 0;
}
__attribute__((weak)) GuestFn recomp_lookup(uint32_t addr) {
	for (int i = 0; i < n_mod_lookups; i++) { GuestFn f = mod_lookups[i](addr); if (f) return f; }
	return 0;
}
void recomp_run(Cpu *c, uint32_t entry) {
	GuestFn f = recomp_lookup(entry & ~1u);
	if (f) f(c); else recomp_unimplemented(c, entry);
}

/* lockstep trace: (addr, r0..r14, nzcv, d0..d31) per instruction, read back by difftest */
#define TRACE_MAX 100000
uint32_t recomp_trace_buf[TRACE_MAX][81];
uint32_t recomp_trace_n;
void recomp_trace(Cpu *c, uint32_t addr) {
	if (recomp_trace_n >= TRACE_MAX) return;
	uint32_t *t = recomp_trace_buf[recomp_trace_n++];
	t[0] = addr; memcpy(t + 1, c->r, 15 * 4);
	t[16] = c->n << 3 | c->z << 2 | c->c << 1 | c->v;
	memcpy(t + 17, c->vfp.s, 64 * 4);
}
