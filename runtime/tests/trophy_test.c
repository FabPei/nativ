/* trophy_test.c -- HLE-LIB-02: the trophy-set reader (trophy_parse.h) on a synthetic TRP (made here, no game bytes).
 * Fail-before is the runtime's record: sceNpTrophyInit and the trophy queries were placeholders in all five analysed
 * titles (12 of the 14 imported SceNpTrophy functions), so no trophy was ever counted or unlocked.
 *   cc -O1 -g -Wall -I ../core trophy_test.c -o /tmp/trophy_test && /tmp/trophy_test
 */
#include <stdio.h>
#include "trophy_parse.h"

static int fails;
static void check(const char *what, int ok) { printf("%-60s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void be64(uint8_t *p, uint64_t v) { be32(p, (uint32_t)(v >> 32)); be32(p + 4, (uint32_t)v); }

static const char conf[] = "<!--Sig--><trophyconf version=\"1.1\"><npcommid>NPXX00000_00</npcommid>"
	"<trophy id=\"000\" hidden=\"no\" ttype=\"P\" pid=\"-1\"></trophy>"
	"<trophy id=\"001\" hidden=\"no\" ttype=\"B\" pid=\"000\"></trophy>"
	"<trophy id=\"002\" hidden=\"yes\" ttype=\"G\" pid=\"000\" gid=\"1\"></trophy></trophyconf>";
static const char names[] = "<trophyconf><title-name>Test Set</title-name>"
	"<trophy id=\"000\" ttype=\"P\" pid=\"-1\"><name>All</name><detail>Every one</detail></trophy>"
	"<trophy id=\"001\" ttype=\"B\" pid=\"000\"><name>First &amp; Best</name><detail>Do &lt;it&gt;</detail></trophy>"
	"<trophy id=\"002\" ttype=\"G\" pid=\"000\"><name>Hidden</name><detail>Secret</detail></trophy></trophyconf>";

int main(void) {
	static uint8_t trp[4096]; memset(trp, 0, sizeof trp);
	be32(trp, 0xDCA24D00u); be32(trp + 4, 2); be32(trp + 0x10, 3); be32(trp + 0x14, 64);
	const char *nm[3] = {"TROPCONF.SFM", "TROP_01.SFM", "TROP000.PNG"}; const char *data[3] = {conf, names, "\x89PNG...."};
	size_t len[3] = {sizeof conf - 1, sizeof names - 1, 8}, off = 0x40 + 3 * 64;
	for (int i = 0; i < 3; i++) { uint8_t *e = trp + 0x40 + 64 * i; memcpy(e, nm[i], strlen(nm[i])); be64(e + 0x20, off); be64(e + 0x28, len[i]); memcpy(trp + off, data[i], len[i]); off += len[i]; }
	be64(trp + 8, off);

	const uint8_t *p; size_t n;
	check("TRP: TROPCONF.SFM found", trp_find(trp, off, "TROPCONF.SFM", &p, &n) && n == sizeof conf - 1);
	check("TRP: a missing entry is not found", !trp_find(trp, off, "GR001.PNG", &p, &n));
	check("TRP: a bad magic is refused", !trp_find((const uint8_t *)"nope", 4, "TROPCONF.SFM", &p, &n));
	TrophyDef d[TROPHY_MAX]; memset(d, 0, sizeof d);
	trp_find(trp, off, "TROPCONF.SFM", &p, &n);
	const int cnt = trp_parse_sfm((const char *)p, n, d, 0);
	check("conf: 3 trophies", cnt == 3);
	check("conf: grades P / B / G", d[0].grade == TROPHY_GRADE_PLATINUM && d[1].grade == TROPHY_GRADE_BRONZE && d[2].grade == TROPHY_GRADE_GOLD);
	check("conf: hidden only on 2", !d[0].hidden && !d[1].hidden && d[2].hidden);
	check("conf: pid of 1 is the platinum 0; gid default -1, 2 in group 1", d[1].pid == 0 && d[1].gid == -1 && d[2].gid == 1);
	trp_find(trp, off, "TROP_01.SFM", &p, &n);
	trp_parse_sfm((const char *)p, n, d, 1);
	check("names: entities decoded", !strcmp(d[1].name, "First & Best") && !strcmp(d[1].detail, "Do <it>"));
	check("names: the hidden flag from TROPCONF is kept", d[2].hidden);
	char title[64] = ""; trp_elem((const char *)p, (const char *)p + n, "title-name", title, sizeof title);
	check("names: the set's title", !strcmp(title, "Test Set"));
	check("icon: TROP000.PNG found", trp_find(trp, off, "TROP000.PNG", &p, &n) && n == 8);

	uint32_t f[4] = {0}; int32_t plat = 0;
	check("unlock: platinum cannot be unlocked directly", trophy_unlock(d, cnt, f, 0, &plat) == TROPHY_ERR_PLATINUM_CANNOT_UNLOCK);
	check("unlock: an unknown id is INVALID_TROPHY_ID", trophy_unlock(d, cnt, f, 7, &plat) == TROPHY_ERR_INVALID_TROPHY_ID);
	check("unlock: 1 ok, no platinum yet", trophy_unlock(d, cnt, f, 1, &plat) == 0 && plat == -1 && trophy_is_set(f, 1));
	check("unlock: 1 again is ALREADY_UNLOCKED", trophy_unlock(d, cnt, f, 1, &plat) == TROPHY_ERR_ALREADY_UNLOCKED);
	check("unlock: 2 completes the set -> platinum 0 unlocked", trophy_unlock(d, cnt, f, 2, &plat) == 0 && plat == 0 && trophy_is_set(f, 0));

	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("all passed\n");
	return 0;
}
