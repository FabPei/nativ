/* gxm_instance_test.c -- sceGxmDrawInstanced split into ordinary draws (runtime/gfx/gxm_instance.h).
 * cc -O1 -Wall -I ../gfx gxm_instance_test.c -o gxm_instance_test && ./gxm_instance_test */
#include <stdio.h>
#include "gxm_instance.h"
static int fails, n;
static void check(const char *what, int ok) { n++; if (!ok) { fails++; printf("FAIL %s\n", what); } }
int main(void) {
	/* the shape of the draws that broke a game's field: 816 indices, a wrap of 204 -> 4 instances of 204 */
	check("816 / wrap 204: 4 instances", gxm_instance_count(816, 204) == 4);
	check("816 / wrap 204: 204 indices each", gxm_instance_indices(816, 204) == 204);
	check("wrap == count: one draw of all", gxm_instance_count(408, 408) == 1 && gxm_instance_indices(408, 408) == 408);
	check("wrap 0: one draw of all (the old path)", gxm_instance_count(480, 0) == 1 && gxm_instance_indices(480, 0) == 480);
	check("wrap > count: one draw of all", gxm_instance_count(100, 300) == 1 && gxm_instance_indices(100, 300) == 100);
	check("a remainder is dropped (whole instances only)", gxm_instance_count(10, 4) == 2);
	check("index sources 2 / 3 are per instance", gxm_index_source_is_instance(2) && gxm_index_source_is_instance(3));
	check("index sources 0 / 1 are per vertex", !gxm_index_source_is_instance(0) && !gxm_index_source_is_instance(1));
	check("instance 3 of an 80-byte stream at +240", gxm_instance_stream(0x22c07280u, 80, 3) == 0x22c07280u + 240);
	check("instance 0 at the stream's base", gxm_instance_stream(0x1000u, 80, 0) == 0x1000u);
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", n, fails);
	return fails != 0;
}
