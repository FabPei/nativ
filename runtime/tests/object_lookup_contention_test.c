/* Synthetic bounded diagnostic of the real work-area lookup under contention.
 * It measures progress on this host; it does not assert a fairness guarantee. */
#define main existing_kernel_test_main
#include "kernel_test.c"
#undef main

enum { WORKERS = 4, ITERATIONS = 250000 };
static int start_flag;
typedef struct { int id, completed, wrong; uint64_t elapsed_us; } LookupJob;

static void *lookup_worker(void *arg) {
	LookupJob *j = arg;
	cur_tid = 0x40011000u + (uint32_t)j->id;
	while (!__atomic_load_n(&start_flag, __ATOMIC_ACQUIRE)) sched_yield();
	uint64_t start = mono_us();
	for (int i = 0; i < ITERATIONS; ++i) {
		Obj *o = by_work(0x1000, LWMUTEX);
		if (!o || o->work != 0x1000) ++j->wrong;
		++j->completed;
	}
	j->elapsed_us = mono_us() - start;
	return NULL;
}

int main(void) {
	pthread_t threads_local[WORKERS];
	LookupJob jobs[WORKERS] = {0};
	new_obj_at(LWMUTEX, 0, 0, 0x1000);
	for (int i = 0; i < WORKERS; ++i) {
		jobs[i].id = i;
		if (pthread_create(&threads_local[i], NULL, lookup_worker, &jobs[i])) return 2;
	}
	__atomic_store_n(&start_flag, 1, __ATOMIC_RELEASE);
	int failed = 0;
	for (int i = 0; i < WORKERS; ++i) {
		pthread_join(threads_local[i], NULL);
		printf("worker %d: %d/%d lookups, wrong=%d, elapsed_us=%llu\n", i,
		       jobs[i].completed, ITERATIONS, jobs[i].wrong,
		       (unsigned long long)jobs[i].elapsed_us);
		failed |= jobs[i].completed != ITERATIONS || jobs[i].wrong != 0;
	}
	/* Exercise the real handlers: disabled counters stay zero, enabled counters
	 * count both an entry and its return once per completed lookup. */
	Cpu c = {0};
	for (int enabled = 0; enabled < 2; ++enabled) {
		lwm_count_on = enabled;
		c.r[0] = 0x1000; c.r[1] = 1; c.r[2] = 0;
		h_lock_lwmutex(&c);
		failed |= c.r[0] != 0;
		c.r[0] = 0x1000; c.r[1] = 1;
		h_unlock_lwmutex(&c);
		failed |= c.r[0] != 0;
		uint64_t entered = __atomic_load_n(&main_thread.lw_lookup_enter, __ATOMIC_RELAXED);
		uint64_t returned = __atomic_load_n(&main_thread.lw_lookup_return, __ATOMIC_RELAXED);
		printf("counter %s: entered=%llu returned=%llu\n", enabled ? "on" : "off",
		       (unsigned long long)entered, (unsigned long long)returned);
		failed |= entered != (uint64_t)enabled || returned != (uint64_t)enabled;
	}
	return failed ? 1 : 0;
}
