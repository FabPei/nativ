/* pad_diagram_test.cpp - launcher/pad_diagram.h (UI-02c): every physical button sits inside the drawing and apart from the
 * others on every family's pad; the inverse map finds what each button carries, conflicts and buttons left off.
 * Build: c++ -std=c++17 -Wall -I .. -I ../../runtime/ui pad_diagram_test.cpp -o /tmp/pad_diagram_test && /tmp/pad_diagram_test */
#include <cmath>
#include <cstdio>
#include "pad_diagram.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
	for (int fam = 0; fam < PAD_FAM_N; fam++) {
		for (int a = 0; a < PD_N; a++) {
			const PadSpot s = pad_spot(fam, a);
			CHECK(s.r > 0 && s.x - s.r >= 0 && s.x + s.r <= 1 && s.y - s.r >= 0 && s.y + s.r <= PAD_DIAG_H);   /* inside */
			for (int b = a + 1; b < PD_N; b++) {
				const PadSpot t = pad_spot(fam, b);
				const float d = std::sqrt((s.x - t.x) * (s.x - t.x) + (s.y - t.y) * (s.y - t.y));
				if (d < s.r + t.r + 0.004f) { printf("  family %d: buttons %s and %s touch\n", fam, pd_names[a], pd_names[b]); CHECK(0); }
			}
		}
	}
	/* the families differ where the pads do: PlayStation's D-pad above its left stick, Xbox's below */
	CHECK(pad_spot(PAD_FAM_PS, PD_UP).y < pad_spot(PAD_FAM_PS, PD_LSTICK).y);
	CHECK(pad_spot(PAD_FAM_XBOX, PD_UP).y > pad_spot(PAD_FAM_XBOX, PD_LSTICK).y);
	CHECK(pad_spot(PAD_FAM_DECK, PD_SOUTH).x > pad_spot(PAD_FAM_PS, PD_SOUTH).x);   /* (the Deck is wider: its buttons at the ends) */
	/* the inverse */
	std::vector<std::string> v = {"south", "east", "west", "north", "start", "back", "dpad_up", "dpad_down", "dpad_left", "dpad_right", "left_stick", "right_stick"};
	auto on = pad_diag_on(v);
	CHECK(on[PD_SOUTH].size() == 1 && on[PD_SOUTH][0] == 0 && on[PD_RSTICK][0] == 11);
	CHECK(pad_diag_unplaced(v).empty());
	v[3] = "south";   /* Triangle on south too: a conflict */
	v[5] = "none";    /* Select on no button */
	on = pad_diag_on(v);
	CHECK(on[PD_SOUTH].size() == 2 && on[PD_NORTH].empty() && on[PD_BACK].empty());
	auto un = pad_diag_unplaced(v);
	CHECK(un.size() == 1 && un[0] == 5);
	CHECK(pad_diag_index("nonsense") == -1 && pad_diag_index("dpad_left") == PD_LEFT);
	printf("pad_diagram: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
