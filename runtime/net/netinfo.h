/* netinfo.h -- netinfo.c's pure parts (runtime/tests/netinfo_test.c) */
#ifndef RECOMP_NETINFO_H
#define RECOMP_NETINFO_H
#include <stdint.h>

#define NETINFO_EINVAL 0x80410116u   /* SCE_NET_ERROR_EINVAL */

/* "aa:bb:cc:dd:ee:ff" (hex, ':' or '-') into mac: 1, or 0 when s is not one (mac untouched) */
static inline int netinfo_parse_mac(const char *s, uint8_t mac[6]) {
	uint8_t m[6];
	if (!s) return 0;
	for (int i = 0; i < 6; i++) {
		int v = 0;
		for (int k = 0; k < 2; k++, s++) {
			char ch = *s;
			int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
			if (d < 0) return 0;
			v = v * 16 + d;
		}
		m[i] = (uint8_t)v;
		if (i < 5) { if (*s != ':' && *s != '-') return 0; s++; }
	}
	if (*s) return 0;
	for (int i = 0; i < 6; i++) mac[i] = m[i];
	return 1;
}
#endif
