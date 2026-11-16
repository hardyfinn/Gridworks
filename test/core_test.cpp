// Checks the GridDrums brain without Rack:  g++ -std=c++11 -I src test/core_test.cpp -o /tmp/t && /tmp/t
#include "GridDrumsCore.hpp"
#include <cstdio>
#include <cstdlib>
using namespace gridworks;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
	GridDrumsCore c;
	// starter beat: kick on step 1
	CHECK(c.tick() & 1);
	CHECK(c.step == 0);
	// toggle step 2 of track 6 from the grid
	c.key(1, 5, true, false, false);
	CHECK(c.isOn(5, 1));
	CHECK(c.tick() & (1 << 5));
	// slot change waits for the bar
	c.key(3, 7, true, false, false);
	CHECK(c.queued == 3 && c.cur == 0);
	for (int i = 0; i < 13; i++) c.tick();   // steps 3..15
	CHECK(c.step == 14 && c.cur == 0);
	c.tick();
	CHECK(c.step == 15 && c.cur == 0);
	c.tick();
	CHECK(c.step == 0 && c.cur == 3 && c.queued == -1);
	// immediate when clock stopped
	c.key(0, 7, true, false, true);
	CHECK(c.cur == 0);
	// mute
	c.key(0, 6, true, false, false);
	CHECK(c.mute[0]);
	c.step = -1;
	CHECK((c.tick() & 1) == 0);
	c.key(0, 6, true, false, false);
	// copy with held key: hold COPY, press slot 9
	c.key(14, 6, true, false, false);
	c.key(8, 7, true, false, false);
	c.key(14, 6, false, false, false);
	CHECK(c.pat[8][0] == c.pat[0][0] && c.pat[8][5] == c.pat[0][5]);
	CHECK(c.queued == -1);   // the copy did not also select the slot
	// clear a track from the screen (latched)
	c.key(12, 6, true, true, false);
	CHECK(c.clearArmed);
	c.key(4, 0, true, true, false);
	CHECK(c.pat[0][0] == 0 && !c.clearArmed);
	// clear a slot with held key
	c.key(13, 6, true, false, false);
	c.key(8, 7, true, false, false);
	c.key(13, 6, false, false, false);
	CHECK(!c.slotHasData(8));
	// reset applies a waiting slot
	c.key(5, 7, true, false, false);
	c.reset();
	CHECK(c.cur == 5 && c.step == -1);
	// LEDs
	uint8_t lv[8][16];
	bool fl[6] = {};
	c.levels(lv, false, fl);
	CHECK(lv[7][5] == 15);
	std::printf(fails ? "%d FAILED\n" : "all good\n", fails);
	return fails ? 1 : 0;
}
