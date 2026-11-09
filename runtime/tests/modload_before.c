/* modload_before.c -- the behaviour before MODLOAD-01, for modload_test.c's fail-before run: neither call had an
 * implementation, so main.c's generic stub answered both (r0 = 0, nothing loaded, no module_start). */
const HleEntry hle_modload[] = { {0, 0} };
