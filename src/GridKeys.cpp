// GridKeys: a Push-style note keyboard on the monome grid. Notes start at the
// bottom-left key and rise to the right; each row starts a set interval higher,
// so the same note turns up in several places. In Key or Chromatic, 26 Push
// scales, chord keys, strum, latch and a held sustain key. Columns 11-15 hold chord
// pads (shape, extensions, inversion, voicing) and a 10-slot chord memory that the
// SLOT/PLAY inputs can play.
#include "plugin.hpp"
#include "GridKeysCore.hpp"
#include <cctype>
#include "grid/GridModule.hpp"

using namespace gridworks;

// ---- LAYOUT (same numbers in res/make_panel_gridkeys.py) ----
static const float PANEL_W = 182.88f;
static const float CELL = 6.0f;
static const float GX0 = 31.0f, GY0 = 18.0f;
static const float STATUS_Y = 71.0f;
static const float KNOB_Y = 88.0f;
static const float X_ROOT = 12.0f, X_SCALE = 30.0f, X_OCT = 48.0f, X_WIDTH = 66.0f, X_ROW = 84.0f, X_GLIDE = 102.0f, X_STRUM = 120.0f;
static const float X_MODE = 140.0f, X_PLAYS = 158.0f;
static const float JACK_Y = 112.0f;
static const float X_CHORD = 11.0f, X_SHOW = 24.0f, X_SLOT = 41.0f, X_PLAY = 54.0f, X_LATCH = 70.0f, X_GRIDLIGHT = 82.0f;
static const float X_PVOCT = 98.0f, X_PGATE = 112.0f, X_MVOCT = 130.0f, X_MGATE = 144.0f, X_TRIG = 165.0f;

static const NVGcolor INK_COL = nvgRGB(0xEE, 0xE8, 0xEF);
static const NVGcolor SOFT_COL = nvgRGB(0x9A, 0x91, 0x9E);
static const NVGcolor ROOT_COL = nvgRGB(0xE6, 0xB4, 0x50);
static const NVGcolor SCALE_COL = nvgRGB(0xB9, 0xA9, 0xC9);
static const NVGcolor SHOW_COL = nvgRGB(0x7F, 0xD1, 0xC1);

static const int NO_NOTE = -999;

struct GridKeys : Module, GridModule {
	enum ParamId { ROOT_PARAM, SCALE_PARAM, OCTAVE_PARAM, WIDTH_PARAM, ROW_PARAM, GLIDE_PARAM, STRUM_PARAM,
		MODE_PARAM, PLAYS_PARAM, LATCH_PARAM, PARAMS_LEN };
	enum InputId { CHORD_INPUT, SHOW_INPUT, SLOT_INPUT, PLAY_INPUT, INPUTS_LEN };
	enum OutputId { PVOCT_OUTPUT, PGATE_OUTPUT, MVOCT_OUTPUT, MGATE_OUTPUT, TRIG_OUTPUT, OUTPUTS_LEN };
	enum LightId { LATCH_LIGHT, ENUMS(GRID_LIGHT, 2), LIGHTS_LEN };

	GridKeysCore core;
	dsp::PulseGenerator trigPulse;
	dsp::ClockDivider slowDiv;
	float monoV = 0.f;
	bool monoValid = false;
	dsp::SchmittTrigger playTrig;
	int lastCvWant = -1;
	std::atomic<bool> clearPadsReq{false}, clearMemReq{false};   // from the menu (UI thread)

	// options (menu)
	int polyphony = 8;
	bool faintOutside = false;

	// what the panel draws; written here on the audio thread, read by the UI
	uint8_t uiLv[8][16] = {};
	int16_t uiNote[8][16];
	int uiChordRoot = -1;    // -1 = CHORD input not in use
	int uiChordMask = 0;     // pitch classes above the chord root
	int uiBase = -12;        // bottom-left note
	bool uiClamped = false;
	int uiSelected = -1;
	int uiLast[12];
	int uiLastN = 0;

	GridKeys() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		std::vector<std::string> notes(NOTE_NAMES, NOTE_NAMES + 12);
		configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root note", notes);
		std::vector<std::string> scales;
		for (int i = 0; i < NUM_PUSH_SCALES; i++) scales.push_back(PUSH_SCALES[i].name);
		configSwitch(SCALE_PARAM, 0.f, NUM_PUSH_SCALES - 1, 0.f, "Scale", scales);
		configParam(OCTAVE_PARAM, 0.f, 7.f, 3.f, "Octave of the bottom-left key");
		getParamQuantity(OCTAVE_PARAM)->snapEnabled = true;
		configParam(WIDTH_PARAM, 4.f, (float) MAX_WIDTH, 8.f, "Width (columns of notes)");
		getParamQuantity(WIDTH_PARAM)->snapEnabled = true;
		configSwitch(ROW_PARAM, 0.f, 4.f, 0.f, "Each row starts",
			{"a 4th higher", "a 3rd higher", "a 5th higher", "an octave higher", "where the row below ends"});
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.f, "Glide (mono output)", " ms", 0.f, 1000.f);
		configParam(STRUM_PARAM, 0.f, 100.f, 0.f, "Strum (gap between the notes of a chord key)", " ms");
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Layout", {"In Key (scale notes only)", "Chromatic (all notes, scale lit)"});
		configSwitch(PLAYS_PARAM, 0.f, 2.f, 0.f, "Each key plays", {"One note", "A triad from the scale", "A 7th chord from the scale"});
		configSwitch(LATCH_PARAM, 0.f, 1.f, 0.f, "Latch (notes keep playing after you let go)", {"Off", "On"});
		configInput(CHORD_INPUT, "Chord (poly V/OCT: its notes become the scale, lowest note = root)");
		configInput(SHOW_INPUT, "Show (poly V/OCT: lights these notes on the grid)");
		configInput(SLOT_INPUT, "Chord memory slot (1 semitone per slot: C4 = slot 1 ... A4 = slot 10)");
		configInput(PLAY_INPUT, "Play the chord memory slot (gate; without it a SLOT change plays)");
		configOutput(PVOCT_OUTPUT, "Poly pitch (1 V/oct, 0 V = C4)");
		configOutput(PGATE_OUTPUT, "Poly gate");
		configOutput(MVOCT_OUTPUT, "Mono pitch (newest note, with glide)");
		configOutput(MGATE_OUTPUT, "Mono gate");
		configOutput(TRIG_OUTPUT, "Trigger on every new note");
		configLight(GRID_LIGHT, "Grid connected");
		slowDiv.setDivision(32);
		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 16; x++) uiNote[y][x] = NO_NOTE;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		core.allOff();
		core.octShift = 0;
		core.setLatch(false);
		core.setSustain(false);
		for (int p = 0; p < NUM_PADS; p++) core.padSticky[p] = false;
		for (int s = 0; s < NUM_SLOTS; s++) core.mem[s].clear();
		core.selected = -1;
		core.lastChord.clear();
		lastCvWant = -1;
		polyphony = 8;
		faintOutside = false;
	}

	int paramInt(int p) { return (int) std::round(params[p].getValue()); }

	void readInputs() {
		// CHORD: its notes become the scale
		Input& ci = inputs[CHORD_INPUT];
		int nc = ci.isConnected() ? ci.getChannels() : 0;
		if (nc > 0) {
			int lowest = 1 << 30, mask = 0;
			for (int c = 0; c < nc; c++) {
				int n = (int) std::round(ci.getVoltage(c) * 12.f);
				lowest = std::min(lowest, n);
			}
			int rootPc = ((lowest % 12) + 12) % 12;
			for (int c = 0; c < nc; c++) {
				int n = (int) std::round(ci.getVoltage(c) * 12.f);
				mask |= 1 << (((n - lowest) % 12 + 12) % 12);
			}
			if (!core.chordOverride || rootPc != uiChordRoot || mask != uiChordMask) {
				core.chordOverride = true;
				core.chordRoot = rootPc;
				core.chordIv.clear();
				for (int k = 0; k < 12; k++)
					if (mask & (1 << k)) core.chordIv.push_back(k);
				uiChordRoot = rootPc;
				uiChordMask = mask;
			}
		}
		else if (core.chordOverride) {
			core.chordOverride = false;
			uiChordRoot = -1;
			uiChordMask = 0;
		}
		// SHOW: notes to light
		core.showNotes.clear();
		Input& si = inputs[SHOW_INPUT];
		if (si.isConnected())
			for (int c = 0; c < si.getChannels(); c++) core.showNotes.push_back((int) std::round(si.getVoltage(c) * 12.f));
	}

	void applyAction(GKAction a) {
		switch (a) {
			case GK_OCT_UP:
				if (core.octave + core.octShift < 8) core.octShift++;
				break;
			case GK_OCT_DOWN:
				if (core.octave + core.octShift > 0) core.octShift--;
				break;
			case GK_LATCH: params[LATCH_PARAM].setValue(params[LATCH_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
			case GK_MODE: params[MODE_PARAM].setValue(params[MODE_PARAM].getValue() > 0.5f ? 0.f : 1.f); break;
			case GK_ROW: params[ROW_PARAM].setValue((float) ((paramInt(ROW_PARAM) + 1) % 5)); break;
			case GK_PLAYS: params[PLAYS_PARAM].setValue((float) ((paramInt(PLAYS_PARAM) + 1) % 3)); break;
			default: break;
		}
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;

		// knobs -> settings
		core.root = clamp(paramInt(ROOT_PARAM), 0, 11);
		core.scale = clamp(paramInt(SCALE_PARAM), 0, NUM_PUSH_SCALES - 1);
		core.octave = clamp(paramInt(OCTAVE_PARAM), 0, 7);
		core.octShift = clamp(core.octShift, -core.octave, 8 - core.octave);
		core.width = clamp(paramInt(WIDTH_PARAM), 4, MAX_WIDTH);
		core.rowMode = clamp(paramInt(ROW_PARAM), 0, 4);
		core.chromatic = params[MODE_PARAM].getValue() > 0.5f;
		core.plays = clamp(paramInt(PLAYS_PARAM), 0, 2);
		core.strum = params[STRUM_PARAM].getValue() * 1e-3f;
		core.setLatch(params[LATCH_PARAM].getValue() > 0.5f);
		int poly = clamp(polyphony, 1, 16);
		if (poly != core.polyphony) {
			core.polyphony = poly;
			core.allOff();
		}

		if (clearPadsReq.exchange(false))
			for (int p = 0; p < NUM_PADS; p++) core.padSticky[p] = false;
		if (clearMemReq.exchange(false)) {
			for (int s = 0; s < NUM_SLOTS; s++) core.mem[s].clear();
			core.selected = -1;
			core.cvPlay(-1);
			lastCvWant = -2;
		}
		bool slow = slowDiv.process();
		if (slow) readInputs();

		GridKeyEvent ev;
		while (!gridQueue.empty()) {
			ev = gridQueue.shift();
			applyAction(core.key(ev.x, ev.y, ev.down));
		}
		while (!screenQueue.empty()) {
			ev = screenQueue.shift();
			applyAction(core.key(ev.x, ev.y, ev.down));
		}
		// SLOT / PLAY inputs: play a chord memory slot
		{
			bool slotIn = inputs[SLOT_INPUT].isConnected(), playIn = inputs[PLAY_INPUT].isConnected();
			int slot = slotIn ? clamp((int) std::round(inputs[SLOT_INPUT].getVoltage() * 12.f), 0, NUM_SLOTS - 1) : 0;
			int want = -1;
			bool retrig = false;
			if (playIn) {
				retrig = playTrig.process(inputs[PLAY_INPUT].getVoltage(), 0.1f, 1.f);
				if (playTrig.isHigh()) want = slot;
			}
			else if (slotIn) want = slot;
			if (want != lastCvWant || retrig) {
				core.cvPlay(want);
				lastCvWant = want;
			}
		}
		core.advance(dt);

		// poly outputs
		outputs[PVOCT_OUTPUT].setChannels(poly);
		outputs[PGATE_OUTPUT].setChannels(poly);
		for (int c = 0; c < poly; c++) {
			outputs[PVOCT_OUTPUT].setVoltage(core.voices[c].note / 12.f, c);
			outputs[PGATE_OUTPUT].setVoltage(core.gate(c) ? 10.f : 0.f, c);
		}

		// mono output: newest note, with glide
		int mn;
		bool mOn = core.monoNote(mn);
		if (mOn) {
			float target = mn / 12.f;
			float glide = params[GLIDE_PARAM].getValue();
			if (!monoValid || glide < 1e-3f) monoV = target;
			else monoV += (target - monoV) * std::min(1.f, dt * 4.f / glide);
			monoValid = true;
		}
		outputs[MVOCT_OUTPUT].setVoltage(monoV);
		outputs[MGATE_OUTPUT].setVoltage(mOn ? 10.f : 0.f);

		if (core.triggerCount > 0) {
			core.triggerCount = 0;
			trigPulse.trigger(1e-3f);
		}
		outputs[TRIG_OUTPUT].setVoltage(trigPulse.process(dt) ? 10.f : 0.f);
		lights[LATCH_LIGHT].setBrightness(core.latch ? 1.f : 0.f);

		if (slow) {
			core.levels(uiLv, faintOutside);
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 16; x++) {
					int n;
					uiNote[y][x] = core.noteAt(x, y, n) ? (int16_t) n : (int16_t) NO_NOTE;
				}
			uiBase = core.baseNote();
			uiClamped = core.rowClamped();
			uiSelected = core.selected;
			uiLastN = std::min(12, (int) core.lastChord.size());
			for (int i = 0; i < uiLastN; i++) uiLast[i] = core.lastChord[i];
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "octShift", json_integer(core.octShift));
		json_object_set_new(root, "polyphony", json_integer(polyphony));
		json_object_set_new(root, "faintOutside", json_boolean(faintOutside));
		json_t* pads = json_array();
		for (int p = 0; p < NUM_PADS; p++)
			if (core.padSticky[p]) json_array_append_new(pads, json_string(PAD_NAMES[p]));
		json_object_set_new(root, "pads", pads);
		json_t* mem = json_array();
		for (int s = 0; s < NUM_SLOTS; s++) {
			json_t* a = json_array();
			for (int n : core.mem[s]) json_array_append_new(a, json_integer(n));
			json_array_append_new(mem, a);
		}
		json_object_set_new(root, "memory", mem);
		json_object_set_new(root, "selected", json_integer(core.selected));
		gridToJson(root);
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "octShift")) core.octShift = clamp((int) json_integer_value(j), -8, 8);
		if (json_t* j = json_object_get(root, "polyphony")) polyphony = clamp((int) json_integer_value(j), 1, 16);
		if (json_t* j = json_object_get(root, "faintOutside")) faintOutside = json_is_true(j);
		if (json_t* pads = json_object_get(root, "pads")) {
			for (int p = 0; p < NUM_PADS; p++) core.padSticky[p] = false;
			for (size_t i = 0; i < json_array_size(pads); i++) {
				const char* name = json_string_value(json_array_get(pads, i));
				for (int p = 0; name && p < NUM_PADS; p++)
					if (!std::strcmp(name, PAD_NAMES[p])) core.padSticky[p] = true;
			}
		}
		if (json_t* mem = json_object_get(root, "memory")) {
			for (int s = 0; s < NUM_SLOTS; s++) {
				core.mem[s].clear();
				json_t* a = json_array_get(mem, s);
				for (size_t i = 0; a && i < json_array_size(a) && i < 16; i++)
					core.mem[s].push_back(clamp((int) json_integer_value(json_array_get(a, i)), -60, 72));
			}
		}
		if (json_t* j = json_object_get(root, "selected")) core.selected = clamp((int) json_integer_value(j), -1, NUM_SLOTS - 1);
		gridFromJson(root);
	}
};

static std::string noteName(int semis) {   // semitones from C4 -> "D#3"
	int pc = ((semis % 12) + 12) % 12;
	int oct = 4 + (int) std::floor(semis / 12.0);
	return string::f("%s%d", NOTE_NAMES[pc], oct);
}

// The grid picture: the same lights as the hardware grid, with note names.
// Clicks act as key presses (press and release, so held notes work with the mouse).
struct GKGridDisplay : Widget {
	GridKeys* module = nullptr;
	int downX = -1, downY = -1;

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && module && e.action == GLFW_PRESS) {
			float mmX = e.pos.x / mm2px(1.f), mmY = e.pos.y / mm2px(1.f);
			int x = (int) std::floor(mmX / CELL), y = (int) std::floor(mmY / CELL);
			if (x >= 0 && x < 16 && y >= 0 && y < 8) {
				module->screenKey(x, y, true);
				downX = x;
				downY = y;
				e.consume(this);
				return;
			}
		}
		Widget::onButton(e);
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (module && downX >= 0) module->screenKey(downX, downY, false);
		downX = downY = -1;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		uint8_t lv[8][16];
		int16_t notes[8][16];
		int rootPc, base, chordRoot, chordMask;
		bool clamped;
		std::string mode;
		if (module) {
			std::memcpy(lv, module->uiLv, sizeof(lv));
			std::memcpy(notes, module->uiNote, sizeof(notes));
			chordRoot = module->uiChordRoot;
			chordMask = module->uiChordMask;
			rootPc = chordRoot >= 0 ? chordRoot : clamp(module->paramInt(GridKeys::ROOT_PARAM), 0, 11);
			base = module->uiBase;
			clamped = module->uiClamped;
		}
		else {   // module browser preview
			GridKeysCore c;
			c.levels(lv, false);
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 16; x++) { int n; notes[y][x] = c.noteAt(x, y, n) ? (int16_t) n : (int16_t) NO_NOTE; }
			rootPc = 0; base = c.baseNote(); chordRoot = -1; chordMask = 0; clamped = false;
		}

		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		float s = mm2px(1.f);
		for (int r = 0; r < 8; r++) {
			for (int k = 0; k < 16; k++) {
				float cx = (k * CELL + CELL / 2) * s, cy = (r * CELL + CELL / 2) * s;
				float half = (CELL / 2 - 0.45f) * s;
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, cx - half, cy - half, 2 * half, 2 * half, 0.7f * s);
				nvgFillColor(args.vg, nvgRGB(0x22, 0x1F, 0x25));
				nvgFill(args.vg);
				int v = lv[r][k];
				if (k >= 10 && k <= 14) {   // chord area: pads, memory keys, slots
					NVGcolor col;
					float a;
					std::string label;
					if (r < 5) {
						label = PAD_NAMES[r * 5 + k - 10];
						col = v >= 11 ? ROOT_COL : SCALE_COL;
						a = v >= 15 ? 1.f : v >= 11 ? 0.85f : v >= 7 ? 0.45f : 0.22f;
					}
					else if (r == 5) {
						label = MEMKEY_NAMES[k - 10];
						col = SHOW_COL;
						a = v >= 15 ? 0.95f : 0.32f;
					}
					else {
						label = string::f("%d", (r - 6) * 5 + k - 10 + 1);
						col = v >= 15 ? nvgRGB(0xFF, 0xFF, 0xFF) : ROOT_COL;
						a = v >= 15 ? 0.95f : v >= 10 ? 0.8f : v >= 6 ? 0.42f : 0.08f;
					}
					nvgBeginPath(args.vg);
					nvgRoundedRect(args.vg, cx - half, cy - half, 2 * half, 2 * half, 0.7f * s);
					nvgFillColor(args.vg, nvgTransRGBAf(col, a));
					nvgFill(args.vg);
					if (font) {
						nvgFontFaceId(args.vg, font->handle);
						nvgFontSize(args.vg, (label.size() > 3 ? 2.0f : 2.6f) * s);
						nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
						nvgFillColor(args.vg, a > 0.7f ? nvgRGB(0x12, 0x10, 0x14) : INK_COL);
						nvgText(args.vg, cx, cy + 0.1f * s, label.c_str(), NULL);
					}
					continue;
				}
				if (v <= 0) continue;
				int n = notes[r][k];
				NVGcolor col;
				float a;
				if (v >= 15) { col = nvgRGB(0xFF, 0xFF, 0xFF); a = 0.95f; }
				else if (n == NO_NOTE) { col = INK_COL; a = 0.15f + 0.05f * v; }        // control keys
				else if (v == 12) { col = SHOW_COL; a = 0.85f; }
				else if (v == 9) { col = ROOT_COL; a = 0.75f; }
				else if (v == 4) { col = SCALE_COL; a = 0.28f; }
				else { col = INK_COL; a = 0.08f; }
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, cx - half, cy - half, 2 * half, 2 * half, 0.7f * s);
				nvgFillColor(args.vg, nvgTransRGBAf(col, a));
				nvgFill(args.vg);
				if (font && n != NO_NOTE && v >= 4) {
					int pc = ((n % 12) + 12) % 12;
					bool isRoot = pc == rootPc;
					std::string label = isRoot ? noteName(n) : NOTE_NAMES[pc];
					nvgFontFaceId(args.vg, font->handle);
					nvgFontSize(args.vg, 2.6f * s);
					nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					nvgFillColor(args.vg, v >= 12 ? nvgRGB(0x12, 0x10, 0x14) : INK_COL);
					nvgText(args.vg, cx, cy + 0.1f * s, label.c_str(), NULL);
				}
			}
		}

		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 3.3f * s);
		float ty = (STATUS_Y - GY0 + 1.2f) * s;
		std::string left;
		if (chordRoot >= 0) {
			left = string::f("CHORD %s:", NOTE_NAMES[chordRoot]);
			for (int iv = 0; iv < 12; iv++)
				if (chordMask & (1 << iv)) left += std::string(" ") + NOTE_NAMES[(chordRoot + iv) % 12];
		}
		else if (module) {
			std::string sc = PUSH_SCALES[clamp(module->paramInt(GridKeys::SCALE_PARAM), 0, NUM_PUSH_SCALES - 1)].name;
			for (auto& ch : sc) ch = std::toupper(ch);
			left = string::f("%s %s", NOTE_NAMES[rootPc], sc.c_str());
		}
		else left = "C MAJOR";
		if (module) {
			left += module->params[GridKeys::MODE_PARAM].getValue() > 0.5f ? "  CHROM" : "  IN KEY";
			left += std::string("  ") + ROW_NAMES[clamp(module->paramInt(GridKeys::ROW_PARAM), 0, 4)];
			if (clamped) left += "=W";
			int plays = clamp(module->paramInt(GridKeys::PLAYS_PARAM), 0, 2);
			if (plays > 0) left += std::string("  ") + PLAYS_NAMES[plays];
			left += string::f("  W%d", clamp(module->paramInt(GridKeys::WIDTH_PARAM), 4, MAX_WIDTH));
		}
		else left += "  IN KEY  4TH  W8";
		left += "  " + noteName(base);
		if (module && module->uiLastN > 0) {
			left += "   LAST";
			for (int i = 0; i < module->uiLastN && i < 6; i++) left += " " + noteName(module->uiLast[i]);
			if (module->uiSelected >= 0) left += string::f("  SLOT %d", module->uiSelected + 1);
		}
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, INK_COL);
		nvgText(args.vg, -GX0 * s + 6.f * s, ty, left.c_str(), NULL);
		std::string right = module ? module->gridStatus() : "NO GRID";
		nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, SOFT_COL);
		nvgText(args.vg, (PANEL_W - 6.f - GX0) * s, ty, right.c_str(), NULL);
	}
};

struct GridKeysWidget : ModuleWidget {
	GridLinkUI link;

	GridKeysWidget(GridKeys* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/GridKeys.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		GKGridDisplay* disp = new GKGridDisplay;
		disp->module = module;
		disp->box.pos = mm2px(Vec(GX0, GY0));
		disp->box.size = mm2px(Vec(16 * CELL, 8 * CELL));
		addChild(disp);

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_ROOT, KNOB_Y)), module, GridKeys::ROOT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_SCALE, KNOB_Y)), module, GridKeys::SCALE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_OCT, KNOB_Y)), module, GridKeys::OCTAVE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_WIDTH, KNOB_Y)), module, GridKeys::WIDTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_ROW, KNOB_Y)), module, GridKeys::ROW_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_GLIDE, KNOB_Y)), module, GridKeys::GLIDE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_STRUM, KNOB_Y)), module, GridKeys::STRUM_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(X_MODE, KNOB_Y)), module, GridKeys::MODE_PARAM));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(X_PLAYS, KNOB_Y)), module, GridKeys::PLAYS_PARAM));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(mm2px(Vec(X_LATCH, JACK_Y)), module,
			GridKeys::LATCH_PARAM, GridKeys::LATCH_LIGHT));
		addChild(createLightCentered<MediumLight<GreenRedLight>>(mm2px(Vec(X_GRIDLIGHT, JACK_Y)), module, GridKeys::GRID_LIGHT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_CHORD, JACK_Y)), module, GridKeys::CHORD_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_SHOW, JACK_Y)), module, GridKeys::SHOW_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_SLOT, JACK_Y)), module, GridKeys::SLOT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_PLAY, JACK_Y)), module, GridKeys::PLAY_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_PVOCT, JACK_Y)), module, GridKeys::PVOCT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_PGATE, JACK_Y)), module, GridKeys::PGATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_MVOCT, JACK_Y)), module, GridKeys::MVOCT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_MGATE, JACK_Y)), module, GridKeys::MGATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_TRIG, JACK_Y)), module, GridKeys::TRIG_OUTPUT));
	}

	void step() override {
		ModuleWidget::step();
		GridKeys* m = dynamic_cast<GridKeys*>(module);
		if (!m) return;
		uint8_t lv[8][16];
		std::memcpy(lv, m->uiLv, sizeof(lv));
		bool live = link.update(m, lv);
		m->lights[GridKeys::GRID_LIGHT + 0].setBrightness(live ? 1.f : 0.f);
		m->lights[GridKeys::GRID_LIGHT + 1].setBrightness(!live && !m->wantedId.empty() ? 0.6f : 0.f);
	}

	void appendContextMenu(Menu* menu) override {
		GridKeys* m = dynamic_cast<GridKeys*>(module);
		if (!m) return;
		GridLinkUI::appendMenu(menu, m);

		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Polyphony (poly output channels)", string::f("%d", m->polyphony), [=](Menu* sub) {
			for (int n : {1, 2, 3, 4, 5, 6, 8, 10, 12, 16})
				sub->addChild(createCheckMenuItem(string::f("%d", n), "",
					[=]() { return m->polyphony == n; }, [=]() { m->polyphony = n; }));
		}));
		menu->addChild(createBoolPtrMenuItem("Dimly light notes outside the scale (Chromatic)", "", &m->faintOutside));
		menu->addChild(createMenuItem("Clear all chord pads", "", [=]() { m->clearPadsReq = true; }));
		menu->addChild(createMenuItem("Clear the chord memory", "", [=]() { m->clearMemReq = true; }));
		menu->addChild(createMenuItem("Reset the grid's octave shift", string::f("%+d", m->core.octShift),
			[=]() { m->core.octShift = 0; }));
	}
};

Model* modelGridKeys = createModel<GridKeys, GridKeysWidget>("GridKeys");
