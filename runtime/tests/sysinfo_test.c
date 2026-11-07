/* sysinfo.h: the "Copy system info" text (UI QUEUE U5) — one line per fact, every value scrubbed of the user's home
 * path (as vitarecomp.py cmd_report does). No SDL, no game: build from this folder with
 *     cc -O1 -g -Wall -I ../ui sysinfo_test.c -o /tmp/sysinfo_test && /tmp/sysinfo_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sysinfo.h"

static int fails, oks;
#define CHECK(c, ...) do { if (c) oks++; else { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static SysInfo full(const char *home) {
	SysInfo s;
	s.versions = "Nativ 1.2.3 · PCSF00532 EU";
	s.platform = "Windows";
	s.sdl_version = "3.4.0";
	s.cpu = "TestCPU 8 cores";
	s.gpu = "TestGPU 16 GB";
	s.win_w = 1280; s.win_h = 720;
	s.render_scale = 2;
	s.present_mode = "vsync";
	s.frame_rate = 60;
	s.log_dir = "C:/Users/bob/Games/nativ/recomp";
	s.home = home;
	return s;
}

int main(void) {
	char out[1024];
	int n;

	/* every field appears, the versions line first */
	SysInfo s = full("C:/Users/bob");
	n = sysinfo_text(out, sizeof out, &s);
	CHECK(n > 0 && n == (int)strlen(out), "returns the length written: %d vs %u", n, (unsigned)strlen(out));
	CHECK(!strncmp(out, "Nativ 1.2.3 · PCSF00532 EU\n", 27), "versions line first: %.40s", out);
	CHECK(strstr(out, "Platform: Windows\n"), "platform present");
	CHECK(strstr(out, "SDL: 3.4.0\n"), "SDL present");
	CHECK(strstr(out, "CPU: TestCPU 8 cores\n"), "cpu present");
	CHECK(strstr(out, "GPU: TestGPU 16 GB\n"), "gpu present");
	CHECK(strstr(out, "Window: 1280x720\n"), "window size present");
	CHECK(strstr(out, "Render scale: 2x\n"), "render scale present");
	CHECK(strstr(out, "Presentation: vsync\n"), "present mode present");
	CHECK(strstr(out, "Frame rate: 60\n"), "frame rate present");
	CHECK(strstr(out, "Log folder: C:/Users/bob/Games/nativ/recomp\n") == NULL, "log folder NOT raw: %.60s", strstr(out, "Log folder:"));

	/* home path replaced with ~ (both slash forms), the user name gone from the whole text */
	CHECK(strstr(out, "Log folder: ~/Games/nativ/recomp\n"), "home replaced (forward slashes): %.60s", strstr(out, "Log folder:"));
	CHECK(!strstr(out, "bob"), "no user name anywhere");
	s = full("C:\\Users\\bob");
	s.log_dir = "C:\\Users\\bob\\Games\\nativ";
	sysinfo_text(out, sizeof out, &s);
	CHECK(strstr(out, "Log folder: ~\\Games\\nativ\n"), "home replaced (backslashes): %.60s", strstr(out, "Log folder:"));
	CHECK(!strstr(out, "bob"), "no user name anywhere (backslash home)");
	s.home = NULL;   /* no home known: the path stands, but the function must not crash */
	sysinfo_text(out, sizeof out, &s);
	CHECK(strstr(out, "Log folder: C:\\Users\\bob\\Games\\nativ\n"), "home NULL: value kept");

	/* an empty value prints "-" (the overlay's missing-value convention); no versions line when unset */
	s = full("C:/Users/bob");
	s.versions = ""; s.cpu = NULL; s.gpu = "";
	sysinfo_text(out, sizeof out, &s);
	CHECK(!strstr(out, "Nativ"), "no versions line when unset");
	CHECK(strstr(out, "CPU: -\n"), "NULL value -> -: %.40s", strstr(out, "CPU:"));
	CHECK(strstr(out, "GPU: -\n"), "empty value -> -");

	/* truncation safe: bounded output, NUL-terminated, nothing written at or past n */
	char small[32];
	memset(small, 'X', sizeof small);
	s = full("C:/Users/bob");
	n = sysinfo_text(small, 16, &s);
	CHECK(n == 15 && strlen(small) == 15, "truncated to n-1: %d %u", n, (unsigned)strlen(small));
	CHECK(small[15] == 0 && small[16] == 'X', "NUL at n-1, nothing at or past n");
	char tiny[1];
	tiny[0] = 'X';
	CHECK(sysinfo_text(tiny, 1, &s) == 0 && tiny[0] == 0, "n=1: empty string");
	CHECK(sysinfo_text(out, 0, &s) == 0, "n=0: no write");
	CHECK(sysinfo_text(NULL, 16, &s) == 0, "NULL out: no crash");

	printf("%s (%d ok, %d failed)\n", fails ? "FAILED" : "all passed", oks, fails);
	return fails ? 1 : 0;
}
