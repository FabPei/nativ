/* events.h -- the bounded event stream (N02g-3): low-frequency `  ev` lines that correlate input
 * delivery, observable load transitions and failures in ONE timeline, plus a last-N ring dumped
 * on failure. Volume stays small by design: only changes and terminal events are logged (input is
 * an edge, never every unchanged frame), and nothing is dumped on a healthy run. Disable with
 * RECOMP_EVENTS=0. Callers: display.c (input delivered, load mark), main.c/libc.c (assert, exit,
 * budget) -- see docs/input-recording.md. */
#ifndef EVENTS_H
#define EVENTS_H
#include <stdint.h>

/* one event: `  ev f<frame> t<ms> <kind> <detail>` to stderr (RECOMP_EVENTS=0 silences) and into
 * the ring. kind is a short lowercase tag; fmt is printf-style, one line, keep it under 120 chars */
void ev_log(const char *kind, const char *fmt, ...);
/* the ring in order, after `  ev-dump (<why>):`; once per run, and only the events actually logged
 * (the ring is written only while enabled) */
void ev_dump(const char *why);
#endif
