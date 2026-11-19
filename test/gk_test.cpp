// GridKeys core tests: g++ -std=c++11 -Wall -I src test/gk_test.cpp && ./a.out
#include "GridKeysCore.hpp"
#include <cstdio>
#include <cstdlib>
using namespace gridworks;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int noteAt(GridKeysCore& g, int x, int y) { int n = -999; g.noteAt(x, y, n); return n; }

int main() {
	GridKeysCore g;   // C major, octave 3, in key, 4th rows, width 8
	CHECK(noteAt(g, 0, 7) == -12);          // bottom-left = C3
	CHECK(noteAt(g, 1, 7) == -10);          // D3
	CHECK(noteAt(g, 7, 7) == 0);            // 8th key = C4
	CHECK(noteAt(g, 0, 6) == -7);           // next row starts on F3 (3 steps up)
	CHECK(noteAt(g, 3, 7) == noteAt(g, 0, 6));   // same note in two places
	int n;
	CHECK(!g.noteAt(8, 7, n));              // past the width
	CHECK(!g.noteAt(15, 0, n));             // control column
	// all in-key notes
	for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) CHECK(g.inScale(noteAt(g, x, y)));

	// chromatic, 4ths
	g.chromatic = true;
	CHECK(noteAt(g, 0, 6) == -12 + 5);
	CHECK(noteAt(g, 5, 7) == noteAt(g, 0, 6));
	// root D, octave 2
	g.root = 2; g.octave = 2;
	CHECK(noteAt(g, 0, 7) == -24 + 2);
	g.root = 0; g.octave = 3;
	// octave rows, width 4 -> clamped to 4
	g.rowMode = 3; g.width = 4;
	CHECK(g.rowClamped());
	CHECK(g.rowStep() == 4);
	g.width = 16;                            // capped at 10
	CHECK(!g.rowClamped());
	CHECK(g.noteAt(9, 0, n) && !g.noteAt(10, 0, n));
	CHECK(noteAt(g, 0, 6) == 0);
	// sequential
	g.rowMode = 4; g.width = 8;
	CHECK(noteAt(g, 0, 6) == -12 + 8);
	g.chromatic = false; g.rowMode = 4;
	CHECK(noteAt(g, 0, 6) == noteAt(g, 7, 7) + 2);   // continues the scale
	g.rowMode = 0;

	// pentatonic in key, octave rows
	g.scale = 11; g.rowMode = 3;   // minor pentatonic
	CHECK(noteAt(g, 5, 7) == 0);   // 5 notes then C4
	CHECK(noteAt(g, 0, 6) == 0);
	g.scale = 0; g.rowMode = 0;

	// chord keys: triad on D (ii) in C major = D F A
	g.plays = 1;
	std::vector<int> c = g.chordFor(-10);
	CHECK(c.size() == 3 && c[0] == -10 && c[1] == -7 && c[2] == -3);
	g.plays = 2;   // 7th on G = G B D F
	c = g.chordFor(-5);
	CHECK(c.size() == 4 && c[0] == -5 && c[1] == -1 && c[2] == 2 && c[3] == 5);
	c = g.chordFor(-11);   // C#: not in scale
	CHECK(c.size() == 1);
	// chord below the base note still works
	c = g.chordFor(-15);   // A2
	CHECK(c.size() == 4 && c[0] == -15 && c[1] == -12 && c[2] == -8 && c[3] == -5);

	// press a 7th chord: 4 voices
	g.key(1, 7, true);   // D3 -> D F A C
	int on = 0;
	for (int i = 0; i < 8; i++) on += g.gate(i);
	CHECK(on == 4);
	CHECK(g.triggerCount == 4);
	g.key(1, 7, false);
	on = 0;
	for (int i = 0; i < 8; i++) on += g.gate(i);
	CHECK(on == 0);

	// strum
	g.plays = 1; g.strum = 0.01f; g.triggerCount = 0;
	g.key(0, 7, true);
	CHECK(g.triggerCount == 1);
	for (int i = 0; i < 480; i++) g.advance(1.f / 48000.f);   // 10 ms
	CHECK(g.triggerCount == 2);
	for (int i = 0; i < 481; i++) g.advance(1.f / 48000.f);
	CHECK(g.triggerCount == 3);
	g.key(0, 7, false);
	CHECK(!g.sounding(-12) && !g.sounding(-8) && !g.sounding(-5));
	g.strum = 0.f; g.plays = 0;

	// mono: last note priority, falls back on release
	g.key(0, 7, true); g.key(2, 7, true);
	CHECK(g.monoNote(n) && n == -8);
	g.key(2, 7, false);
	CHECK(g.monoNote(n) && n == -12);
	g.key(0, 7, false);
	CHECK(!g.monoNote(n));

	// a key released after the width shrank still stops its note
	g.key(6, 7, true);
	CHECK(g.sounding(-1));
	g.width = 4;
	g.key(6, 7, false);
	CHECK(!g.sounding(-1));
	g.width = 8;
	// stolen voices don't leave the mono output stuck
	g.polyphony = 1;
	g.key(0, 7, true); g.key(1, 7, true); g.key(1, 7, false);
	CHECK(!g.monoNote(n));
	g.key(0, 7, false);
	g.polyphony = 8;

	// latch: notes stay, a new chord replaces them
	g.setLatch(true);
	g.key(0, 7, true); g.key(2, 7, true); g.key(0, 7, false); g.key(2, 7, false);
	CHECK(g.sounding(-12) && g.sounding(-8));
	g.key(4, 7, true); g.key(4, 7, false);
	CHECK(!g.sounding(-12) && !g.sounding(-8) && g.sounding(-5));
	g.setLatch(false);
	CHECK(!g.sounding(-5));

	// sustain key (control column, row 8)
	CHECK(g.key(15, 7, true) == GK_NONE && g.sustain);
	g.key(0, 7, true); g.key(0, 7, false);
	CHECK(g.sounding(-12));
	g.key(15, 7, false);
	CHECK(!g.sounding(-12));

	// control keys
	CHECK(g.key(15, 0, true) == GK_OCT_UP);
	CHECK(g.key(15, 1, true) == GK_OCT_DOWN);
	CHECK(g.key(15, 2, true) == GK_LATCH);
	CHECK(g.key(15, 3, true) == GK_MODE);
	CHECK(g.key(15, 4, true) == GK_ROW);
	CHECK(g.key(15, 5, true) == GK_PLAYS);
	CHECK(g.key(15, 0, false) == GK_NONE);

	// voice stealing with polyphony 2
	g.polyphony = 2;
	g.key(0, 7, true); g.key(1, 7, true); g.key(2, 7, true);
	CHECK(!g.sounding(-12) && g.sounding(-10) && g.sounding(-8));
	g.allOff();
	g.polyphony = 8;

	// retrigger: reused channel goes low briefly
	g.polyphony = 1;
	g.key(0, 7, true); g.key(1, 7, true);
	CHECK(!g.gate(0));
	for (int i = 0; i < 48; i++) g.advance(1.f / 48000.f);
	CHECK(g.gate(0) && g.voices[0].note == -10);
	g.allOff();
	g.polyphony = 8;

	// CHORD input as the scale: D F A (root D)
	g.chordOverride = true; g.chordRoot = 2; g.chordIv = {0, 3, 7};
	CHECK(noteAt(g, 0, 7) == -10);
	CHECK(noteAt(g, 1, 7) == -7);
	CHECK(noteAt(g, 3, 7) == 2);
	g.chordOverride = false;

	// octave shift
	g.octShift = 1;
	CHECK(noteAt(g, 0, 7) == 0);
	g.octShift = 0;

	// LEDs
	uint8_t lv[8][16];
	g.showNotes = {-10};
	g.key(2, 7, true);
	g.levels(lv, false);
	CHECK(lv[7][0] == 9);    // root
	CHECK(lv[7][1] == 12);   // shown
	CHECK(lv[7][2] == 15);   // playing
	CHECK(lv[7][3] == 4);    // in scale
	CHECK(lv[6][0] == 4);
	CHECK(lv[7][8] == 0);    // outside width
	CHECK(lv[2][15] == 4);   // latch off
	g.chromatic = true;
	g.levels(lv, true);
	CHECK(lv[7][1] == 1);    // C# faint
	CHECK(lv[3][15] == 15);

	// ---------- chord pads ----------
	{
		GridKeysCore k;   // C major, C3 bottom-left
		auto tap = [&](int p) { k.key(10 + p % 5, p / 5, true); k.key(10 + p % 5, p / 5, false); };
		auto play = [&](int x, int y) { k.key(x, y, true); std::vector<int> c = k.lastChord; k.key(x, y, false); return c; };
		std::vector<int> c = play(1, 7);                 // D3 alone
		CHECK(c.size() == 1 && c[0] == -10);
		tap(P_7);                                        // sticky 7: diatonic Dm7
		c = play(1, 7);
		CHECK((c == std::vector<int>{-10, -7, -3, 0}));
		CHECK(k.padSticky[P_7]);
		tap(P_MAJ);                                      // forced major: D F# A C
		c = play(1, 7);
		CHECK((c == std::vector<int>{-10, -6, -3, 0}));
		tap(P_MIN);                                      // replaces MAJ in its group
		CHECK(!k.padSticky[P_MAJ] && k.padSticky[P_MIN]);
		tap(P_MAJ7);                                     // replaces 7: D F A C#
		c = play(1, 7);
		CHECK((c == std::vector<int>{-10, -7, -3, 1}));
		tap(P_RST);
		CHECK(!k.padSticky[P_MIN] && !k.padSticky[P_MAJ7]);
		// hold a pad while playing = momentary
		k.key(10 + P_SUS4 % 5, P_SUS4 / 5, true);
		c = play(0, 7);                                  // C F G
		CHECK((c == std::vector<int>{-12, -7, -5}));
		k.key(10 + P_SUS4 % 5, P_SUS4 / 5, false);
		CHECK(!k.padSticky[P_SUS4]);
		c = play(0, 7);
		CHECK(c.size() == 1);
		// 9 brings the 7th: Cmaj9 in key = C E G B D
		tap(P_9);
		c = play(0, 7);
		CHECK((c == std::vector<int>{-12, -8, -5, -1, 2}));
		tap(P_RST);
		// inversions and voicing on a C triad
		k.plays = 1;
		c = play(0, 7);
		CHECK((c == std::vector<int>{-12, -8, -5}));
		tap(P_INV1);
		c = play(0, 7);
		CHECK((c == std::vector<int>{-8, -5, 0}));
		tap(P_INV2);
		CHECK(!k.padSticky[P_INV1]);
		c = play(0, 7);
		CHECK((c == std::vector<int>{-5, 0, 4}));
		tap(P_INV0);
		CHECK(k.inversion() == 0);
		tap(P_OPEN);                                     // C G E'
		c = play(0, 7);
		CHECK((c == std::vector<int>{-12, -5, 0 + 4 - 12 + 12}));
		tap(P_OPEN);
		tap(P_BASS);
		c = play(0, 7);
		CHECK((c == std::vector<int>{-24, -12, -8, -5}));
		tap(P_BASS);
		tap(P_TOP);
		c = play(0, 7);
		CHECK((c == std::vector<int>{-12, -8, 7}));
		tap(P_TOP);
		tap(P_PWR);
		c = play(0, 7);
		CHECK((c == std::vector<int>{-12, -5}));
		tap(P_RST);
		k.plays = 0;
		// RND always gives more than a triad's root alone
		tap(P_RND);
		for (int i = 0; i < 20; i++) { c = play(0, 7); CHECK(c.size() >= 4); }
		tap(P_RST);

		// ---------- chord memory ----------
		k.plays = 1;
		std::vector<int> dm = play(1, 7);                // D F A
		k.key(10, 5, true);                              // hold SAVE
		k.key(12, 6, true); k.key(12, 6, false);         // slot 3
		k.key(10, 5, false);
		CHECK(k.mem[2] == dm && k.selected == 2);
		k.key(0, 7, true); k.key(0, 7, false);           // play C so lastChord changes
		k.key(12, 6, true);                              // tap slot 3: plays Dm
		CHECK(k.sounding(-10) && k.sounding(-7) && k.sounding(-3));
		k.key(12, 6, false);
		CHECK(!k.sounding(-10));
		// copy slot 3 -> slot 7
		k.key(12, 5, true);
		k.key(12, 6, true); k.key(12, 6, false);
		k.key(11, 7, true); k.key(11, 7, false);
		k.key(12, 5, false);
		CHECK(k.mem[6] == dm);
		// delete slot 3
		k.key(11, 5, true);
		k.key(12, 6, true); k.key(12, 6, false);
		k.key(11, 5, false);
		CHECK(k.mem[2].empty());
		// NEXT from slot 7 wraps around to slot 7 (only one stored)
		k.selected = -1;
		k.key(14, 5, true);
		CHECK(k.selected == 6 && k.sounding(-10));
		k.key(14, 5, false);
		CHECK(!k.sounding(-10) && !k.memHeld[M_NEXT]);
		// empty slot plays nothing
		k.key(10, 6, true);
		CHECK(!k.monoNote(n));
		k.key(10, 6, false);
		// CV playing
		k.mem[0] = {0, 4, 7};
		k.cvPlay(0);
		CHECK(k.sounding(0) && k.sounding(7) && k.cvSlot == 0);
		k.cvPlay(6);
		CHECK(!k.sounding(0) && k.sounding(-10));
		k.cvPlay(-1);
		CHECK(!k.sounding(-10));
		// CV chords ignore latch when replaced
		k.setLatch(true);
		k.cvPlay(0); k.cvPlay(6);
		CHECK(!k.sounding(0));
		k.cvPlay(-1);
		k.setLatch(false);
		// LEDs
		uint8_t l2[8][16];
		k.levels(l2, false);
		CHECK(l2[6][10] == 6);     // stored slot 1
		CHECK(l2[7][11] == 10);    // selected slot 7
		CHECK(l2[6][11] == 1);     // empty
		CHECK(l2[3][10] == 7);     // INV0 shown as the default
		CHECK(l2[0][10] == 3);
		CHECK(l2[7][9] == 0);      // column 10 is beyond width 8
	}

	if (fails == 0) printf("all GridKeys tests passed\n");
	return fails ? 1 : 0;
}
