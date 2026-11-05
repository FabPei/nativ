/* events.c -- the bounded event stream; see events.h. Pure stdio: no runtime, SDL or
 * guest dependency, so the ring and its format can be exercised on their own. display_frames()
 * gives the presented-frame number (0 before the first present, frozen while nothing renders);
 * a monotonic host clock gives milliseconds. */
#include "events.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EV_RING 256        /* the last 256 events: bounded by count and by line length */
#define EV_LINE 200

static char ring[EV_RING][EV_LINE];
static uint32_t ev_next;    /* events written (slot = written % EV_RING) */
static int ev_on = -1;      /* RECOMP_EVENTS: 0 off, otherwise on (default on: the volume is tiny) */
static int ev_dumped;
static uint64_t ev_t0;      /* ms origin at the first event */
static pthread_mutex_t ev_mu = PTHREAD_MUTEX_INITIALIZER;   /* input events come from the pad
                                                              * thread, stops from the main one */

extern uint64_t display_frames(void);

static void ev_dump_on_exit(void) { ev_dump("exit"); }   /* a run that ends through exit() without
                                                          * passing summary() still leaves its tail */

static uint64_t ev_ms(void)
{
	struct timespec t;
	uint64_t at;
	clock_gettime(CLOCK_MONOTONIC, &t);
	at = (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
	if (!ev_t0) ev_t0 = at;
	return at - ev_t0;
}

void ev_log(const char *kind, const char *fmt, ...)
{
	char detail[EV_LINE - 64], line[EV_LINE];
	va_list ap;
	int n;

	if (ev_on < 0) {
		const char *e = getenv("RECOMP_EVENTS");
		ev_on = !(e && (!*e || !strcmp(e, "0") || !strcmp(e, "off") || !strcmp(e, "no")));
	}
	if (!ev_on) return;
	{ static int once; if (!once) { once = 1; atexit(ev_dump_on_exit); } }  /* every exit() path
	                                                                        * recaps the ring once */
	va_start(ap, fmt);
	vsnprintf(detail, sizeof detail, fmt, ap);
	va_end(ap);
	n = snprintf(line, sizeof line, "  ev f%llu t%llu %s %s",
	             (unsigned long long)display_frames(), (unsigned long long)ev_ms(),
	             kind ? kind : "-", detail);
	if (n < 0) return;
	if (n >= (int)sizeof line) n = (int)sizeof line - 1;
	pthread_mutex_lock(&ev_mu);
	memcpy(ring[ev_next % EV_RING], line, (size_t)n + 1);
	ev_next++;
	fprintf(stderr, "%s\n", line);
	pthread_mutex_unlock(&ev_mu);
}

void ev_dump(const char *why)
{
	uint32_t i, first, count;

	if (ev_on < 0) return;                  /* nothing ever logged */
	pthread_mutex_lock(&ev_mu);
	if (ev_dumped || !ev_on || !ev_next) { pthread_mutex_unlock(&ev_mu); return; }
	ev_dumped = 1;
	first = ev_next > EV_RING ? ev_next - EV_RING : 0;
	count = ev_next - first;
	fprintf(stderr, "  ev-dump (%s): last %u events\n", why ? why : "end", count);
	for (i = first; i < ev_next; i++) fprintf(stderr, "%s\n", ring[i % EV_RING]);
	pthread_mutex_unlock(&ev_mu);
}
