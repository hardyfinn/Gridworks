// GridMutes core tests: g++ -std=c++11 -Wall -I src test/gm_test.cpp && ./a.out
#include "GridMutesCore.hpp"
#include <cstdio>
using namespace gridworks;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static void tap(GridMutesCore& g, int x, int y) { g.key(x, y, true); g.key(x, y, false); }
static void ch(GridMutesCore& g, int c) { tap(g, c / 6, c % 6); }

int main() {
	// no clock: changes land at once
	{
		GridMutesCore g;
		CHECK(g.on[0] && g.on[17]);
		ch(g, 0);
		CHECK(!g.on[0]);
		ch(g, 13);
		CHECK(!g.on[13]);
	}
	// clocked, next beat (16th clock)
	{
		GridMutesCore g;
		g.setClocked(true);
		g.clockPulse();             // pulse 0 (beat 1)
		ch(g, 2);
		CHECK(g.on[2] && g.pending);
		g.clockPulse(); g.clockPulse(); g.clockPulse();
		CHECK(g.on[2]);
		g.clockPulse();             // pulse 4 = beat 2
		CHECK(!g.on[2] && !g.pending);
	}
	// next bar and next 2 bars
	{
		GridMutesCore g;
		g.setClocked(true);
		tap(g, 14, 5);               // BAR
		CHECK(g.timing == T_BAR);
		g.clockPulse();              // beat 1 of bar 1
		ch(g, 1);
		for (int i = 0; i < 15; i++) g.clockPulse();
		CHECK(g.on[1]);
		g.clockPulse();              // pulse 16 = bar 2
		CHECK(!g.on[1]);
		tap(g, 15, 5);               // 2BAR; we're at pulse 17 (bar 2)
		ch(g, 1);
		for (int i = 0; i < 15; i++) g.clockPulse();   // up to pulse 32 = bar 3: not yet
		CHECK(!g.on[1]);
		for (int i = 0; i < 16; i++) g.clockPulse();   // pulse 48... wait for bar 4? 2-bar points are bars 1,3,5
		// pulse 32 is bar 3 (beat 8): 8 % 8 == 0 -> lands there
		CHECK(g.on[1]);
		// NOW key lands pending changes straight away
		tap(g, 13, 5);
		ch(g, 1);
		CHECK(g.pending);
		tap(g, 12, 5);
		CHECK(!g.pending && !g.on[1]);
	}
	// unplugging the clock lands pending changes
	{
		GridMutesCore g;
		g.setClocked(true);
		ch(g, 5);
		CHECK(g.on[5]);
		g.setClocked(false);
		CHECK(!g.on[5]);
	}
	// cue & lock
	{
		GridMutesCore g;
		tap(g, 4, 0);                // CUE on
		ch(g, 0); ch(g, 1); ch(g, 2); ch(g, 2);   // stage 0 and 1 (2 staged twice = off)
		CHECK(g.stagedCount() == 2 && g.on[0]);
		tap(g, 4, 1);                // LOCK: both drop (no clock = now)
		CHECK(!g.on[0] && !g.on[1] && g.on[2] && g.cue);
		ch(g, 3);
		tap(g, 4, 0);                // leaving cue drops the rest
		CHECK(!g.on[3] && !g.cue);
		// CLR forgets staged
		tap(g, 4, 0); ch(g, 4); tap(g, 5, 2);
		CHECK(g.stagedCount() == 0 && g.on[4]);
		tap(g, 4, 0);
		CHECK(g.on[4]);
	}
	// solo
	{
		GridMutesCore g;
		tap(g, 4, 2);                // SOLO mode
		ch(g, 6);
		CHECK(g.solo[6] && g.audible(6) && !g.audible(0));
		ch(g, 6);
		CHECK(!g.anySolo() && g.audible(0));
		ch(g, 7);
		tap(g, 4, 2);                // leave solo mode; solo stays
		CHECK(g.mode == MODE_MUTE && g.solo[7]);
		tap(g, 5, 2);                // CLR drops solos
		CHECK(!g.anySolo());
	}
	// A/B
	{
		GridMutesCore g;
		tap(g, 4, 3);
		ch(g, 0); ch(g, 1);
		CHECK(g.ab[0] == 1 && g.on[0] && !g.on[1]);
		tap(g, 4, 3);
		ch(g, 0);
		CHECK(!g.on[0] && g.on[1]);
		ch(g, 1);
		CHECK(g.on[0] && !g.on[1]);
		tap(g, 4, 3); ch(g, 1); ch(g, 0); tap(g, 4, 3);   // same two again = unlink
		CHECK(g.ab[0] == -1 && g.ab[1] == -1);
	}
	// stereo pairs
	{
		GridMutesCore g;
		tap(g, 4, 4);
		ch(g, 8);
		CHECK(g.pair[8] == 9 && g.pair[9] == 8);
		ch(g, 17);                   // last channel: nothing to pair with
		CHECK(g.pair[17] == -1);
		tap(g, 4, 4);
		ch(g, 9);
		CHECK(!g.on[8] && !g.on[9]);
		tap(g, 4, 4); ch(g, 8); tap(g, 4, 4);
		CHECK(g.pair[8] == -1);
	}
	// INV, ALL+, ALL-
	{
		GridMutesCore g;
		ch(g, 0);
		tap(g, 4, 5);
		CHECK(g.on[0] && !g.on[1]);
		tap(g, 5, 0);
		for (int c = 0; c < GM_CH; c++) CHECK(g.on[c]);
		tap(g, 5, 1);
		for (int c = 0; c < GM_CH; c++) CHECK(!g.on[c]);
	}
	// scenes: tap SAVE then a scene; recall; delete; copy
	{
		GridMutesCore g;
		ch(g, 0); ch(g, 5);
		tap(g, 5, 3);                // SAVE armed
		CHECK(g.arm == ARM_SAVE);
		tap(g, 13, 0);               // scene 2
		CHECK(g.arm == ARM_NONE && g.sceneStored[1] && !g.scenes[1][0] && g.scenes[1][1]);
		tap(g, 5, 0);                // all on
		tap(g, 13, 0);               // recall scene 2
		CHECK(!g.on[0] && !g.on[5] && g.on[1] && g.scene == 1);
		tap(g, 5, 5); tap(g, 13, 0); tap(g, 15, 3);   // copy 2 -> 16
		CHECK(g.sceneStored[15] && !g.scenes[15][0]);
		tap(g, 5, 4); tap(g, 13, 0);                  // delete 2
		CHECK(!g.sceneStored[1]);
		tap(g, 5, 3); tap(g, 5, 3);                   // tap SAVE twice = cancel
		CHECK(g.arm == ARM_NONE);
		tap(g, 12, 0);                                // empty scene: nothing
		CHECK(g.scene == -1 || g.scene == 1 || g.scene == 15);
		// clocked recall waits for the beat
		g.setClocked(true);
		tap(g, 5, 0);
		g.clockPulse();
		tap(g, 15, 3);
		CHECK(g.on[0] && g.pendingScene == 15);
		for (int i = 0; i < 4; i++) g.clockPulse();
		CHECK(!g.on[0] && g.scene == 15);
		// next scene from TRIG
		g.setClocked(false);
		tap(g, 5, 3); tap(g, 12, 0);   // save current as scene 1
		g.scene = 15;
		g.nextScene();
		CHECK(g.scene == 0);
	}
	// LEDs
	{
		GridMutesCore g;
		float lv[GM_CH] = {};
		lv[0] = 1.f; lv[1] = 0.5f;
		ch(g, 1);
		uint8_t o[8][16];
		g.levels(o, lv);
		CHECK(o[0][0] == 12 && o[1][0] == 2);
		CHECK(o[0][7] == 15);            // ch 1 loud and on
		CHECK(o[1][7] == 3);             // ch 2 muted: dim grey level
		CHECK(o[5][13] == 15 && o[5][12] == 4);
		CHECK(o[0][12] == 1);
	}
	if (fails == 0) printf("all GridMutes tests passed\n");
	return fails ? 1 : 0;
}
