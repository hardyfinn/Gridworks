// GridDrums brain: patterns, slots, mutes, key handling and the LED picture.
// No Rack code in here, so it can be tested on its own (test/core_test.cpp).
//
// Grid map (16 wide x 8 tall, x = column 0..15, y = row 0..7):
//   rows 0-5   tracks 1-6, one key per step
//   row 6      keys 0-5 mute/unmute tracks 1-6
//              keys 12-13 CLEAR (hold + step-row key = clear that track,
//                                hold + slot key = clear that slot)
//              keys 14-15 COPY  (hold + slot key = copy the playing pattern there)
//   row 7      16 memory slots (press = play this slot from the next bar)
#pragma once
#include <cstdint>
#include <cstring>

namespace gridworks {

struct GridDrumsCore {
	static const int TRACKS = 6;
	static const int STEPS = 16;
	static const int SLOTS = 16;
	static const int MUTE_ROW = 6;
	static const int SLOT_ROW = 7;

	uint16_t pat[SLOTS][TRACKS];   // bit s = step s+1 is on
	int cur = 0;                   // slot playing now
	int queued = -1;               // slot waiting for the next bar, -1 = none
	int step = -1;                 // step playing now, -1 = waiting for first clock
	bool mute[TRACKS] = {};

	int clearHeld = 0, copyHeld = 0;          // hardware keys held down
	bool clearArmed = false, copyArmed = false;   // on-screen latch (mouse can't hold two keys)

	GridDrumsCore() {
		clearAll();
		starterBeat();
	}

	void clearAll() {
		std::memset(pat, 0, sizeof(pat));
	}

	void starterBeat() {
		pat[0][0] = 0x1111;   // kick on 1 5 9 13
		pat[0][1] = 0x1010;   // snare on 5 13
		pat[0][2] = 0x5555;   // closed hat on every odd step
		pat[0][3] = 0x4444;   // open hat on the off-beats (3 7 11 15)
	}

	bool slotHasData(int s) const {
		for (int t = 0; t < TRACKS; t++)
			if (pat[s][t]) return true;
		return false;
	}
	bool isOn(int t, int s) const { return (pat[cur][t] >> s) & 1; }
	bool clearActive() const { return clearHeld > 0 || clearArmed; }
	bool copyActive() const { return copyHeld > 0 || copyArmed; }

	// Choose a slot. immediate = switch now (clock stopped, or "change at once" mode).
	void requestSlot(int s, bool immediate) {
		if (s < 0 || s >= SLOTS) return;
		if (immediate) {
			cur = s;
			queued = -1;
		}
		else {
			queued = (s == cur) ? -1 : s;
		}
	}

	// A clock pulse arrived. Returns which tracks fire (bit t = track t+1).
	int tick() {
		step = (step + 1) % STEPS;
		if (step == 0 && queued >= 0) {
			cur = queued;
			queued = -1;
		}
		int mask = 0;
		for (int t = 0; t < TRACKS; t++)
			if (!mute[t] && isOn(t, step)) mask |= 1 << t;
		return mask;
	}

	void reset() {
		step = -1;
		// a reset is a new bar too, so a waiting slot starts now
		if (queued >= 0) {
			cur = queued;
			queued = -1;
		}
	}

	// A key on the grid (fromScreen = false) or a click on the panel (true).
	void key(int x, int y, bool down, bool fromScreen, bool immediateSlots) {
		if (x < 0 || x >= STEPS || y < 0 || y > SLOT_ROW) return;
		if (y == MUTE_ROW && (x == 12 || x == 13)) {
			if (fromScreen) {
				if (down) { clearArmed = !clearArmed; copyArmed = false; }
			}
			else clearHeld += down ? 1 : (clearHeld > 0 ? -1 : 0);
			return;
		}
		if (y == MUTE_ROW && (x == 14 || x == 15)) {
			if (fromScreen) {
				if (down) { copyArmed = !copyArmed; clearArmed = false; }
			}
			else copyHeld += down ? 1 : (copyHeld > 0 ? -1 : 0);
			return;
		}
		if (!down) return;
		if (y < TRACKS) {
			if (clearActive()) {
				pat[cur][y] = 0;
				clearArmed = false;
			}
			else {
				pat[cur][y] ^= (uint16_t) (1u << x);
			}
		}
		else if (y == MUTE_ROW) {
			if (x < TRACKS) mute[x] = !mute[x];
		}
		else if (y == SLOT_ROW) {
			if (copyActive()) {
				if (x != cur) std::memcpy(pat[x], pat[cur], sizeof(pat[cur]));
				copyArmed = false;
			}
			else if (clearActive()) {
				std::memset(pat[x], 0, sizeof(pat[x]));
				clearArmed = false;
			}
			else {
				requestSlot(x, immediateSlots);
			}
		}
	}

	// LED brightness 0..15 for each key. blink toggles about 4 times a second;
	// flash[t] is true just after track t fired.
	void levels(uint8_t out[8][16], bool blink, const bool* flash) const {
		std::memset(out, 0, 8 * 16);
		for (int t = 0; t < TRACKS; t++) {
			for (int s = 0; s < STEPS; s++) {
				bool on = isOn(t, s);
				uint8_t v = on ? (mute[t] ? 4 : 10) : 0;
				if (s == step) v = on ? (mute[t] ? 7 : 15) : 3;
				out[t][s] = v;
			}
		}
		for (int t = 0; t < TRACKS; t++)
			out[MUTE_ROW][t] = mute[t] ? 2 : ((flash && flash[t]) ? 15 : 8);
		out[MUTE_ROW][12] = out[MUTE_ROW][13] = clearActive() ? 15 : 4;
		out[MUTE_ROW][14] = out[MUTE_ROW][15] = copyActive() ? 15 : 4;
		for (int s = 0; s < SLOTS; s++) {
			uint8_t v = slotHasData(s) ? 6 : 2;
			if (s == queued) v = blink ? 15 : 5;
			if (s == cur) v = 15;
			out[SLOT_ROW][s] = v;
		}
	}
};

} // namespace gridworks
