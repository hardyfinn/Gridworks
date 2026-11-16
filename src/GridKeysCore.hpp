// GridKeys brain: a Push-style note layout on the monome grid, voices, chord keys
// and strum. No Rack code in here, so it can be tested on its own (test/gk_test.cpp).
//
// Layout: the bottom-left key is the root in the starting octave. Notes go up one
// step per key to the right; each row starts a fixed interval above the row below.
// With the interval smaller than the row width, rows overlap, so a note appears in
// several places (exactly like Ableton Push).
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

namespace gridworks {

// Ableton Push's scale list (+ Chromatic).
struct PushScale {
	const char* name;
	int n;
	int iv[12];
};
static const PushScale PUSH_SCALES[] = {
	{"Major", 7, {0, 2, 4, 5, 7, 9, 11}},
	{"Minor", 7, {0, 2, 3, 5, 7, 8, 10}},
	{"Dorian", 7, {0, 2, 3, 5, 7, 9, 10}},
	{"Mixolydian", 7, {0, 2, 4, 5, 7, 9, 10}},
	{"Lydian", 7, {0, 2, 4, 6, 7, 9, 11}},
	{"Phrygian", 7, {0, 1, 3, 5, 7, 8, 10}},
	{"Locrian", 7, {0, 1, 3, 5, 6, 8, 10}},
	{"Diminished", 8, {0, 1, 3, 4, 6, 7, 9, 10}},
	{"Whole-half", 8, {0, 2, 3, 5, 6, 8, 9, 11}},
	{"Whole Tone", 6, {0, 2, 4, 6, 8, 10}},
	{"Minor Blues", 6, {0, 3, 5, 6, 7, 10}},
	{"Minor Pentatonic", 5, {0, 3, 5, 7, 10}},
	{"Major Pentatonic", 5, {0, 2, 4, 7, 9}},
	{"Harmonic Minor", 7, {0, 2, 3, 5, 7, 8, 11}},
	{"Melodic Minor", 7, {0, 2, 3, 5, 7, 9, 11}},
	{"Super Locrian", 7, {0, 1, 3, 4, 6, 8, 10}},
	{"Bhairav", 7, {0, 1, 4, 5, 7, 8, 11}},
	{"Hungarian Minor", 7, {0, 2, 3, 6, 7, 8, 11}},
	{"Minor Gypsy", 7, {0, 1, 4, 5, 7, 8, 10}},
	{"Hirojoshi", 5, {0, 2, 3, 7, 8}},
	{"In-Sen", 5, {0, 1, 5, 7, 10}},
	{"Iwato", 5, {0, 1, 5, 6, 10}},
	{"Kumoi", 5, {0, 2, 3, 7, 9}},
	{"Pelog", 6, {0, 1, 3, 4, 7, 8}},
	{"Spanish", 8, {0, 1, 3, 4, 5, 6, 8, 10}},
	{"Chromatic", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
};
static const int NUM_PUSH_SCALES = sizeof(PUSH_SCALES) / sizeof(PUSH_SCALES[0]);

static const char* const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char* const ROW_NAMES[5] = {"4TH", "3RD", "5TH", "OCT", "SEQ"};
static const char* const PLAYS_NAMES[3] = {"NOTE", "TRIAD", "7TH"};

struct GKVoice {
	int note = 0;        // semitones from C4
	bool on = false;
	bool pending = false;   // waiting for its strum delay
	float delay = 0.f;
	int retrig = 0;      // samples of forced low gate before a reused channel goes high
	uint32_t age = 0;
	int owner = -1;      // key index that started it (y * 16 + x), -1 = latched/sustained
};

enum GKAction { GK_NONE, GK_OCT_UP, GK_OCT_DOWN, GK_LATCH, GK_MODE, GK_ROW, GK_PLAYS };

struct GridKeysCore {
	// ---- settings (set by the module from knobs) ----
	int root = 0;           // 0..11
	int scale = 0;          // PUSH_SCALES index
	int octave = 3;         // octave of the bottom-left key (3 = C3)
	int octShift = 0;       // grid OCT +/- keys
	bool chromatic = false;
	int width = 8;          // 4..16 columns of notes
	int rowMode = 0;        // ROW_NAMES
	int plays = 0;          // PLAYS_NAMES: one key = note / triad / 7th
	bool latch = false;
	int polyphony = 8;
	float strum = 0.f;      // seconds between the notes of a chord key

	// CHORD input: when set, its notes become the scale (lowest note = root)
	bool chordOverride = false;
	int chordRoot = 0;
	std::vector<int> chordIv;

	// SHOW input: notes to light (semitones from C4)
	std::vector<int> showNotes;

	// ---- state ----
	GKVoice voices[16];
	uint32_t clockAge = 0;
	bool sustain = false;          // grid sustain key held
	bool keyDown[8][16] = {};
	std::vector<int> keyNotes[8][16];   // the notes each held key started
	int heldCount = 0;
	int triggerCount = 0;          // new notes since the module last looked (for TRIG)

	// ---------- scale ----------
	int scaleLen() const { return chordOverride ? (int) chordIv.size() : PUSH_SCALES[scale].n; }
	int scaleIv(int k) const { return chordOverride ? chordIv[k] : PUSH_SCALES[scale].iv[k]; }
	int rootPc() const { return chordOverride ? chordRoot : root; }
	int degreeNote(int d) const {   // semitones above the root for scale degree d (d >= 0)
		int n = scaleLen();
		return 12 * (d / n) + scaleIv(d % n);
	}
	bool inScale(int semis) const {   // semis from C4
		int pc = ((semis - rootPc()) % 12 + 12) % 12;
		for (int k = 0; k < scaleLen(); k++)
			if (scaleIv(k) == pc) return true;
		return false;
	}
	int baseNote() const {   // bottom-left key, semitones from C4
		return rootPc() + 12 * (octave + octShift - 4);
	}

	// Interval between rows, in scale steps (In Key) or semitones (Chromatic).
	// Never wider than the row, or notes would be skipped.
	int rowStep() const {
		int s;
		if (chromatic) {
			static const int SEMIS[4] = {5, 4, 7, 12};
			s = rowMode < 4 ? SEMIS[rowMode] : width;
		}
		else {
			if (rowMode == 0) s = 3;
			else if (rowMode == 1) s = 2;
			else if (rowMode == 2) s = 4;
			else if (rowMode == 3) s = scaleLen();
			else s = width;
		}
		return std::min(s, width);
	}
	bool rowClamped() const {
		int want;
		if (chromatic) { static const int SEMIS[4] = {5, 4, 7, 12}; want = rowMode < 4 ? SEMIS[rowMode] : width; }
		else want = rowMode == 0 ? 3 : rowMode == 1 ? 2 : rowMode == 2 ? 4 : rowMode == 3 ? scaleLen() : width;
		return want > width;
	}
	bool hasControls() const { return width < 16; }

	// Note under a grid key (x 0..15, y 0..7 with 0 = top). False if it's not a note key.
	bool noteAt(int x, int y, int& out) const {
		if (x < 0 || x >= width || y < 0 || y > 7) return false;
		if (hasControls() && x == 15) return false;
		int row = 7 - y;
		if (chromatic) out = baseNote() + x + row * rowStep();
		else out = baseNote() + degreeNote(x + row * rowStep());
		return true;
	}

	// Notes a key plays: the note, or a chord stacked in thirds from the scale.
	std::vector<int> chordFor(int note) const {
		std::vector<int> v{note};
		if (plays == 0) return v;
		// find the scale degree of this note
		int rel = note - baseNote();
		int oct = (int) std::floor(rel / 12.0);
		int pc = rel - 12 * oct;
		int deg = -1;
		for (int k = 0; k < scaleLen(); k++)
			if (scaleIv(k) == pc) deg = k;
		if (deg < 0) return v;   // out of scale (Chromatic mode): just the note
		int d = oct * scaleLen() + deg;
		int count = plays == 1 ? 3 : 4;
		v.clear();
		for (int i = 0; i < count; i++) {
			int dd = d + 2 * i;
			int o = (int) std::floor((double) dd / scaleLen());
			v.push_back(baseNote() + 12 * o + scaleIv(dd - o * scaleLen()));
		}
		return v;
	}

	// ---------- voices ----------
	int findVoice() {
		int n = std::max(1, std::min(16, polyphony));
		int best = -1;
		for (int i = 0; i < n; i++)
			if (!voices[i].on && !voices[i].pending && (best < 0 || voices[i].age < voices[best].age)) best = i;
		if (best >= 0) return best;
		for (int i = 0; i < n; i++)   // steal the oldest
			if (best < 0 || voices[i].age < voices[best].age) best = i;
		return best;
	}
	void noteOn(int note, int owner, float delay) {
		int v = findVoice();
		GKVoice& vc = voices[v];
		bool wasOn = vc.on;
		vc.note = note;
		vc.owner = owner;
		vc.age = ++clockAge;
		if (delay > 0.f) {
			vc.on = false;
			vc.pending = true;
			vc.delay = delay;
		}
		else {
			vc.on = true;
			vc.pending = false;
			vc.retrig = wasOn ? 48 : 0;
			triggerCount++;
		}
	}
	void releaseOwner(int owner) {
		for (auto& vc : voices) {
			if (vc.owner != owner || (!vc.on && !vc.pending)) continue;
			if (latch || sustain) { vc.owner = -1; continue; }
			vc.on = false;
			vc.pending = false;
		}
	}
	void releaseLoose() {   // notes left on by latch or sustain
		for (auto& vc : voices) {
			if (vc.owner == -1 && (vc.on || vc.pending)) vc.on = vc.pending = false;
		}
	}
	void allOff() {
		for (auto& vc : voices) { vc.on = vc.pending = false; vc.owner = -1; }
		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 16; x++) { keyDown[y][x] = false; keyNotes[y][x].clear(); }
		heldCount = 0;
	}
	void setLatch(bool on) {
		if (latch && !on) {
			latch = false;
			if (!sustain) releaseLoose();
		}
		latch = on;
	}
	void setSustain(bool on) {
		sustain = on;
		if (!on && !latch) releaseLoose();
	}

	// ---------- keys ----------
	GKAction key(int x, int y, bool down) {
		if (x < 0 || x > 15 || y < 0 || y > 7) return GK_NONE;
		// a release always ends what the press started, even if the layout changed meanwhile
		if (!down && keyDown[y][x]) {
			keyDown[y][x] = false;
			heldCount = std::max(0, heldCount - 1);
			releaseOwner(y * 16 + x);
			keyNotes[y][x].clear();
			return GK_NONE;
		}
		if (!down && x == 15 && y == 7 && sustain) { setSustain(false); return GK_NONE; }
		if (hasControls() && x == 15) {
			if (y == 7) { setSustain(down); return GK_NONE; }
			if (!down) return GK_NONE;
			switch (y) {
				case 0: return GK_OCT_UP;
				case 1: return GK_OCT_DOWN;
				case 2: return GK_LATCH;
				case 3: return GK_MODE;
				case 4: return GK_ROW;
				case 5: return GK_PLAYS;
				default: return GK_NONE;
			}
		}
		int note;
		if (!noteAt(x, y, note)) return GK_NONE;
		int owner = y * 16 + x;
		if (down) {
			if (keyDown[y][x]) return GK_NONE;
			// latch: the first key of a new chord replaces what was latched
			if (latch && heldCount == 0) releaseLoose();
			keyDown[y][x] = true;
			heldCount++;
			std::vector<int> notes = chordFor(note);
			keyNotes[y][x] = notes;
			for (size_t i = 0; i < notes.size(); i++)
				noteOn(notes[i], owner, strum * i);
		}
		return GK_NONE;
	}

	// Time passes (strum delays and retrigger gaps). dt in seconds; one call per sample.
	void advance(float dt) {
		for (auto& vc : voices) {
			if (vc.retrig > 0) vc.retrig--;
			if (vc.pending) {
				vc.delay -= dt;
				if (vc.delay <= 0.f) {
					vc.pending = false;
					vc.on = true;
					triggerCount++;
				}
			}
		}
	}

	bool gate(int ch) const { return voices[ch].on && voices[ch].retrig == 0; }
	// Mono output: the newest sounding voice (so releasing a key falls back to the one before).
	bool monoNote(int& note) const {
		int best = -1;
		for (int i = 0; i < 16; i++)
			if (voices[i].on && (best < 0 || voices[i].age > voices[best].age)) best = i;
		if (best < 0) return false;
		note = voices[best].note;
		return true;
	}
	bool sounding(int note) const {
		for (const auto& vc : voices)
			if ((vc.on || vc.pending) && vc.note == note) return true;
		return false;
	}

	// ---------- LEDs (0..15), out[y][x], y = 0 top ----------
	void levels(uint8_t out[8][16], bool faintOutside) const {
		std::memset(out, 0, 8 * 16);
		for (int y = 0; y < 8; y++) {
			for (int x = 0; x < 16; x++) {
				int note;
				if (!noteAt(x, y, note)) continue;
				uint8_t v;
				int pc = ((note - rootPc()) % 12 + 12) % 12;
				if (sounding(note)) v = 15;
				else if (std::find(showNotes.begin(), showNotes.end(), note) != showNotes.end()) v = 12;
				else if (pc == 0) v = 9;
				else if (inScale(note)) v = 4;
				else v = faintOutside ? 1 : 0;
				out[y][x] = v;
			}
		}
		if (hasControls()) {
			out[0][15] = (uint8_t) std::min(15, 4 + 3 * std::max(0, octShift));
			out[1][15] = (uint8_t) std::min(15, 4 + 3 * std::max(0, -octShift));
			out[2][15] = latch ? 15 : 4;
			out[3][15] = chromatic ? 15 : 4;
			out[4][15] = (uint8_t) (4 + 2 * rowMode);
			out[5][15] = (uint8_t) (4 + 5 * plays);
			out[7][15] = sustain ? 15 : 4;
		}
	}
};

} // namespace gridworks
