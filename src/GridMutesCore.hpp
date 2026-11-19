// GridMutes brain: 18 clocked performance mutes with cue & lock, solo, A/B toggle
// pairs, stereo pairs, invert and 16 scenes. No Rack code here, so it can be tested
// on its own (test/gm_test.cpp).
//
// Grid map (x 0-15 left to right, y 0-7 top to bottom):
//   x 0-2,  y 0-5   channels 1-18 (down each column)
//   x 4,    y 0-5   CUE LOCK SOLO A/B PAIR INV
//   x 5,    y 0-5   ALL+ ALL- CLR SAVE DEL COPY
//   x 7-9,  y 0-5   signal activity (same shape as the channel block, display only)
//   x 12-15,y 0-3   scenes 1-16
//   x 12-15,y 5     timing: NOW BEAT BAR 2BAR
// Every function key is tap-to-use (no holding), so the mouse works as well as the grid.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace gridworks {

static const int GM_CH = 18;
static const int GM_SCENES = 16;

enum GMTiming { T_NOW, T_BEAT, T_BAR, T_2BAR };
static const char* const GM_TIMING_NAMES[4] = {"NOW", "NEXT BEAT", "NEXT BAR", "NEXT 2 BARS"};
enum GMMode { MODE_MUTE, MODE_SOLO, MODE_AB, MODE_PAIR };
enum GMArm { ARM_NONE, ARM_SAVE, ARM_DEL, ARM_COPY };
static const char* const GM_FN_NAMES[2][6] = {
	{"CUE", "LOCK", "SOLO", "A/B", "PAIR", "INV"},
	{"ALL+", "ALL-", "CLR", "SAVE", "DEL", "COPY"}};
static const char* const GM_TIME_KEYS[4] = {"NOW", "BEAT", "BAR", "2BAR"};

struct GridMutesCore {
	// what you hear
	bool on[GM_CH];
	bool solo[GM_CH] = {};
	// where the next timing point takes them
	bool tgtOn[GM_CH];
	bool tgtSolo[GM_CH] = {};
	bool pending = false;
	// cue: staged flips, dropped together by LOCK (or by leaving cue)
	bool cue = false;
	bool staged[GM_CH] = {};

	int ab[GM_CH];      // A/B partner (always opposite), -1 = none
	int pair[GM_CH];    // stereo partner (always the same), -1 = none
	int mode = MODE_MUTE;
	int firstPick = -1; // A/B mode: first channel of the pair being made

	int arm = ARM_NONE;
	int copySrc = -1;
	bool sceneStored[GM_SCENES] = {};
	bool scenes[GM_SCENES][GM_CH] = {};
	int scene = -1;          // last scene recalled or saved
	int pendingScene = -1;   // scene waiting for the timing point

	int timing = T_BEAT;
	int pulsesPerBeat = 4;   // clock input: 4 = 16th notes
	int beatsPerBar = 4;
	bool clocked = false;    // set by the module: is a clock patched?
	long pulseCount = 0;
	double time = 0.0;       // for blinking

	GridMutesCore() {
		for (int c = 0; c < GM_CH; c++) { on[c] = tgtOn[c] = true; ab[c] = pair[c] = -1; }
	}

	bool anySolo() const {
		for (int c = 0; c < GM_CH; c++) if (solo[c]) return true;
		return false;
	}
	bool audible(int c) const { return on[c] && (!anySolo() || solo[c]); }
	int stagedCount() const {
		int n = 0;
		for (int c = 0; c < GM_CH; c++) n += staged[c];
		return n;
	}

	// ---- changes (land now, or at the next timing point) ----
	void apply() {
		std::memcpy(on, tgtOn, sizeof(on));
		std::memcpy(solo, tgtSolo, sizeof(solo));
		if (pendingScene >= 0) scene = pendingScene;
		pendingScene = -1;
		pending = false;
	}
	void changed() {
		if (timing == T_NOW || !clocked) apply();
		else pending = true;
	}
	// flip one channel in an array, taking its links with it
	void flip(bool* arr, int c) {
		arr[c] = !arr[c];
		if (pair[c] >= 0) arr[pair[c]] = arr[c];
		if (ab[c] >= 0) arr[ab[c]] = !arr[c];
	}
	void lock() {
		for (int c = 0; c < GM_CH; c++)
			if (staged[c]) { flip(tgtOn, c); staged[c] = false; }
		changed();
	}
	void clearPending() {
		std::memcpy(tgtOn, on, sizeof(on));
		std::memcpy(tgtSolo, solo, sizeof(solo));
		for (int c = 0; c < GM_CH; c++) staged[c] = false;
		pending = false;
		pendingScene = -1;
	}

	// ---- links ----
	void unlink(int c) {
		if (ab[c] >= 0) { ab[ab[c]] = -1; ab[c] = -1; }
		if (pair[c] >= 0) { pair[pair[c]] = -1; pair[c] = -1; }
	}
	void linkAB(int a, int b) {
		unlink(a);
		unlink(b);
		ab[a] = b;
		ab[b] = a;
		tgtOn[b] = !tgtOn[a];
		changed();
	}
	void linkPair(int a) {   // a with the next channel
		int b = a + 1;
		if (b >= GM_CH) return;
		unlink(a);
		unlink(b);
		pair[a] = b;
		pair[b] = a;
		tgtOn[b] = tgtOn[a];
		changed();
	}

	// ---- scenes ----
	void recall(int s) {
		if (s < 0 || s >= GM_SCENES || !sceneStored[s]) return;
		std::memcpy(tgtOn, scenes[s], sizeof(tgtOn));
		for (int c = 0; c < GM_CH; c++) staged[c] = false;
		pendingScene = s;
		changed();
	}
	void nextScene() {
		for (int i = 1; i <= GM_SCENES; i++) {
			int s = ((scene < 0 ? -1 : scene) + i + GM_SCENES) % GM_SCENES;
			if (sceneStored[s]) { recall(s); return; }
		}
	}
	void sceneKey(int s) {
		if (arm == ARM_SAVE) {
			std::memcpy(scenes[s], on, sizeof(on));
			sceneStored[s] = true;
			scene = s;
			arm = ARM_NONE;
		}
		else if (arm == ARM_DEL) {
			sceneStored[s] = false;
			if (scene == s) scene = -1;
			arm = ARM_NONE;
		}
		else if (arm == ARM_COPY) {
			if (copySrc < 0) { if (sceneStored[s]) copySrc = s; }
			else {
				std::memcpy(scenes[s], scenes[copySrc], sizeof(scenes[s]));
				sceneStored[s] = true;
				copySrc = -1;
				arm = ARM_NONE;
			}
		}
		else recall(s);
	}
	void setArm(int a) {
		arm = (arm == a) ? ARM_NONE : a;   // tap again to cancel
		copySrc = -1;
	}
	void setMode(int m) {
		mode = (mode == m) ? MODE_MUTE : m;
		firstPick = -1;
	}

	// ---- channel keys ----
	void channelKey(int c) {
		switch (mode) {
			case MODE_SOLO:
				tgtSolo[c] = !tgtSolo[c];
				if (pair[c] >= 0) tgtSolo[pair[c]] = tgtSolo[c];
				changed();
				break;
			case MODE_AB:
				if (firstPick < 0) firstPick = c;
				else if (firstPick == c) firstPick = -1;
				else {
					if (ab[c] == firstPick) unlink(c);   // the same two again = unlink
					else linkAB(firstPick, c);
					firstPick = -1;
				}
				break;
			case MODE_PAIR:
				if (pair[c] >= 0) unlink(c);
				else linkPair(c);
				break;
			default:
				if (cue) {
					staged[c] = !staged[c];
					if (pair[c] >= 0) staged[pair[c]] = staged[c];
					if (ab[c] >= 0) staged[ab[c]] = false;   // the partner follows on its own
				}
				else {
					flip(tgtOn, c);
					changed();
				}
		}
	}

	// A key press (grid or screen). Only presses matter: nothing is held.
	void key(int x, int y, bool down) {
		if (!down) return;
		if (x >= 0 && x <= 2 && y >= 0 && y <= 5) { channelKey(x * 6 + y); return; }
		if (x == 4 && y >= 0 && y <= 5) {
			switch (y) {
				case 0:   // CUE: leaving cue drops what was staged
					if (cue && stagedCount() > 0) lock();
					cue = !cue;
					if (cue) mode = MODE_MUTE;
					break;
				case 1: lock(); break;
				case 2: setMode(MODE_SOLO); break;
				case 3: setMode(MODE_AB); break;
				case 4: setMode(MODE_PAIR); break;
				case 5:
					for (int c = 0; c < GM_CH; c++) tgtOn[c] = !tgtOn[c];
					changed();
					break;
			}
			return;
		}
		if (x == 5 && y >= 0 && y <= 5) {
			switch (y) {
				case 0: for (int c = 0; c < GM_CH; c++) tgtOn[c] = true; changed(); break;
				case 1: for (int c = 0; c < GM_CH; c++) tgtOn[c] = false; changed(); break;
				case 2:
					clearPending();
					for (int c = 0; c < GM_CH; c++) tgtSolo[c] = false;   // CLR also drops solos
					changed();
					break;
				case 3: setArm(ARM_SAVE); break;
				case 4: setArm(ARM_DEL); break;
				case 5: setArm(ARM_COPY); break;
			}
			return;
		}
		if (x >= 12 && x <= 15 && y >= 0 && y <= 3) { sceneKey(y * 4 + (x - 12)); return; }
		if (x >= 12 && x <= 15 && y == 5) {
			timing = x - 12;
			if (timing == T_NOW && pending) apply();
		}
	}

	// ---- clock ----
	void reset() { pulseCount = 0; }
	// One clock pulse. Returns true if pending changes landed.
	bool clockPulse() {
		long pos = pulseCount++;
		int ppb = std::max(1, pulsesPerBeat);
		if (!pending || pos % ppb != 0) return false;
		long beat = pos / ppb;
		bool hit = timing == T_BEAT || (timing == T_BAR && beat % beatsPerBar == 0)
			|| (timing == T_2BAR && beat % (2 * beatsPerBar) == 0) || timing == T_NOW;
		if (hit) apply();
		return hit;
	}
	void setClocked(bool c) {
		clocked = c;
		if (!c && pending) apply();
	}
	void advance(double dt) { time += dt; }

	// ---- LEDs (0-15), out[y][x]; level[c] = signal 0..1 ----
	void levels(uint8_t out[8][16], const float* level) const {
		std::memset(out, 0, 8 * 16);
		bool fast = std::fmod(time, 0.25) < 0.125, slow = std::fmod(time, 0.8) < 0.4;
		bool solos = anySolo();
		for (int c = 0; c < GM_CH; c++) {
			int x = c / 6, y = c % 6;
			uint8_t v;
			if (mode == MODE_SOLO) v = solo[c] ? 15 : (on[c] ? 5 : 2);
			else if (mode == MODE_AB) v = (c == firstPick) ? (fast ? 15 : 4) : ab[c] >= 0 ? 13 : (on[c] ? 5 : 2);
			else if (mode == MODE_PAIR) v = pair[c] >= 0 ? 13 : (on[c] ? 5 : 2);
			else {
				v = on[c] ? (solos && !solo[c] ? 6 : 12) : 2;
				if (staged[c]) v = slow ? 15 : 1;
				else if (pending && (tgtOn[c] != on[c] || tgtSolo[c] != solo[c])) v = fast ? 15 : 1;
			}
			out[y][x] = v;
			// activity mirror
			float l = level ? std::min(1.f, std::max(0.f, level[c])) : 0.f;
			out[y][7 + x] = audible(c) ? (uint8_t) std::round(15.f * l) : (uint8_t) std::round(5.f * l);
		}
		int st = stagedCount();
		out[0][4] = cue ? (st > 0 && fast ? 8 : 15) : 4;
		out[1][4] = st > 0 ? 12 : 4;
		out[2][4] = mode == MODE_SOLO ? 15 : (solos ? 9 : 4);
		out[3][4] = mode == MODE_AB ? 15 : 4;
		out[4][4] = mode == MODE_PAIR ? 15 : 4;
		out[5][4] = 4;
		out[0][5] = 4;
		out[1][5] = 4;
		out[2][5] = (pending || st > 0 || solos) ? 10 : 4;
		out[3][5] = arm == ARM_SAVE ? 15 : 4;
		out[4][5] = arm == ARM_DEL ? 15 : 4;
		out[5][5] = arm == ARM_COPY ? (copySrc >= 0 && fast ? 8 : 15) : 4;
		for (int s = 0; s < GM_SCENES; s++) {
			uint8_t v = sceneStored[s] ? 6 : 1;
			if (s == scene) v = 12;
			if (s == pendingScene) v = fast ? 15 : 3;
			if (s == copySrc) v = 15;
			out[s / 4][12 + s % 4] = v;
		}
		for (int t = 0; t < 4; t++) out[5][12 + t] = t == timing ? 15 : 4;
	}
};

} // namespace gridworks
