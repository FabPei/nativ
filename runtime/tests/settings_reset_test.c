/* settings.h: settings_reset_all (UI QUEUE U6) -- every key of both files goes except the keep list (an exact name,
 * or a prefix when it ends in '*'), then the files are saved; 0 on a write failure. The identity keys
 * (settings_default_keep) must survive with their values -- also across a failed write, where the change stays
 * pending and the next save writes it. No SDL, no game: build from this folder with
 *     cc -O1 -g -Wall -I ../core settings_reset_test.c ../core/settings.c -o /tmp/settings_reset_test -lpthread && /tmp/settings_reset_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "settings.h"

static int fails, oks;
#define CHECK(c, ...) do { if (c) oks++; else { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *SH = "/tmp/u6-shared.ini", *GM = "/tmp/u6-game.ini";

static void put_file(const char *path, const char *body) {
	FILE *f = fopen(path, "w");
	if (!f) { printf("FAIL cannot write %s\n", path); exit(1); }
	fputs(body, f);
	fclose(f);
}
static void add_line(const char *path, const char *line) {
	FILE *f = fopen(path, "a");
	if (!f) { printf("FAIL cannot append %s\n", path); exit(1); }
	fputs(line, f);
	fclose(f);
}
/* 1: the key is on its own key=value line in the file; the header line does not count */
static int in_file(const char *path, const char *key) {
	FILE *f = fopen(path, "r");
	char line[256]; int hit = 0;
	if (!f) return 0;
	while (fgets(line, sizeof line, f)) {
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		char *s = line;
		while (*s == ' ' || *s == '\t') s++;
		char *e = s + strlen(s);
		while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
		if (!strcmp(s, key)) hit = 1;
	}
	fclose(f);
	return hit;
}
/* the file's "key = value" line trimmed, "" when absent */
static const char *file_value(const char *path, const char *key, char *out, size_t n) {
	FILE *f = fopen(path, "r");
	char line[256];
	out[0] = 0;
	if (!f) return out;
	while (fgets(line, sizeof line, f)) {
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		char *s = line;
		while (*s == ' ' || *s == '\t') s++;
		char *e = s + strlen(s);
		while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
		if (!strcmp(s, key)) {
			snprintf(out, n, "%s", eq + 1);
			char *v = out;
			while (*v == ' ' || *v == '	') v++;
			memmove(out, v, strlen(v) + 1);
			e = out + strlen(out);
			while (e > out && (e[-1] == ' ' || e[-1] == '	' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
		}
	}
	fclose(f);
	return out;
}

int main(void) {
	char b[256];

	/* (1) a temp shared + game file with the task's keys: identity keys in shared, a game option and a shared-key
	 * override in the game's file */
	put_file(SH, "# shared\nopen_psid = AAAABBBBCCCCDDDD\nonline_id = ntv_tester\nnp_x = 7\nrender_scale = 3\n");
	put_file(GM, "# game\nmenu_hint_shown = 1\nfov = 1.2\nrender_scale = 2\n");
	settings_load(GM);
	settings_load_shared(SH);

	/* fail-before the function exists: this call does not compile */
	CHECK(settings_reset_all(settings_default_keep) == 1, "reset reports success");

	/* only the keep keys remain, values unchanged (np_x survives through the np_* prefix) */
	CHECK(!strcmp(settings_str("open_psid", "", b, sizeof b), "AAAABBBBCCCCDDDD"), "open_psid kept, unchanged: %s", b);
	CHECK(!strcmp(settings_str("online_id", "", b, sizeof b), "ntv_tester"), "online_id kept, unchanged: %s", b);
	CHECK(settings_int("np_x", -1) == 7, "np_x kept via np_*: %d", settings_int("np_x", -1));
	CHECK(settings_int("menu_hint_shown", -1) == 1, "menu_hint_shown kept: %d", settings_int("menu_hint_shown", -1));
	CHECK(!settings_has("render_scale"), "render_scale gone");
	CHECK(!settings_has("fov"), "fov gone");

	/* the files on disk agree: the keep keys are there (values on their lines), the rest is gone */
	CHECK(in_file(SH, "open_psid") && in_file(SH, "online_id") && in_file(SH, "np_x"), "shared file keeps the three keys");
	CHECK(!in_file(SH, "render_scale"), "shared file lost render_scale");
	CHECK(in_file(GM, "menu_hint_shown"), "game file keeps menu_hint_shown");
	CHECK(!in_file(GM, "render_scale") && !in_file(GM, "fov"), "game file lost both");
	CHECK(!strcmp(file_value(SH, "open_psid", b, sizeof b), "AAAABBBBCCCCDDDD") && !strcmp(file_value(SH, "np_x", b, sizeof b), "7"),
	      "kept values written unchanged");

	/* (2) a key another writer put into the file after our load goes too ("every key of both files") */
	add_line(SH, "late_key = 9\n");
	CHECK(settings_reset_all(settings_default_keep) == 1, "reset with a late key reports success");
	CHECK(!in_file(SH, "late_key"), "a key added after load is removed");

	/* (3) the layer move + a failed write: the pending value of a kept key survives the failure and the next save
	 * writes it (review finding 1: dirty must only clear once the write has landed) */
	mkdir("/tmp/u6-r", 0777);
	put_file("/tmp/u6-r/shared.ini", "# shared\nopen_psid = AAAABBBBCCCCDDDD\nonline_id = ntv_tester\nnp_x = 7\n");
	put_file(GM, "# game\nonline_id = ntv_tester\nmenu_hint_shown = 1\n");
	settings_load(GM);
	settings_load_shared("/tmp/u6-r/shared.ini");
	settings_set_str("online_id", "ntv_newer");   /* the move: shared gets the value, the game copy is marked gone */
	chmod("/tmp/u6-r", 0555);                     /* the folder refuses new files: the shared write must fail */
	CHECK(settings_reset_all(settings_default_keep) == 0, "the failed shared write is reported");
	CHECK(!strcmp(settings_str("online_id", "", b, sizeof b), "ntv_newer"), "the pending value is still in memory: %s", b);
	chmod("/tmp/u6-r", 0755);
	settings_save();                              /* the retry */
	CHECK(!strcmp(file_value("/tmp/u6-r/shared.ini", "online_id", b, sizeof b), "ntv_newer"),
	      "the next save writes the kept value: '%s'", b);
	CHECK(in_file("/tmp/u6-r/shared.ini", "open_psid") && in_file("/tmp/u6-r/shared.ini", "np_x"), "the kept keys are all there");
	CHECK(!in_file(GM, "online_id"), "the game-layer copy is gone after the move");

	/* (4) a write failure is reported: the game file's folder does not exist */
	settings_load("/tmp/u6-nonexistent-dir/game.ini");
	CHECK(settings_reset_all(settings_default_keep) == 0, "write failure -> 0");

	printf("%s (%d ok, %d failed)\n", fails ? "FAILED" : "all passed", oks, fails);
	return fails ? 1 : 0;
}
