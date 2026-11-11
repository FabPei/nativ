/* log_redact_test.c -- LOG-REDACT (runtime/ui/log_redact.h): what the launcher's Copy log / Save log... and
 * `vitarecomp report` (pipeline/redact.py, the same rules) take out of a log before it leaves the computer.
 * Fail-before: Save log wrote the log as it was -- a bug report on the public repo carried the player's Windows user
 * name in every path (C:\Users\<name>\OneDrive\...), and a report's settings.ini its NP account id. Build: README.md here.
 */
#include <stdio.h>
#include "../ui/log_redact.h"

static int fails;
static void check(const char *what, const char *in, const char *want) {
	size_t n; char *o = log_redact(in, strlen(in), &n);
	int ok = o && !strcmp(o, want) && n == strlen(want);
	printf("%-60s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) { printf("    got  [%s]\n    want [%s]\n", o ? o : "(null)", want); fails++; }
	free(o);
}

int main(void) {
	check("a Windows user folder", "shader recipes from C:\\Users\\alice\\OneDrive\\Desktop\\Nativ\\packs", "shader recipes from ~\\OneDrive\\Desktop\\Nativ\\packs");
	check("forward slashes, another case", "open c:/users/Bob/AppData/Local/x.log", "open ~/AppData/Local/x.log");
	check("a quoted path", "\"C:\\Users\\carol\\Downloads\\a.pkg\"", "\"~\\Downloads\\a.pkg\"");
	check("Linux and macOS homes", "at /home/dave/.local and /Users/erin/Library", "at ~/.local and ~/Library");
	check("the shared Public folder stays", "C:\\Users\\Public\\Documents", "C:\\Users\\Public\\Documents");
	check("a home inside another path is no home", "C:\\games\\home\\x and D:\\Users", "C:\\games\\home\\x and D:\\Users");
	check("already redacted stays", "~\\Downloads\\ProjectNativ", "~\\Downloads\\ProjectNativ");
	check("identity keys lose their value", "fullscreen = 1\nnp_account_id = d637ece97f48c2ff\nonline_id = alice\nopen_psid=0011aabb\n",
	      "fullscreen = 1\nnp_account_id = <removed>\nonline_id = <removed>\nopen_psid=<removed>\n");
	check("an empty identity value stays empty", "online_id = \r\nframe_rate = 60\r\n", "online_id = \r\nframe_rate = 60\r\n");
	check("only at a line's start: a log line naming the key stays", "    np: online_id (none) offline", "    np: online_id (none) offline");
	check("nothing personal: unchanged", "frame 300: 960x544 at 0x21940000", "frame 300: 960x544 at 0x21940000");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
