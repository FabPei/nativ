/* hub_scale_test.c - launcher/hub_scale.h (UI-01h). Build: cc -std=c99 -Wall -I .. hub_scale_test.c -o /tmp/hub_scale_test && /tmp/hub_scale_test */
#include <math.h>
#include <stdio.h>
#include "hub_scale.h"

static int fails, checks;
#define NEAR(a, b) do { checks++; if (fabsf((a) - (b)) > 0.005f) { fails++; printf("FAIL %s:%d: %s = %.3f, want %.3f\n", __FILE__, __LINE__, #a, (double)(a), (double)(b)); } } while (0)

int main(void) {
	NEAR(hub_ui_scale(480, 1), 1.0f);        /* the launcher's smallest window */
	NEAR(hub_ui_scale(800, 1), 1.0f);        /* Steam Deck */
	NEAR(hub_ui_scale(1080, 1), 1.35f);      /* a 1080p TV or monitor at 100 % */
	NEAR(hub_ui_scale(2160, 1), 2.7f);       /* a 4K TV at 100 % */
	NEAR(hub_ui_scale(4320, 1), 3.0f);       /* capped */
	NEAR(hub_ui_scale(1080, 1.5f), 1.5f);    /* the display's own scale wins when it is larger */
	NEAR(hub_ui_scale(2160, 2.0f), 2.7f);    /* a 4K monitor at 200 %: the height wins */
	NEAR(hub_ui_scale(800, 0), 1.0f);        /* no display scale known */
	NEAR(hub_ui_scale(0, 1.25f), 1.25f);     /* (a minimized window: no height) */
	printf("hub_scale: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
