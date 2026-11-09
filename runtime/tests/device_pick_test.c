/* device_pick_test.c - runtime/ui/device_pick.h (UI-02b): the chosen camera or controller by name, the first one when it is
 * gone or nothing was chosen, nothing when there is none.
 * Build: cc -std=c99 -Wall -I ../ui device_pick_test.c -o /tmp/device_pick_test && /tmp/device_pick_test */
#include <stdio.h>
#include "device_pick.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
	const char *pads[] = {"Steam Virtual Gamepad", "Xbox Wireless Controller", "DualSense Wireless Controller"};
	/* before UI-02b the runtime opened the first device, always: what device_pick gives with nothing chosen */
	CHECK(device_pick(pads, 3, "") == 0 && device_pick(pads, 3, 0) == 0);
	/* the chosen one */
	CHECK(device_pick(pads, 3, "DualSense Wireless Controller") == 2);
	CHECK(device_pick(pads, 3, "xbox wireless controller") == 1);   /* (a name written by hand, another case) */
	/* gone: the first one, not nothing; marked as not connected */
	CHECK(device_pick(pads, 3, "8BitDo Pro 2") == 0);
	CHECK(!device_present(pads, 3, "8BitDo Pro 2") && device_present(pads, 3, "DualSense Wireless Controller") && device_present(pads, 3, ""));
	/* the exact name wins over a case-folded one */
	const char *two[] = {"cam", "Cam"};
	CHECK(device_pick(two, 2, "Cam") == 1);
	/* none at all */
	CHECK(device_pick(pads, 0, "Xbox Wireless Controller") == -1);
	CHECK(!device_same("ab", "abc", 1) && !device_same(0, "a", 0));
	printf("device_pick: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
