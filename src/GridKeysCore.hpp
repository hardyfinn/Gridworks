// GridKeys brain: a Push-style note layout on the monome grid, voices, chord keys
// and strum. No Rack code in here, so it can be tested on its own (test/gk_test.cpp).
//
// Layout: the bottom-left key is the root in the starting octave. Notes go up one
// step per key to the right; each row starts a fixed interval above the row below.
// With the interval smaller than the row width, rows overlap, so a note appears in
// several places (exactly like Ableton Push).
//
// Columns 11-15 are the chord area (after the Hapax / OXI One chord modes):
// rows 1-5 modifier pads (shape, extensions, inversion, voicing), row 6 memory keys
// (SAVE DEL COPY PREV NEXT), rows 7-8 ten chord memory slots. Column 16 = controls.
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

// Chord pads, in grid order (row by row, 5 per row).
enum GKPad {
	P_MAJ, P_MIN, P_DIM, P_SUS2, P_SUS4,
	P_6, P_7, P_MAJ7, P_9, P_ADD9,
	P_11, P_13, P_AUG, P_PWR, P_RND,
	P_INV0, P_INV1, P_INV2, P_INV3, P_DRP2,
	P_OPEN, P_SPRD, P_BASS, P_TOP, P_RST,
	NUM_PADS
};
static const char* const PAD_NAMES[NUM_PADS] = {
	"MAJ", "MIN", "DIM", "SUS2", "SUS4",
	"6", "7", "MAJ7", "9", "ADD9",
	"11", "13", "AUG", "PWR", "RND",
	"INV0", "INV1", "INV2", "INV3", "DRP2",
	"OPEN", "SPRD", "BASS", "TOP+", "RST"};
// pads in the same group replace each other (0 = stands alone)
static inline int padGroup(int p) {
	switch (p) {
		case P_MAJ: case P_MIN: case P_DIM: case P_SUS2: case P_SUS4: case P_AUG: case P_PWR: return 1;
		case P_7: case P_MAJ7: return 2;
		case P_9: case P_11: case P_13: return 3;
		case P_INV0: case P_INV1: case P_INV2: case P_INV3: return 4;
		case P_DRP2: case P_OPEN: case P_SPRD: return 5;
		default: return 0;
	}
}
enum GKMemKey { M_SAVE, M_DEL, M_COPY, M_PREV, M_NEXT, NUM_MEMKEYS };
static const char* const MEMKEY_NAMES[NUM_MEMKEYS] = {"SAVE", "DEL", "COPY", "PREV", "NEXT"};
static const int NUM_SLOTS = 10;
static const int MAX_WIDTH = 10;
static const int CV_OWNER = 200;   // voices started by the SLOT/PLAY inputs

enum GKAction { GK_NONE, GK_OCT_UP, GK_OCT_DOWN, GK_LATCH, GK_MODE, GK_ROW, GK_PLAYS };

struct GridKeysCore {
	// ---- settings (set by the module from knobs) ----
	int root = 0;           // 0..11
	int scale = 0;          // PUSH_SCALES index
	int octave = 3;         // octave of the bottom-left key (3 = C3)
	int octShift = 0;       // grid OCT +/- keys
	bool chromatic = false;
	int width = 8;          // 4..10 columns of notes
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

	// chord pads: a tap toggles (sticky), holding applies only while held
	bool padSticky[NUM_PADS] = {};
	bool padHeld[NUM_PADS] = {};
	bool padUsed[NUM_PADS] = {};    // a note was played while the pad was held
	bool memHeld[NUM_MEMKEYS] = {};
	int copySrc = -1;

	// chord memory (exact notes, semitones from C4)
	std::vector<int> mem[NUM_SLOTS];
	int selected = -1;              // last slot played or saved
	std::vector<int> lastChord;     // last chord played, what SAVE stores
	int cvSlot = -1;                // slot the CV input is sounding
	uint32_t rng = 0x9E3779B9u;

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
	bool hasControls() const { return true; }

	// Note under a grid key (x 0..15, y 0..7 with 0 = top). False if it's not a note key.
	bool noteAt(int x, int y, int& out) const {
		if (x < 0 || x >= std::min(width, MAX_WIDTH) || y < 0 || y > 7) return false;
		int row = 7 - y;
		if (chromatic) out = baseNote() + x + row * rowStep();
		else out = baseNote() + degreeNote(x + row * rowStep());
		return true;
	}

	// ---------- chord pads ----------
	bool padOn(int p) const {
		if (padHeld[p]) return true;
		int g = padGroup(p);
		if (g)
			for (int q = 0; q < NUM_PADS; q++)
				if (q != p && padHeld[q] && padGroup(q) == g) return false;
		return padSticky[p];
	}
	void padPress(int p) {
		if (p == P_RST) {
			for (int q = 0; q < NUM_PADS; q++) padSticky[q] = false;
			return;
		}
		padHeld[p] = true;
		padUsed[p] = false;
	}
	void padRelease(int p) {
		if (!padHeld[p]) return;
		padHeld[p] = false;
		if (padUsed[p]) return;   // it was held while playing: momentary only
		bool on = !padSticky[p];
		int g = padGroup(p);
		if (on && g)
			for (int q = 0; q < NUM_PADS; q++)
				if (padGroup(q) == g) padSticky[q] = false;
		padSticky[p] = (p == P_INV0) ? false : on;   // INV0 just clears the inversion
	}
	void markPadsUsed() {
		for (int q = 0; q < NUM_PADS; q++)
			if (padHeld[q]) padUsed[q] = true;
	}
	int inversion() const {
		for (int k = 1; k <= 3; k++)
			if (padOn(P_INV0 + k)) return k;
		return 0;
	}
	uint32_t nextRand() {
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return rng;
	}

	// Scale degree of a note (counted from the bottom-left key), false if it's not in the scale.
	bool degreeOf(int note, int& d) const {
		int rel = note - baseNote();
		int oct = (int) std::floor(rel / 12.0);
		int pc = rel - 12 * oct;
		for (int k = 0; k < scaleLen(); k++)
			if (scaleIv(k) == pc) { d = oct * scaleLen() + k; return true; }
		return false;
	}
	int noteOfDegree(int d) const {
		int n = scaleLen();
		int o = (int) std::floor((double) d / n);
		return baseNote() + 12 * o + scaleIv(d - o * n);
	}

	// Notes a key plays: the note alone, or a chord built from PLAYS and the chord pads,
	// then voiced (inversion, drop 2, open, spread, bass, top up).
	std::vector<int> chordFor(int note) {
		static const int SHAPES[][3] = {{0, 4, 7}, {0, 3, 7}, {0, 3, 6}, {0, 2, 7}, {0, 5, 7}};
		int shape = -1;
		for (int p : {P_MAJ, P_MIN, P_DIM, P_SUS2, P_SUS4, P_AUG, P_PWR})
			if (padOn(p)) shape = p;
		bool e6 = padOn(P_6), e7 = padOn(P_7) || plays == 2, eM7 = padOn(P_MAJ7), add9 = padOn(P_ADD9);
		bool e9 = padOn(P_9), e11 = padOn(P_11), e13 = padOn(P_13);
		if (padOn(P_RND)) {
			switch (nextRand() % 7) {
				case 0: e6 = true; break;
				case 1: e7 = true; break;
				case 2: eM7 = true; break;
				case 3: e9 = true; break;
				case 4: add9 = true; break;
				case 5: e11 = true; break;
				default: e13 = true; break;
			}
		}
		bool anyExt = e6 || e7 || eM7 || add9 || e9 || e11 || e13;
		int d = 0;
		bool inKey = degreeOf(note, d);

		std::vector<int> v{note};
		bool build = shape >= 0 || (inKey && (plays > 0 || anyExt));
		if (build) {
			bool dia = shape < 0;   // no shape pad: follow the scale
			auto dg = [&](int k) { return noteOfDegree(d + k) - note; };
			if (shape == P_PWR) v.push_back(note + 7);
			else if (shape == P_AUG) { v.push_back(note + 4); v.push_back(note + 8); }
			else if (shape >= 0) { v.push_back(note + SHAPES[shape][1]); v.push_back(note + SHAPES[shape][2]); }
			else { v.push_back(note + dg(2)); v.push_back(note + dg(4)); }
			if (e9 || e11 || e13) e7 = e7 || !eM7;   // 9, 11 and 13 bring the 7th, like chord symbols
			if (eM7) v.push_back(note + 11);
			else if (e7) v.push_back(note + (dia ? dg(6) : shape == P_DIM ? 9 : 10));
			if (e6) v.push_back(note + (dia ? dg(5) : 9));
			if (e9 || e11 || e13 || add9) v.push_back(note + (dia ? dg(8) : 14));
			if (e11) v.push_back(note + (dia ? dg(10) : 17));
			if (e13) v.push_back(note + (dia ? dg(12) : 21));
		}
		std::sort(v.begin(), v.end());
		v.erase(std::unique(v.begin(), v.end()), v.end());

		// voicing
		int inv = std::min(inversion(), (int) v.size() - 1);
		for (int i = 0; i < inv; i++) {
			v.push_back(v.front() + 12);
			v.erase(v.begin());
		}
		int n = (int) v.size();
		if (padOn(P_DRP2) && n >= 3) v[n - 2] -= 12;
		else if (padOn(P_OPEN) && n >= 3) v[1] += 12;
		else if (padOn(P_SPRD))
			for (int i = 1; i < n; i++) v[i] += 12 * ((i + 1) / 2);
		std::sort(v.begin(), v.end());
		if (padOn(P_TOP) && n >= 2) v.back() += 12;
		if (padOn(P_BASS)) {
			int b = note - 12;
			while (b >= v.front()) b -= 12;
			v.push_back(b);
		}
		std::sort(v.begin(), v.end());
		v.erase(std::unique(v.begin(), v.end()), v.end());
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
	void releaseForce(int owner) {   // ignores latch and sustain
		for (auto& vc : voices)
			if (vc.owner == owner) { vc.on = vc.pending = false; vc.owner = -1; }
	}
	void allOff() {
		for (auto& vc : voices) { vc.on = vc.pending = false; vc.owner = -1; }
		cvSlot = -1;
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

	// ---------- chord memory ----------
	// Start a chord from a grid key (latch, strum and the held-key bookkeeping).
	void playFromKey(int x, int y, const std::vector<int>& notes) {
		if (latch && heldCount == 0) releaseLoose();   // the first key of a new chord replaces what was latched
		keyDown[y][x] = true;
		heldCount++;
		keyNotes[y][x] = notes;
		int owner = y * 16 + x;
		for (size_t i = 0; i < notes.size(); i++)
			noteOn(notes[i], owner, strum * i);
		lastChord = notes;
	}
	int stepSlot(int dir) const {   // next stored slot from the selected one, -1 if none
		for (int i = 1; i <= NUM_SLOTS; i++) {
			int s = ((selected < 0 ? (dir > 0 ? -1 : 0) : selected) + dir * i + 10 * NUM_SLOTS) % NUM_SLOTS;
			if (!mem[s].empty()) return s;
		}
		return -1;
	}
	void slotKey(int s, int x, int y) {
		if (memHeld[M_SAVE]) {
			if (!lastChord.empty()) { mem[s] = lastChord; selected = s; }
			return;
		}
		if (memHeld[M_DEL]) {
			mem[s].clear();
			if (selected == s) selected = -1;
			return;
		}
		if (memHeld[M_COPY]) {
			if (copySrc < 0) { if (!mem[s].empty()) copySrc = s; }
			else { mem[s] = mem[copySrc]; selected = s; copySrc = -1; }
			return;
		}
		markPadsUsed();
		if (mem[s].empty()) return;
		selected = s;
		playFromKey(x, y, mem[s]);
	}
	// SLOT/PLAY inputs: sound slot s (or nothing with -1), replacing what the CV was playing.
	void cvPlay(int s) {
		releaseForce(CV_OWNER);
		cvSlot = -1;
		if (s < 0 || s >= NUM_SLOTS || mem[s].empty()) return;
		cvSlot = s;
		selected = s;
		for (size_t i = 0; i < mem[s].size(); i++)
			noteOn(mem[s][i], CV_OWNER, strum * i);
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
			if (y == 5 && x >= 10 && x <= 14) memHeld[x - 10] = false;   // PREV / NEXT
			return GK_NONE;
		}
		if (!down && x == 15 && y == 7 && sustain) { setSustain(false); return GK_NONE; }
		// chord area
		if (x >= 10 && x <= 14) {
			int c = x - 10;
			if (y < 5) {
				if (down) padPress(y * 5 + c);
				else padRelease(y * 5 + c);
			}
			else if (y == 5) {
				memHeld[c] = down;
				if (c == M_COPY && !down) copySrc = -1;
				if (down && (c == M_PREV || c == M_NEXT)) {
					int s = stepSlot(c == M_NEXT ? 1 : -1);
					if (s >= 0) {
						markPadsUsed();
						selected = s;
						playFromKey(x, y, mem[s]);
					}
				}
			}
			else if (down && !keyDown[y][x]) slotKey((y - 6) * 5 + c, x, y);
			return GK_NONE;
		}
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
		if (down && !keyDown[y][x]) {
			markPadsUsed();
			playFromKey(x, y, chordFor(note));
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
		// chord area
		int inv = inversion();
		for (int p = 0; p < NUM_PADS; p++) {
			uint8_t v = padHeld[p] ? 15 : padOn(p) ? 11 : 3;
			if (p == P_INV0 && inv == 0 && !padHeld[p]) v = 7;
			out[p / 5][10 + p % 5] = v;
		}
		for (int k = 0; k < NUM_MEMKEYS; k++)
			out[5][10 + k] = (memHeld[k] || (k == M_COPY && copySrc >= 0)) ? 15 : 5;
		for (int s = 0; s < NUM_SLOTS; s++) {
			int x = 10 + s % 5, y = 6 + s / 5;
			uint8_t v = mem[s].empty() ? 1 : 6;
			if (s == selected) v = 10;
			if (keyDown[y][x] || s == cvSlot || s == copySrc) v = 15;
			out[y][x] = v;
		}
	}
};

} // namespace gridworks
