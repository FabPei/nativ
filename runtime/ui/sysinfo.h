/*
 * The "Copy system info" text (UI QUEUE U5): one "key: value" line per fact about the running game, for a player to
 * paste into a bug report. Every value is scrubbed of the user's home path -- written as ~, both slash forms, the
 * rule vitarecomp.py's cmd_report applies to a report's text files -- so the pasted text never carries the user
 * name. An empty value prints "-" (the overlay's missing-value convention). Header-only and free of SDL so
 * runtime/tests/sysinfo_test.c can check it.
 */
#ifndef RECOMP_SYSINFO_H
#define RECOMP_SYSINFO_H
#include <stdio.h>
#include <string.h>

typedef struct SysInfo {
	const char *versions;      /* the menu's version line: "Nativ <tools version> · <title id> <game version>" */
	const char *platform;      /* SDL_GetPlatform() */
	const char *sdl_version;   /* "3.4.0" */
	const char *cpu, *gpu;     /* MetricsSnapshot's cpu_name / gpu_name */
	int win_w, win_h;          /* the window's pixel size (window_pixel_size) */
	int render_scale;          /* the settings' render_scale: 1..6 */
	const char *present_mode;  /* window_present_mode(): vsync, mailbox or immediate */
	int frame_rate;            /* the settings' frame_rate: 30 | 60 */
	const char *log_dir;       /* the game's working directory: where session.log is written */
	const char *home;          /* the user's home path; replaced with ~ wherever it appears in a value */
} SysInfo;

/* dst: src with every occurrence of home (or its other slash form) replaced by ~; empty/NULL src -> "-" */
static void sysinfo_scrub(char *dst, size_t n, const char *src, const char *home) {
	char h2[512];
	size_t o = 0, i;
	if (!n) return;
	h2[0] = 0;
	if (home && *home) {
		for (i = 0; home[i] && i + 1 < sizeof h2; i++)
			h2[i] = home[i] == '\\' ? '/' : (home[i] == '/' ? '\\' : home[i]);
		h2[i] = 0;
	}
	if (!src || !*src) src = "-";
	while (*src && o + 1 < n) {
		size_t hl = 0;
		if (home && home[0] && !strncmp(src, home, strlen(home))) hl = strlen(home);
		else if (h2[0] && !strncmp(src, h2, strlen(h2))) hl = strlen(h2);
		if (hl) { dst[o++] = '~'; src += hl; } else dst[o++] = *src++;
	}
	dst[n ? (o < n ? o : n - 1) : 0] = 0;
}

/* append "label: <scrubbed value>\n" at *pos, bounded by n (truncation keeps out NUL-terminated) */
static void sysinfo_add(char *out, size_t n, size_t *pos, const char *label, const char *value, const char *home) {
	char v[512];
	if (*pos + 1 >= n) return;
	sysinfo_scrub(v, sizeof v, value, home);
	{	int k = snprintf(out + *pos, n - *pos, "%s: %s\n", label, v);
		if (k > 0) *pos += (size_t)k < n - *pos ? (size_t)k : n - *pos - 1;
	}
}

/* the whole text; returns the length written (always < n), "" when out is NULL or n is 0 */
static int sysinfo_text(char *out, size_t n, const SysInfo *s) {
	char v[512];
	size_t pos = 0;
	if (!out || !n) return 0;
	out[0] = 0;
	if (s->versions && *s->versions) {
		sysinfo_scrub(v, sizeof v, s->versions, s->home);
		if (pos + 1 < n) {
			int k = snprintf(out + pos, n - pos, "%s\n", v);
			if (k > 0) pos += (size_t)k < n - pos ? (size_t)k : n - pos - 1;
		}
	}
	sysinfo_add(out, n, &pos, "Platform", s->platform, s->home);
	sysinfo_add(out, n, &pos, "SDL", s->sdl_version, s->home);
	sysinfo_add(out, n, &pos, "CPU", s->cpu, s->home);
	sysinfo_add(out, n, &pos, "GPU", s->gpu, s->home);
	{	char w[64]; snprintf(w, sizeof w, "%dx%d", s->win_w, s->win_h);
		sysinfo_add(out, n, &pos, "Window", w, s->home); }
	{	char r[32]; snprintf(r, sizeof r, "%dx", s->render_scale);
		sysinfo_add(out, n, &pos, "Render scale", r, s->home); }
	sysinfo_add(out, n, &pos, "Presentation", s->present_mode, s->home);
	{	char f[32]; snprintf(f, sizeof f, "%d", s->frame_rate);
		sysinfo_add(out, n, &pos, "Frame rate", f, s->home); }
	sysinfo_add(out, n, &pos, "Log folder", s->log_dir, s->home);
	return (int)pos;
}

#endif
