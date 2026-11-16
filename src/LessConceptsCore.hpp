// LessConcepts brain: a fresh C++ take on the norns script "less concepts"
// by dan derks and linus schrab (llllllll.co/t/less-concepts-3), without the
// ~refrain looper and with CV out instead of MIDI/engine.
//
// The idea: an 8-bit number (the SEED) is fed through a one-dimensional
// cellular automaton (the RULE, 0-255, like Wolfram's rule 30) once per step,
// so the 8 bits keep changing in a pattern set by the rule. Each of the two
// voices watches one bit: when that bit is 1, the voice plays a note. The note
// comes from the whole 8-bit number, scaled into a LOW..HIGH range of scale
// steps. Up to 16 snapshots of all the settings can be stored, recalled and
// cycled through automatically.
//
// No Rack code in here, so it can be tested on its own (test/lc_test.cpp).
// Grid coordinates below are 1-based (x 1..16, y 1..8), as in the original
// script's documentation, so its grid notes still apply:
//   (1,1)-(8,2)   bit for voice 1 (row 1) / voice 2 (row 2); (9,1)/(9,2) mute
//   (10,1)-(16,2) octave -3..+3 for each voice
//   row 3         randomise: 1 seed, 2 rule, 4/5 bits v1/v2, 7 low, 8 high,
//                 10/11 octave v1/v2, 13 time, 14 time + all, 16 all
//   rows 4-5      the 32 notes: hold (1,6) and press = LOW, hold (2,6) = HIGH
//   (1,8)-(3,8)   time slower / middle / faster
//   (9,6)-(11,6)  cycle snapshots down / random / up (hold = destructive)
//   (14,6) hold = clear all snapshots, (15,6) hold = clear selected,
//   (16,6) take snapshot
//   (9,7)-(16,8)  snapshots 1-16
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include "Scales.hpp"

namespace gridworks {

struct LCRandom {
	uint32_t state = 0x9E3779B9u;
	uint32_t next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
	int range(int lo, int hi) {   // inclusive
		if (hi < lo) { int t = lo; lo = hi; hi = t; }
		return lo + (int) (next() % (uint32_t) (hi - lo + 1));
	}
};

struct LCSnapshot {
	int seed = 36, rule = 30;
	int bit[2] = {1, 8};
	int low = 1, high = 14;
	int octave[2] = {0, 0};
	int timeSel = 3;
	int duration = 4;
};

struct LCStep {
	bool stepped = false;      // a step happened on this tick
	bool gate[2] = {false, false};
	float pitch[2] = {0.f, 0.f};   // V/OCT for voices that played
	int seed = 0;
};

struct LessConceptsCore {
	// --- settings (the norns params) ---
	int scale = 0;                    // index into SCALES
	int transpose = 0;                // global, semitones
	int tranAmount[2] = {12, 12};     // "transpose n": jump size in semitones
	int tranProb[2] = {0, 0};         // % chance a note jumps
	int gateProb[2] = {100, 100};     // % chance a gate plays
	int timeRange = 0;                // 0 legacy 1/8-1/32, 1 slow 1/1-1/16, 2 full 2/1-1/32
	int seedMin = 0, seedMax = 255, ruleMin = 0, ruleMax = 255;
	int lowMin = 1, lowMax = 32, highMin = 1, highMax = 32;
	int octMin = -2, octMax = 2;
	int defaultDuration = 4;          // cycle length in beats when there are no snapshots

	// --- live state ---
	int seed = 36;        // the 8 bits now
	int newSeed = 36;     // the start seed (what the SEED knob / snapshot sets)
	int rule = 30;
	int nextSeed = 0;
	int bit[2] = {1, 8};  // 1..8, 0 = muted
	int octave[2] = {0, 0};
	int low = 1, high = 14;
	int timeSel = 3;      // 1-based index into the time list
	int duration = 4;     // beats between snapshot changes when cycling

	LCSnapshot snaps[17];
	int snapCount = 0;
	int selected = 0;     // 0 = none
	int cycleDir = 0;     // 0 off, 1 down "<", 2 random "~", 3 up ">"
	bool destructive = false;   // "*": the selected snapshot keeps recording changes
	bool snapHeld = false;

	int ppqnCounter = PPQN * 16;
	int durationCounter = 1;
	bool lastGate[2] = {false, false};
	bool display[2] = {false, false};
	int gridNote = 0;     // 1..32 note shown on rows 4-5, 0 = none

	// grid hold keys
	bool holdLow = false, holdHigh = false;          // hardware hold
	bool armLow = false, armHigh = false;            // on-screen latch
	float holdTime[17] = {};   // seconds a timed key (cycle / clear) has been down, -1 = not down
	bool holdFired[17] = {};

	LCRandom rng;

	static const int PPQN = 96;

	LessConceptsCore() {
		for (int i = 0; i < 17; i++) holdTime[i] = -1.f;
		bang();
	}

	// ---------- time ----------
	static int timeCount(int range) {
		return range == 0 ? 5 : (range == 1 ? 9 : 13);
	}
	static double timeDivision(int range, int sel) {   // steps per quarter note
		static const double v0[] = {2, 3, 4, 6, 8};
		static const double v1[] = {1. / 4, 1. / 3, 1. / 2, 1 / 1.5, 1, 1.5, 2, 3, 4};
		static const double v2[] = {1. / 8, 1. / 6, 1. / 4, 1. / 3, 1. / 2, 1 / 1.5, 1, 1.5, 2, 3, 4, 6, 8};
		const double* v = range == 0 ? v0 : (range == 1 ? v1 : v2);
		return v[sel - 1];
	}
	static const char* timeName(int range, int sel) {
		static const char* n0[] = {"1/8", "1/8t", "1/16", "1/16t", "1/32"};
		static const char* n1[] = {"1/1", "1/1t", "1/2", "1/2t", "1/4", "1/4t", "1/8", "1/8t", "1/16"};
		static const char* n2[] = {"2/1", "2/1t", "1/1", "1/1t", "1/2", "1/2t", "1/4", "1/4t", "1/8", "1/8t", "1/16", "1/16t", "1/32"};
		const char** n = range == 0 ? n0 : (range == 1 ? n1 : n2);
		return n[sel - 1];
	}
	int timeMiddle() const { return (timeCount(timeRange) + 1) / 2; }
	void setTimeRange(int r) {
		timeRange = r < 0 ? 0 : (r > 2 ? 2 : r);
		timeSel = timeMiddle();
	}
	void clampTime() {
		int n = timeCount(timeRange);
		if (timeSel < 1) timeSel = 1;
		if (timeSel > n) timeSel = n;
	}

	// ---------- the automaton ----------
	int seedBit(int s, int b) const { return (s >> (b - 1)) & 1; }   // b = 1..8

	// Work out the next seed from the current one with the rule (cells wrap around).
	void bang() {
		int b[9];
		for (int i = 1; i <= 8; i++) b[i] = seedBit(seed, i);
		// neighbourhoods as in the script: output bit 8 looks at bits 1,8,7; bit 7 at 8,7,6; ... bit 1 at 2,1,8
		int packs[8][3] = {
			{b[1], b[8], b[7]}, {b[8], b[7], b[6]}, {b[7], b[6], b[5]}, {b[6], b[5], b[4]},
			{b[5], b[4], b[3]}, {b[4], b[3], b[2]}, {b[3], b[2], b[1]}, {b[2], b[1], b[8]},
		};
		int out = 0;
		for (int k = 0; k < 8; k++) {
			int idx = packs[k][0] * 4 + packs[k][1] * 2 + packs[k][2];   // 0..7
			int r = (rule >> idx) & 1;
			out |= r << (7 - k);
		}
		nextSeed = out;
	}

	// Scale step 1..32 picked by the current seed inside low..high.
	int scaledIndex() const {
		return (int) std::floor(((double) seed / 256.0) * (high + 1 - low) + low);
	}

	// ---------- setters used by knobs / CV ----------
	void setSeed(int s) {
		newSeed = seed = s & 255;
		bang();
		if (destructive && selected > 0) pack(selected);
	}
	void setRule(int r) {
		rule = r & 255;
		seed = newSeed;
		bang();
		if (destructive && selected > 0) pack(selected);
	}

	// ---------- snapshots ----------
	void pack(int set) {
		if (set < 1 || set > 16) return;
		LCSnapshot& p = snaps[set];
		p.seed = newSeed;
		p.rule = rule;
		p.bit[0] = bit[0]; p.bit[1] = bit[1];
		p.low = low; p.high = high;
		p.octave[0] = octave[0]; p.octave[1] = octave[1];
		p.timeSel = timeSel;
		p.duration = duration;
		selected = set;
	}
	void unpack(int set) {
		if (set < 1 || set > snapCount) return;
		const LCSnapshot& p = snaps[set];
		newSeed = seed = p.seed;
		rule = p.rule;
		bit[0] = p.bit[0]; bit[1] = p.bit[1];
		low = p.low; high = p.high;
		octave[0] = p.octave[0]; octave[1] = p.octave[1];
		timeSel = p.timeSel;
		clampTime();
		duration = p.duration;
		durationCounter = 1;
		bang();
	}
	void removeSnap(int set) {
		if (set < 1 || set > snapCount) return;
		for (int i = set; i < 16; i++) snaps[i] = snaps[i + 1];
		snaps[16] = LCSnapshot();
		selected = 0;
		snapCount = snapCount > 0 ? snapCount - 1 : 0;
		if (snapCount == 0) { cycleDir = 0; destructive = false; }
	}
	void clearSnaps() {
		snapCount = 0;
		cycleDir = 0;
		destructive = false;
		selected = 0;
	}
	void addSnap() {
		if (snapCount >= 16) return;
		snapCount++;
		pack(snapCount);
	}
	void selectSnap(int set) {
		if (set < 1 || set > snapCount) return;
		selected = set;
		unpack(set);
	}

	// ---------- randomising ----------
	void randomizeAll() {
		seed = newSeed = rng.range(seedMin, seedMax);
		rule = rng.range(ruleMin, ruleMax);
		bit[0] = rng.range(0, 8);
		bit[1] = rng.range(0, 8);
		low = rng.range(lowMin, lowMax);
		high = rng.range(highMin, highMax);
		octave[0] = rng.range(octMin, octMax);
		octave[1] = rng.range(octMin, octMax);
		bang();
	}

	// ---------- cycle mode keys (short / long press) ----------
	void cycleShort(int x) {   // x = 9 down, 10 random, 11 up
		int dir = x == 9 ? 1 : (x == 10 ? 2 : 3);
		if (destructive && cycleDir != 0) {
			cycleDir = dir;   // stays destructive, changes direction
		}
		else if (destructive) {
			// "*" on its own: a short press keeps it as it is (as the script does)
		}
		else {
			cycleDir = (cycleDir == dir) ? 0 : dir;
		}
		durationCounter = 1;
	}
	void cycleLong(int x) {
		int dir = x == 9 ? 1 : (x == 10 ? 2 : 3);
		if (cycleDir == 0 && !destructive) {
			destructive = true;      // "-" -> "*"
		}
		else if (destructive && cycleDir == 0) {
			cycleDir = dir;          // "*" -> "<*" etc.
		}
		else if (destructive) {
			destructive = false;     // "<*" -> "<"
			cycleDir = dir;
		}
		else {
			cycleDir = dir;          // "<" -> "<*"
			destructive = true;
		}
		if (selected == 0) selected = snapCount > 0 ? 1 : 0;
	}
	const char* cycleName() const {
		static const char* plain[] = {"-", "<", "~", ">"};
		static const char* star[] = {"*", "<*", "~*", ">*"};
		return destructive ? star[cycleDir] : plain[cycleDir];
	}

	// ---------- keys ----------
	// x, y are 0-based from the grid; z = 1 press, 0 release.
	void key(int gx, int gy, int z, bool fromScreen) {
		int x = gx + 1, y = gy + 1;
		if (x < 1 || x > 16 || y < 1 || y > 8) return;

		if (y == 8 && x <= 3) {
			if (z) {
				if (x == 1) timeSel--;
				if (x == 2) timeSel = timeMiddle();
				if (x == 3) timeSel++;
				clampTime();
			}
		}
		else if (y == 6 && x >= 9 && x <= 11) {
			if (snapCount == 0) return;
			if (z) { holdTime[x] = 0.f; holdFired[x] = false; }
			else if (holdTime[x] >= 0.f) {
				if (!holdFired[x]) cycleShort(x);
				holdTime[x] = -1.f;
			}
		}
		else if ((y == 1 || y == 2) && x <= 9) {
			bit[y - 1] = 9 - x;   // x = 9 mutes
		}
		else if ((y == 1 || y == 2) && x > 9) {
			if (z) octave[y - 1] = x - 13;
		}
		else if (y == 6 && (x == 1 || x == 2)) {
			if (fromScreen) {
				if (z) {
					if (x == 1) { armLow = !armLow; armHigh = false; }
					else { armHigh = !armHigh; armLow = false; }
				}
			}
			else {
				if (x == 1) holdLow = z;
				else holdHigh = z;
			}
		}
		else if ((y == 4 || y == 5) && z) {
			int n = (y == 4) ? x : x + 16;
			if (holdLow || armLow) { low = n; armLow = false; }
			else if (holdHigh || armHigh) { high = n; armHigh = false; }
		}
		else if (y == 3 && z) {
			switch (x) {
				case 1: seed = newSeed = rng.range(seedMin, seedMax); break;
				case 2: rule = rng.range(ruleMin, ruleMax); break;
				case 4: bit[0] = rng.range(0, 8); break;
				case 5: bit[1] = rng.range(0, 8); break;
				case 7: low = rng.range(lowMin, lowMax); break;
				case 8: high = rng.range(highMin, highMax); break;
				case 10: octave[0] = rng.range(octMin, octMax); break;
				case 11: octave[1] = rng.range(octMin, octMax); break;
				case 13: timeSel = rng.range(1, timeCount(timeRange)); break;
				case 14: timeSel = rng.range(1, timeCount(timeRange)); randomizeAll(); break;
				case 16: randomizeAll(); break;
				default: break;
			}
			bang();
		}
		else if ((y == 7 || y == 8) && x >= 9) {
			int set = (y == 7) ? x - 8 : x;
			if (z) {
				selectSnap(set);
				if (cycleDir != 0 || destructive) snapHeld = true;   // holding a snapshot pauses cycling
			}
			else snapHeld = false;
		}
		else if (y == 6 && x >= 14) {
			if (x == 16) {
				if (!z) addSnap();   // taken on release, as in the script
			}
			else {
				if (z) { holdTime[x] = 0.f; holdFired[x] = false; }
				else holdTime[x] = -1.f;
			}
		}
	}

	// Called with the time since last call, for the hold keys (0.5 s).
	void advanceTime(float dt) {
		for (int x : {9, 10, 11, 14, 15}) {
			if (holdTime[x] < 0.f || holdFired[x]) continue;
			holdTime[x] += dt;
			if (holdTime[x] >= 0.5f) {
				holdFired[x] = true;
				if (x == 14) clearSnaps();
				else if (x == 15) { if (snapCount > 0) removeSnap(selected); }
				else cycleLong(x);
			}
		}
	}

	// ---------- the clock: 96 ticks per quarter note ----------
	// After a reset the very next tick is a step, so the first clock plays at once.
	void reset() {
		ppqnCounter = PPQN * 16;
		durationCounter = 1;
		seed = newSeed;
		bang();
	}

	LCStep tick() {
		LCStep out;
		out.seed = seed;
		if (snapCount == 0) duration = defaultDuration;
		if (cycleDir != 0) durationCounter++;
		ppqnCounter++;
		if (ppqnCounter <= PPQN / timeDivision(timeRange, timeSel)) return out;
		ppqnCounter = 1;

		if (destructive && selected > 0) pack(selected);
		if (!snapHeld && durationCounter > PPQN * duration) {
			durationCounter = 1;
			if (cycleDir != 0 && snapCount > 0) {
				if (cycleDir == 3) { selected++; if (selected > snapCount) selected = 1; }
				else if (cycleDir == 1) { selected--; if (selected <= 0) selected = snapCount; }
				else selected = rng.range(1, snapCount);
				unpack(selected);
			}
		}

		seed = nextSeed;
		bang();
		out.stepped = true;
		out.seed = seed;
		gridNote = 0;
		for (int i = 0; i < 2; i++) {
			display[i] = false;
			if (bit[i] >= 1 && seedBit(seed, bit[i])) {
				if (rng.range(0, 99) < gateProb[i]) {
					int k = scaledIndex();          // 1..32
					int add = (rng.range(0, 99) < tranProb[i]) ? tranAmount[i] : 0;
					int semis = scaleNote(scale, k - 1) + 12 * octave[i] + transpose + add;
					out.gate[i] = true;
					out.pitch[i] = semis / 12.f;
					display[i] = true;
					gridNote = k;
				}
			}
		}
		return out;
	}

	// ---------- LEDs (0..15), out[row 0..7][col 0..15] ----------
	void levels(uint8_t out[8][16], bool fired[2]) const {
		std::memset(out, 0, 8 * 16);
		auto led = [&](int x, int y, int v) {
			if (x >= 1 && x <= 16 && y >= 1 && y <= 8) out[y - 1][x - 1] = (uint8_t) v;
		};
		for (int i = 1; i <= 8; i++) {
			if (seedBit(seed, i)) { led(9 - i, 1, 2); led(9 - i, 2, 2); }
		}
		for (int v = 0; v < 2; v++) {
			led(9 - bit[v], v + 1, 4);
			if (bit[v] >= 1 && seedBit(seed, bit[v]) && display[v] && fired && fired[v]) led(9 - bit[v], v + 1, 15);
			led(octave[v] + 13, v + 1, 15);
		}
		for (int x : {1, 2, 4, 5, 7, 8, 10, 11, 13, 14, 16}) led(x, 3, 4);
		for (int y = 7; y <= 8; y++)
			for (int x = 9; x <= 16; x++) led(x, y, 2);
		for (int s = 1; s <= snapCount; s++) led(s <= 8 ? s + 8 : s, s <= 8 ? 7 : 8, 6);
		if (snapCount > 0 && selected > 0) led(selected <= 8 ? selected + 8 : selected, selected <= 8 ? 7 : 8, 15);
		led(14, 6, (holdTime[14] >= 0.f) ? 15 : 2);
		led(15, 6, (holdTime[15] >= 0.f) ? 15 : 4);
		led(16, 6, 6);
		// played note (dim), then LOW and HIGH on top
		if (gridNote >= 1) led(gridNote <= 16 ? gridNote : gridNote - 16, gridNote <= 16 ? 4 : 5, 3);
		int lowLed = (holdLow || armLow) ? 15 : 6;
		int highLed = (holdHigh || armHigh) ? 15 : 6;
		led(low <= 16 ? low : low - 16, low <= 16 ? 4 : 5, lowLed);
		led(high <= 16 ? high : high - 16, high <= 16 ? 4 : 5, highLed);
		led(1, 6, lowLed);
		led(2, 6, highLed);
		// time keys: brightness shows how fast
		int n = timeCount(timeRange);
		int off = (int) std::floor(15.0 / n + 0.5);
		led(1, 8, off + (int) std::floor((1.0 - (double) timeSel / n) * 15 + 0.5));
		led(2, 8, 8);
		led(3, 8, 15 - (int) std::floor((1.0 - (double) timeSel / n) * 15 + 0.5));
		if (snapCount > 0) {
			led(9, 6, 4); led(10, 6, 4); led(11, 6, 4);
			int hl = destructive ? 15 : 8;
			if (cycleDir == 1) led(9, 6, hl);
			if (cycleDir == 2) led(10, 6, hl);
			if (cycleDir == 3) led(11, 6, hl);
			if (destructive && cycleDir == 0) { led(9, 6, 10); led(10, 6, 10); led(11, 6, 10); }
		}
	}
};

} // namespace gridworks
