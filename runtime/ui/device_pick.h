/*
 * device_pick.h - which camera or controller to use when the player has chosen one by name (UI-02b): settings "pad_device"
 * and "camera_device" hold a device's name as SDL reports it ("" = automatic: the first one). The chosen one when it is
 * there (the same name; else the same name ignoring case), else the first one -- so a device that is gone falls back to
 * another instead of to nothing, and comes back by itself when it is plugged in again.
 * Plain C, header-only: runtime/tests/device_pick_test.c checks it.
 */
#ifndef RECOMP_DEVICE_PICK_H
#define RECOMP_DEVICE_PICK_H
#include <ctype.h>
#include <string.h>

static inline int device_same(const char *a, const char *b, int fold) {
	if (!a || !b) return 0;
	if (!fold) return !strcmp(a, b);
	for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
	return !*a && !*b;
}
/* the index (0..n-1) of the device to use among names[0..n-1]; want: the chosen name ("" or 0: automatic); -1 when n is 0 */
static inline int device_pick(const char *const *names, int n, const char *want) {
	if (n <= 0) return -1;
	if (want && *want) {
		for (int i = 0; i < n; i++) if (device_same(names[i], want, 0)) return i;
		for (int i = 0; i < n; i++) if (device_same(names[i], want, 1)) return i;
	}
	return 0;
}
/* the chosen device is the one in use (for a "not connected" mark: want set and none of the names matches) */
static inline int device_present(const char *const *names, int n, const char *want) {
	if (!want || !*want) return 1;
	for (int i = 0; i < n; i++) if (device_same(names[i], want, 1)) return 1;
	return 0;
}
#endif
