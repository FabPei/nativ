/* kb_bind_test.c -- the keyboard bindings' table (runtime/ui/kb_bind.h, KB-MAP): every Vita control has a key, the
 * defaults are Vita3K's, no two actions share a default key or a settings key, none takes a key the host keeps for
 * itself (F1..F12, Escape, Alt), each group's rows are together, and the stick keys make the stick bytes.
 * Synthetic, no SDL:  cc -O1 -g -Wall -I ../ui kb_bind_test.c -o kb_bind_test && ./kb_bind_test */
#include <stdio.h>
#include <string.h>
#include "kb_bind.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void expect_def(int act, const char *key) {
	CHECK(!strcmp(kb_defs[act].def, key), "%s default %s, want %s", kb_defs[act].key, kb_defs[act].def, key);
}

int main(void) {
	CHECK(KB_N == 25, "KB_N %d, want 25 (4+4 sticks, 4 D-pad, 4 face, L R, Start Select PS, L2 R2 L3 R3)", KB_N);
	/* Vita3K's default keyboard mapping */
	expect_def(KB_MOVE_UP, "W"); expect_def(KB_MOVE_LEFT, "A"); expect_def(KB_MOVE_DOWN, "S"); expect_def(KB_MOVE_RIGHT, "D");
	expect_def(KB_LOOK_UP, "I"); expect_def(KB_LOOK_LEFT, "J"); expect_def(KB_LOOK_DOWN, "K"); expect_def(KB_LOOK_RIGHT, "L");
	expect_def(KB_UP, "Up"); expect_def(KB_DOWN, "Down"); expect_def(KB_LEFT, "Left"); expect_def(KB_RIGHT, "Right");
	expect_def(KB_CROSS, "X"); expect_def(KB_CIRCLE, "C"); expect_def(KB_SQUARE, "Z"); expect_def(KB_TRIANGLE, "V");
	expect_def(KB_L, "Q"); expect_def(KB_R, "E");
	expect_def(KB_START, "Return"); expect_def(KB_SELECT, "Right Shift"); expect_def(KB_PS, "P");
	expect_def(KB_L2, "U"); expect_def(KB_R2, "O"); expect_def(KB_L3, "F"); expect_def(KB_R3, "H");
	/* the settings keys the old table had stay (a player's saved rebinds keep working) */
	CHECK(!strcmp(kb_defs[KB_CROSS].key, "bind_cross") && !strcmp(kb_defs[KB_MOVE_UP].key, "bind_move_up") && !strcmp(kb_defs[KB_SELECT].key, "bind_select"),
	      "the old settings keys changed");
	static const char *const host[] = {"Escape", "Left Alt", "Right Alt", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
	for (int i = 0; i < KB_N; i++) {
		CHECK(kb_defs[i].key && !strncmp(kb_defs[i].key, "bind_", 5), "row %d: settings key %s", i, kb_defs[i].key);
		CHECK(kb_defs[i].label && *kb_defs[i].label && kb_defs[i].group && *kb_defs[i].group, "row %d: label or group empty", i);
		for (int j = i + 1; j < KB_N; j++) {
			CHECK(strcmp(kb_defs[i].def, kb_defs[j].def), "%s and %s share the default %s", kb_defs[i].key, kb_defs[j].key, kb_defs[i].def);
			CHECK(strcmp(kb_defs[i].key, kb_defs[j].key), "settings key %s twice", kb_defs[i].key);
		}
		for (size_t h = 0; h < sizeof host / sizeof host[0]; h++) CHECK(strcmp(kb_defs[i].def, host[h]), "%s takes the host's %s", kb_defs[i].key, host[h]);
		/* a group's rows together: once left, a group never comes back */
		if (i > 0 && strcmp(kb_defs[i].group, kb_defs[i - 1].group))
			for (int j = 0; j < i - 1; j++) CHECK(strcmp(kb_defs[j].group, kb_defs[i].group), "group %s split (rows %d and %d)", kb_defs[i].group, j, i);
	}
	/* the stick from its four keys: centre 128-ish (127.5 rounded), full tilt 0 / 255, a diagonal ~0.707 */
	unsigned char x, y;
	kb_stick(0, 0, 0, 0, &x, &y); CHECK(x == 128 && y == 128, "none held: %u,%u", x, y);
	kb_stick(1, 0, 0, 0, &x, &y); CHECK(x == 128 && y == 0, "up: %u,%u", x, y);
	kb_stick(0, 1, 0, 0, &x, &y); CHECK(x == 128 && y == 255, "down: %u,%u", x, y);
	kb_stick(0, 0, 1, 0, &x, &y); CHECK(x == 0 && y == 128, "left: %u,%u", x, y);
	kb_stick(0, 0, 0, 1, &x, &y); CHECK(x == 255 && y == 128, "right: %u,%u", x, y);
	kb_stick(1, 0, 0, 1, &x, &y); CHECK(x == 218 && y == 37, "up+right: %u,%u", x, y);
	kb_stick(1, 1, 1, 1, &x, &y); CHECK(x == 128 && y == 128, "all four: %u,%u", x, y);
	CHECK(kb_stick(0, 0, 0, 0, &x, &y) == 0 && kb_stick(0, 0, 1, 0, &x, &y) == 1, "kb_stick's return: any direction held");
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
