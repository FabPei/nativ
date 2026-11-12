/* pad_rows_test.c -- the controller rows as the menus show them (runtime/ui/pad_rows.h, MAP-UI): every Vita and PS TV
 * button is shown once, in its group, the groups each in one piece; every controller button the mapping can use has the
 * pad's own name on every pad family (LT on an Xbox pad, L2 on a PlayStation pad, ZL on a Switch pad); every label,
 * group and button name the menus translate is in the catalog, in English and German; the keyboard's too.
 * Synthetic, no SDL:  cc -O1 -g -Wall -I ../ui pad_rows_test.c ../ui/ui_text.c -lm -o pad_rows_test && ./pad_rows_test */
#include <stdio.h>
#include <string.h>
#include "pad_rows.h"
#include "kb_bind.h"
#include "ui_text.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int in_catalog(const char *id) {   /* English and German both there */
	for (int i = 0, n = ui_text_count(); i < n; i++) {
		const char *cid, *en, *de; ui_text_at(i, &cid, &en, &de);
		if (!strcmp(cid, id)) return en && *en && de && *de;
	}
	return 0;
}
static void expect_label(int fam, const char *phys, const char *want) {
	const char *got = pad_phys_label(fam, phys);
	CHECK(got && !strcmp(got, want), "family %d, %s: \"%s\", want \"%s\"", fam, phys, got ? got : "(null)", want);
}

int main(void) {
	/* the display order: every row once */
	int seen[PADB_N] = {0};
	for (int k = 0; k < PADB_N; k++) { CHECK(pad_row_order[k] >= 0 && pad_row_order[k] < PADB_N, "order[%d] out of range", k); if (pad_row_order[k] >= 0 && pad_row_order[k] < PADB_N) seen[pad_row_order[k]]++; }
	for (int i = 0; i < PADB_N; i++) CHECK(seen[i] == 1, "row %s shown %d times", pad_rows[i].key, seen[i]);
	/* the groups in one piece, in display order */
	for (int k = 1; k < PADB_N; k++) {
		const char *g = pad_rows[pad_row_order[k]].group;
		if (strcmp(g, pad_rows[pad_row_order[k - 1]].group))
			for (int j = 0; j < k - 1; j++) CHECK(strcmp(pad_rows[pad_row_order[j]].group, g), "group %s split", g);
	}
	/* the rows' settings keys stay what window.c always saved (a player's bindings keep working) */
	CHECK(!strcmp(pad_rows[PADB_CROSS].key, "pad_bind_cross") && !strcmp(pad_rows[PADB_R3].key, "pad_bind_r3") && !strcmp(pad_rows[PADB_PS].key, "pad_bind_ps"), "settings keys changed");
	/* each row's default is a button the mapping knows, and its label and group are translated */
	for (int i = 0; i < PADB_N; i++) {
		int known = 0; for (int b = 0; b < PAD_PHYS_N; b++) if (!strcmp(pad_rows[i].def, pad_phys_names[b])) known = 1;
		CHECK(known, "%s: default %s is no controller button", pad_rows[i].key, pad_rows[i].def);
		CHECK(in_catalog(pad_rows[i].label), "%s: label id %s not in the catalog (EN + DE)", pad_rows[i].key, pad_rows[i].label);
		CHECK(in_catalog(pad_rows[i].group), "%s: group id %s not in the catalog (EN + DE)", pad_rows[i].key, pad_rows[i].group);
	}
	/* every controller button has a name on every pad: the pad's own, or a translated id */
	for (int fam = 0; fam < PAD_FAM_N; fam++)
		for (int b = 0; b < PAD_PHYS_N; b++) {
			const char *l = pad_phys_label(fam, pad_phys_names[b]);
			CHECK(l && *l, "family %d: %s has no name", fam, pad_phys_names[b]);
			if (l && !strncmp(l, "btn.", 4)) CHECK(in_catalog(l), "family %d: %s's id %s not in the catalog", fam, pad_phys_names[b], l);
		}
	CHECK(!strcmp(pad_phys_label(PAD_FAM_PS, "none"), "btn.none") && in_catalog("btn.none"), "an unbound row's name");
	/* the pads' own names where they differ */
	expect_label(PAD_FAM_PS, "south", "Cross"); expect_label(PAD_FAM_XBOX, "south", "A"); expect_label(PAD_FAM_SWITCH, "south", "B");
	expect_label(PAD_FAM_PS, "left_trigger", "L2"); expect_label(PAD_FAM_XBOX, "left_trigger", "LT"); expect_label(PAD_FAM_SWITCH, "left_trigger", "ZL");
	expect_label(PAD_FAM_PS, "left_shoulder", "L1"); expect_label(PAD_FAM_XBOX, "right_shoulder", "RB");
	expect_label(PAD_FAM_PS, "guide", "PS"); expect_label(PAD_FAM_XBOX, "guide", "Xbox"); expect_label(PAD_FAM_SWITCH, "guide", "Home");
	expect_label(PAD_FAM_PS, "left_stick", "L3"); expect_label(PAD_FAM_XBOX, "right_stick", "RS");
	expect_label(PAD_FAM_PS, "start", "Options"); expect_label(PAD_FAM_XBOX, "back", "View");
	/* the PS TV group says what it is for */
	CHECK(in_catalog("grp.pstv_help"), "the PS TV group's note");
	/* (MAP-TOUCH) the touch cursor's two rows in a group of their own, with a note; the PS TV group keeps L3 and R3 */
	CHECK(!strcmp(pad_rows[PADB_L2].group, "grp.touch") && !strcmp(pad_rows[PADB_R2].group, "grp.touch"), "L2 / R2 rows: the touch group");
	CHECK(in_catalog("grp.touch") && in_catalog("grp.touch_help"), "the touch group and its note");
	CHECK(!strcmp(pad_rows[PADB_L3].group, "grp.pstv") && !strcmp(pad_rows[PADB_R3].group, "grp.pstv"), "L3 / R3: PS TV only");
	/* the keyboard's labels and groups are translated too */
	for (int i = 0; i < KB_N; i++) {
		CHECK(in_catalog(kb_defs[i].label), "%s: label id %s not in the catalog", kb_defs[i].key, kb_defs[i].label);
		CHECK(in_catalog(kb_defs[i].group), "%s: group id %s not in the catalog", kb_defs[i].key, kb_defs[i].group);
	}
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
