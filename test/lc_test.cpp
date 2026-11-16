// Checks the LessConcepts brain without Rack:
//   g++ -std=c++11 -I src test/lc_test.cpp -o lc_test && ./lc_test
#include "LessConceptsCore.hpp"
#include <cstdio>
using namespace gridworks;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

// reference elementary automaton, written the textbook way: cell i's new value is
// rule bit (left*4 + self*2 + right); here "left" of bit b is b+1 (wrapping), as in the script.
static int ref(int seed, int rule) {
	int out = 0;
	for (int b = 1; b <= 8; b++) {
		int self = (seed >> (b - 1)) & 1;
		int left = (seed >> (b % 8)) & 1;          // bit b+1, bit 8 wraps to bit 1
		int right = (seed >> ((b + 6) % 8)) & 1;   // bit b-1, bit 1 wraps to bit 8
		out |= ((rule >> (left * 4 + self * 2 + right)) & 1) << (b - 1);
	}
	return out;
}

int main() {
	LessConceptsCore c;
	for (int r = 0; r < 256; r++)
		for (int s = 0; s < 256; s += 7) {
			c.rule = r; c.seed = s; c.bang();
			if (c.nextSeed != ref(s, r)) { std::printf("CA mismatch seed %d rule %d: %d vs %d\n", s, r, c.nextSeed, ref(s, r)); fails++; r = 256; break; }
		}
	c = LessConceptsCore();
	// 1/16 by default: first tick steps, then every 24 ticks
	int steps = 0, firstAt = -1, secondAt = -1;
	for (int t = 0; t < 96 * 4; t++) {
		LCStep st = c.tick();
		if (st.stepped) { steps++; if (firstAt < 0) firstAt = t; else if (secondAt < 0) secondAt = t; }
	}
	CHECK(firstAt == 0);
	CHECK(secondAt == 24);
	CHECK(steps == 16);
	// rule 30 from 36 matches the reference sequence
	c = LessConceptsCore();
	int s = 36, ok = 1;
	for (int i = 0; i < 40; i++) {
		s = ref(s, 30);
		LCStep st;
		do { st = c.tick(); } while (!st.stepped);
		if (st.seed != s) ok = 0;
	}
	CHECK(ok);
	// voice 1 watches bit 1: gate exactly when bit 1 of the seed is on
	c = LessConceptsCore();
	ok = 1;
	for (int i = 0; i < 64; i++) {
		LCStep st;
		do { st = c.tick(); } while (!st.stepped);
		if (st.gate[0] != (bool) (st.seed & 1)) ok = 0;
		if (st.gate[1] != (bool) (st.seed & 128)) ok = 0;
	}
	CHECK(ok);
	// mute key (9,1)
	c.key(8, 0, 1, false);
	CHECK(c.bit[0] == 0);
	// octave keys
	c.key(15, 1, 1, false);
	CHECK(c.octave[1] == 3);
	// low/high with hold
	c.key(0, 5, 1, false); c.key(4, 4, 1, false); c.key(0, 5, 0, false);
	CHECK(c.low == 21);
	c.key(1, 5, 1, true); c.key(2, 3, 1, true);   // screen latch
	CHECK(c.high == 3 && !c.armHigh);
	// time keys
	c.key(2, 7, 1, false);
	CHECK(c.timeSel == 4);
	c.key(1, 7, 1, false);
	CHECK(c.timeSel == 3);
	// snapshots: take two on release
	c.key(15, 5, 1, false); c.key(15, 5, 0, false);
	c.setSeed(99);
	c.key(15, 5, 1, false); c.key(15, 5, 0, false);
	CHECK(c.snapCount == 2 && c.selected == 2);
	c.key(8, 6, 1, false); c.key(8, 6, 0, false);   // snapshot 1
	CHECK(c.selected == 1 && c.newSeed == 36);
	// cycle up: short press on (11,6), then after 4 beats it moves to snapshot 2
	c.key(10, 5, 1, false); c.key(10, 5, 0, false);
	CHECK(c.cycleDir == 3 && !c.destructive);
	for (int t = 0; t < 96 * 4 + 30; t++) c.tick();
	CHECK(c.selected == 2 && c.newSeed == 99);
	// long press makes it destructive
	c.key(10, 5, 1, false); c.advanceTime(0.6f); c.key(10, 5, 0, false);
	CHECK(c.cycleDir == 3 && c.destructive);
	// hold clear-selected for 0.5 s
	c.key(14, 5, 1, false); c.advanceTime(0.3f);
	CHECK(c.snapCount == 2);
	c.advanceTime(0.3f); c.key(14, 5, 0, false);
	CHECK(c.snapCount == 1);
	// hold clear-all
	c.key(13, 5, 1, false); c.advanceTime(0.6f); c.key(13, 5, 0, false);
	CHECK(c.snapCount == 0 && c.cycleDir == 0);
	// pitch: major scale, low=high=1 -> root; octave +1 -> 1 V
	c = LessConceptsCore();
	c.low = c.high = 3; c.octave[0] = 1; c.bit[0] = 0; c.bit[1] = 0;
	c.bit[0] = 1;
	LCStep st;
	for (int i = 0; i < 200; i++) { st = c.tick(); if (st.gate[0]) break; }
	CHECK(st.gate[0] && std::fabs(st.pitch[0] - (1.f + 4.f / 12.f)) < 1e-5);
	// LEDs smoke test
	uint8_t lv[8][16]; bool f[2] = {true, true};
	c.levels(lv, f);
	CHECK(lv[0][13] == 15);   // octave +1 for voice 1 at x = 14
	std::printf(fails ? "%d FAILED\n" : "all good\n", fails);
	return fails ? 1 : 0;
}
