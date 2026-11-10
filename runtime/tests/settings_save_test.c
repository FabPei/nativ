/* settings.h: settings_save (UI QUEUE U8) -- 1 ok, 0 = any write failed. The unwritable case: a folder that refuses
 * new files; the retry once it is writable again must land (U6 keeps the change pending). No SDL, no game: build
 * from this folder with
 *     cc -O1 -g -Wall -I ../core settings_save_test.c ../core/settings.c -o /tmp/settings_save_test -lpthread && /tmp/settings_save_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "settings.h"

static int fails, oks;
#define CHECK(c, ...) do { if (c) oks++; else { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void put_file(const char *path, const char *body) {
	FILE *f = fopen(path, "w");
	if (!f) { printf("FAIL cannot write %s\n", path); exit(1); }
	fputs(body, f);
	fclose(f);
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
			while (*v == ' ' || *v == '\t') v++;
			memmove(out, v, strlen(v) + 1);
			e = out + strlen(out);
			while (e > out && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
		}
	}
	fclose(f);
	return out;
}

int main(void) {
	char b[96];

	/* a game file in a normal folder; a shared file in a folder that refuses new files once chmod'd */
	mkdir("/tmp/u8-s", 0777);
	put_file("/tmp/u8-s/shared.ini", "# shared\nopen_psid = AAAABBBBCCCCDDDD\nvolume = 1\n");
	put_file("/tmp/u8-game.ini", "# game\n");
	settings_load("/tmp/u8-game.ini");
	settings_load_shared("/tmp/u8-s/shared.ini");
	settings_set_int("volume", 2);

	/* fail-before: settings_save is void here -- this does not compile */
	chmod("/tmp/u8-s", 0555);
	CHECK(settings_save() == 0, "the unwritable shared folder -> 0");
	CHECK(settings_int("volume", -1) == 2, "the change is still pending in memory");

	/* the retry once the folder is writable again: 1, and the file proves it */
	chmod("/tmp/u8-s", 0755);
	CHECK(settings_save() == 1, "the writable retry -> 1");
	CHECK(!strcmp(file_value("/tmp/u8-s/shared.ini", "volume", b, sizeof b), "2"), "the retry wrote volume = 2: '%s'", b);
	CHECK(!strcmp(file_value("/tmp/u8-s/shared.ini", "open_psid", b, sizeof b), "AAAABBBBCCCCDDDD"), "the merge kept the rest");

	/* and nothing left to write is still 1 */
	CHECK(settings_save() == 1, "nothing to write -> 1");

	printf("%s (%d ok, %d failed)\n", fails ? "FAILED" : "all passed", oks, fails);
	return fails ? 1 : 0;
}
