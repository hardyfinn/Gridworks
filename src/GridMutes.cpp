// GridMutes: 18 clocked performance mutes for audio, CV or triggers, played from a
// monome grid or the grid picture on the panel. Cue & lock, solo, A/B toggle pairs,
// stereo pairs, invert, 16 scenes (CV selectable), click-free fades and a submix of
// every channel whose output isn't patched.
#include "plugin.hpp"
#include "GridMutesCore.hpp"
#include "grid/GridModule.hpp"

using namespace gridworks;

// ---- LAYOUT (same numbers in res/make_panel_gridmutes.py) ----
static const float PANEL_W = 208.28f;
static const float IN_X[3] = {10.6f, 21.1f, 31.6f};
static const float OUT_X[3] = {47.8f, 58.3f, 68.8f};
static const float CH_Y0 = 26.0f, CH_DY = 11.6f;
static const float LIGHT_DX = 4.3f, LIGHT_DY = -4.6f;
static const float CELL = 7.5f;
static const float GX0 = 83.4f, GY0 = 17.0f;
static const float STATUS_Y = 83.0f;
static const float JACK_Y = 111.0f;
static const float X_CLOCK = 12.0f, X_RESET = 26.0f, X_SCENE = 44.0f, X_TRIG = 58.0f;
static const float X_FADE = 145.0f, X_GRIDLIGHT = 167.0f, X_MIX = 195.0f;

static const NVGcolor INK_COL = nvgRGB(0xEE, 0xE8, 0xEF);
static const NVGcolor SOFT_COL = nvgRGB(0x9A, 0x91, 0x9E);
static const NVGcolor CH_COL = nvgRGB(0xE6, 0xB4, 0x50);
static const NVGcolor FN_COL = nvgRGB(0xB9, 0xA9, 0xC9);
static const NVGcolor EDIT_COL = nvgRGB(0x8F, 0xB3, 0xE8);
static const NVGcolor ACT_COL = nvgRGB(0x6F, 0xE0, 0x8A);
static const NVGcolor SCENE_COL = nvgRGB(0xE0, 0x8A, 0x6B);
static const NVGcolor TIME_COL = nvgRGB(0x7F, 0xD1, 0xC1);

struct GridMutes : Module, GridModule {
	enum ParamId { FADE_PARAM, PARAMS_LEN };
	enum InputId { ENUMS(IN_INPUT, GM_CH), CLOCK_INPUT, RESET_INPUT, SCENE_INPUT, TRIG_INPUT, INPUTS_LEN };
	enum OutputId { ENUMS(OUT_OUTPUT, GM_CH), MIX_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(MUTE_LIGHT, GM_CH), ENUMS(GRID_LIGHT, 2), LIGHTS_LEN };

	GridMutesCore core;
	float gain[GM_CH] = {};
	float env[GM_CH] = {};
	dsp::SchmittTrigger clockTrig, resetTrig, sceneTrig;
	dsp::ClockDivider slowDiv;
	int lastCvScene = -1;
	bool firstRun = true;
	std::atomic<bool> clearScenesReq{false};

	// what the panel draws (written on the audio thread)
	uint8_t uiLv[8][16] = {};
	bool uiAudible[GM_CH] = {};
	int uiScene = -1, uiTiming = T_BEAT, uiOn = GM_CH, uiMode = MODE_MUTE, uiArm = ARM_NONE;
	int uiStaged = 0, uiCopySrc = -1, uiFirstPick = -1;
	bool uiCue = false, uiClocked = false, uiPending = false;

	GridMutes() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(FADE_PARAM, 1.f, 50.f, 5.f, "Fade (how long a mute takes, so it never clicks)", " ms");
		for (int c = 0; c < GM_CH; c++) {
			configInput(IN_INPUT + c, string::f("Channel %d", c + 1));
			configOutput(OUT_OUTPUT + c, string::f("Channel %d (unpatched = goes to MIX)", c + 1));
			configLight(MUTE_LIGHT + c, string::f("Channel %d playing", c + 1));
		}
		configInput(CLOCK_INPUT, "Clock (16th notes; change in the menu). Without it, mutes change at once");
		configInput(RESET_INPUT, "Reset (the next clock is beat 1 of bar 1)");
		configInput(SCENE_INPUT, "Scene CV (1 semitone per scene: C4 = scene 1 ... D#5 = scene 16)");
		configInput(TRIG_INPUT, "Scene trigger (recalls the CV's scene; without CV, the next stored scene)");
		configOutput(MIX_OUTPUT, "Mix of every channel whose OUT isn't patched");
		configLight(GRID_LIGHT, "Grid connected");
		slowDiv.setDivision(32);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		core = GridMutesCore();
		lastCvScene = -1;
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;
		if (firstRun) {   // start without a fade from silence
			for (int c = 0; c < GM_CH; c++) gain[c] = core.audible(c) ? 1.f : 0.f;
			firstRun = false;
		}
		if (clearScenesReq.exchange(false)) {
			for (int s = 0; s < GM_SCENES; s++) core.sceneStored[s] = false;
			core.scene = -1;
		}

		GridKeyEvent ev;
		while (!gridQueue.empty()) { ev = gridQueue.shift(); core.key(ev.x, ev.y, ev.down); }
		while (!screenQueue.empty()) { ev = screenQueue.shift(); core.key(ev.x, ev.y, ev.down); }

		// clock and reset
		bool clocked = inputs[CLOCK_INPUT].isConnected();
		if (clocked != core.clocked) core.setClocked(clocked);
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) core.reset();
		if (clocked && clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) core.clockPulse();

		// scene CV / trigger
		bool cvIn = inputs[SCENE_INPUT].isConnected(), trigIn = inputs[TRIG_INPUT].isConnected();
		int cvScene = cvIn ? clamp((int) std::round(inputs[SCENE_INPUT].getVoltage() * 12.f), 0, GM_SCENES - 1) : -1;
		if (trigIn) {
			if (sceneTrig.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f)) {
				if (cvIn) core.recall(cvScene);
				else core.nextScene();
			}
		}
		else if (cvIn && cvScene != lastCvScene) core.recall(cvScene);
		lastCvScene = cvScene;
		core.advance(dt);

		// audio: fade each channel towards on/off, sum unpatched ones into MIX
		float step = dt / (params[FADE_PARAM].getValue() * 1e-3f);
		float decay = std::exp(-dt / 0.3f);
		float mix = 0.f;
		for (int c = 0; c < GM_CH; c++) {
			float target = core.audible(c) ? 1.f : 0.f;
			if (gain[c] < target) gain[c] = std::min(target, gain[c] + step);
			else if (gain[c] > target) gain[c] = std::max(target, gain[c] - step);
			Input& in = inputs[IN_INPUT + c];
			Output& out = outputs[OUT_OUTPUT + c];
			int n = in.getChannels();
			float peak = 0.f;
			for (int k = 0; k < n; k++) {
				float v = in.getVoltage(k);
				peak = std::max(peak, std::fabs(v));
				float g = v * gain[c];
				if (out.isConnected()) out.setVoltage(g, k);
				else mix += g;
			}
			out.setChannels(std::max(1, n));
			if (n == 0) out.setVoltage(0.f);
			env[c] = std::max(peak, env[c] * decay);
			lights[MUTE_LIGHT + c].setBrightness(gain[c]);
		}
		outputs[MIX_OUTPUT].setVoltage(mix);

		if (slowDiv.process()) {
			float level[GM_CH];
			for (int c = 0; c < GM_CH; c++) level[c] = env[c] < 0.01f ? 0.f : std::min(1.f, 0.25f + env[c] / 6.6f);
			core.levels(uiLv, level);
			int nOn = 0;
			for (int c = 0; c < GM_CH; c++) { uiAudible[c] = core.audible(c); nOn += uiAudible[c]; }
			uiOn = nOn;
			uiScene = core.scene;
			uiTiming = core.timing;
			uiMode = core.mode;
			uiArm = core.arm;
			uiStaged = core.stagedCount();
			uiCopySrc = core.copySrc;
			uiFirstPick = core.firstPick;
			uiCue = core.cue;
			uiClocked = core.clocked;
			uiPending = core.pending;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		auto boolArr = [](const bool* b, int n) {
			json_t* a = json_array();
			for (int i = 0; i < n; i++) json_array_append_new(a, json_boolean(b[i]));
			return a;
		};
		auto intArr = [](const int* v, int n) {
			json_t* a = json_array();
			for (int i = 0; i < n; i++) json_array_append_new(a, json_integer(v[i]));
			return a;
		};
		json_object_set_new(root, "on", boolArr(core.tgtOn, GM_CH));
		json_object_set_new(root, "solo", boolArr(core.tgtSolo, GM_CH));
		json_object_set_new(root, "ab", intArr(core.ab, GM_CH));
		json_object_set_new(root, "pair", intArr(core.pair, GM_CH));
		json_t* scenes = json_array();
		for (int s = 0; s < GM_SCENES; s++)
			json_array_append_new(scenes, core.sceneStored[s] ? boolArr(core.scenes[s], GM_CH) : json_null());
		json_object_set_new(root, "scenes", scenes);
		json_object_set_new(root, "scene", json_integer(core.scene));
		json_object_set_new(root, "timing", json_integer(core.timing));
		json_object_set_new(root, "pulsesPerBeat", json_integer(core.pulsesPerBeat));
		json_object_set_new(root, "beatsPerBar", json_integer(core.beatsPerBar));
		gridToJson(root);
		return root;
	}

	void dataFromJson(json_t* root) override {
		auto readBools = [](json_t* a, bool* b, int n) {
			for (int i = 0; a && i < n && i < (int) json_array_size(a); i++) b[i] = json_is_true(json_array_get(a, i));
		};
		readBools(json_object_get(root, "on"), core.tgtOn, GM_CH);
		readBools(json_object_get(root, "solo"), core.tgtSolo, GM_CH);
		for (const char* key : {"ab", "pair"}) {
			json_t* a = json_object_get(root, key);
			int* dst = std::string(key) == "ab" ? core.ab : core.pair;
			for (int i = 0; a && i < GM_CH && i < (int) json_array_size(a); i++)
				dst[i] = clamp((int) json_integer_value(json_array_get(a, i)), -1, GM_CH - 1);
		}
		if (json_t* scenes = json_object_get(root, "scenes")) {
			for (int s = 0; s < GM_SCENES; s++) {
				json_t* a = json_array_get(scenes, s);
				core.sceneStored[s] = a && json_is_array(a);
				if (core.sceneStored[s]) readBools(a, core.scenes[s], GM_CH);
			}
		}
		auto geti = [&](const char* k, int def) { json_t* j = json_object_get(root, k); return j ? (int) json_integer_value(j) : def; };
		core.scene = clamp(geti("scene", -1), -1, GM_SCENES - 1);
		core.timing = clamp(geti("timing", T_BEAT), 0, 3);
		core.pulsesPerBeat = clamp(geti("pulsesPerBeat", 4), 1, 24);
		core.beatsPerBar = clamp(geti("beatsPerBar", 4), 2, 7);
		core.apply();
		firstRun = true;
		gridFromJson(root);
	}
};

// The grid picture: the same lights as the hardware grid, with labels. Clicks are key presses.
struct GMGridDisplay : Widget {
	GridMutes* module = nullptr;

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && module && e.action == GLFW_PRESS) {
			float mmX = e.pos.x / mm2px(1.f), mmY = e.pos.y / mm2px(1.f);
			int x = (int) std::floor(mmX / CELL), y = (int) std::floor(mmY / CELL);
			if (x >= 0 && x < 16 && y >= 0 && y < 8) {
				module->screenKey(x, y, true);
				module->screenKey(x, y, false);
				e.consume(this);
				return;
			}
		}
		Widget::onButton(e);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		uint8_t lv[8][16];
		bool aud[GM_CH];
		if (module) {
			std::memcpy(lv, module->uiLv, sizeof(lv));
			std::memcpy(aud, module->uiAudible, sizeof(aud));
		}
		else {
			GridMutesCore c;
			c.levels(lv, nullptr);
			for (int i = 0; i < GM_CH; i++) aud[i] = true;
		}
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		float s = mm2px(1.f);
		for (int r = 0; r < 8; r++) {
			for (int k = 0; k < 16; k++) {
				float cx = (k * CELL + CELL / 2) * s, cy = (r * CELL + CELL / 2) * s;
				float half = (CELL / 2 - 0.5f) * s;
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, cx - half, cy - half, 2 * half, 2 * half, 0.8f * s);
				nvgFillColor(args.vg, nvgRGB(0x22, 0x1F, 0x25));
				nvgFill(args.vg);
				int v = lv[r][k];
				NVGcolor col = INK_COL;
				std::string label;
				bool area = true;
				if (k <= 2 && r <= 5) { col = CH_COL; label = string::f("%d", k * 6 + r + 1); }
				else if (k == 4 && r <= 5) { col = FN_COL; label = GM_FN_NAMES[0][r]; }
				else if (k == 5 && r <= 5) { col = EDIT_COL; label = GM_FN_NAMES[1][r]; }
				else if (k >= 7 && k <= 9 && r <= 5) { col = aud[(k - 7) * 6 + r] ? ACT_COL : SOFT_COL; }
				else if (k >= 12 && r <= 3) { col = SCENE_COL; label = string::f("%d", r * 4 + k - 12 + 1); }
				else if (k >= 12 && r == 5) { col = TIME_COL; label = GM_TIME_KEYS[k - 12]; }
				else area = false;
				if (!area) continue;
				float a = v <= 0 ? 0.f : 0.08f + 0.87f * v / 15.f;
				if (a > 0.f) {
					nvgBeginPath(args.vg);
					nvgRoundedRect(args.vg, cx - half, cy - half, 2 * half, 2 * half, 0.8f * s);
					nvgFillColor(args.vg, nvgTransRGBAf(col, a));
					nvgFill(args.vg);
				}
				if (font && !label.empty()) {
					nvgFontFaceId(args.vg, font->handle);
					nvgFontSize(args.vg, (label.size() > 3 ? 2.4f : 3.0f) * s);
					nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					nvgFillColor(args.vg, a > 0.6f ? nvgRGB(0x14, 0x12, 0x15) : INK_COL);
					nvgText(args.vg, cx, cy + 0.1f * s, label.c_str(), NULL);
				}
			}
		}

		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 3.3f * s);
		float ty = (STATUS_Y - GY0 + 1.2f) * s;
		std::string left;
		if (module) {
			GridMutes* m = module;
			if (m->uiArm == ARM_SAVE) left = "SAVE: TAP A SCENE";
			else if (m->uiArm == ARM_DEL) left = "DELETE: TAP A SCENE";
			else if (m->uiArm == ARM_COPY) left = m->uiCopySrc < 0 ? "COPY: TAP THE SCENE TO COPY" : string::f("COPY SCENE %d: TAP WHERE IT GOES", m->uiCopySrc + 1);
			else if (m->uiMode == MODE_SOLO) left = "SOLO: TAP CHANNELS, SOLO AGAIN TO LEAVE";
			else if (m->uiMode == MODE_AB) left = m->uiFirstPick < 0 ? "A/B: TAP TWO CHANNELS" : string::f("A/B: %d + TAP ANOTHER", m->uiFirstPick + 1);
			else if (m->uiMode == MODE_PAIR) left = "PAIR: TAP A CHANNEL (PAIRS WITH THE NEXT)";
			else {
				left = m->uiScene >= 0 ? string::f("SCENE %d  ", m->uiScene + 1) : "";
				left += m->uiClocked ? GM_TIMING_NAMES[m->uiTiming] : "NO CLOCK: NOW";
				left += string::f("  %d ON  %d MUTED", m->uiOn, GM_CH - m->uiOn);
				if (m->uiCue) left += string::f("  CUE %d", m->uiStaged);
				if (m->uiPending) left += "  WAITING";
			}
		}
		else left = "NEXT BEAT  18 ON  0 MUTED";
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, INK_COL);
		nvgText(args.vg, -1.5f * s, ty, left.c_str(), NULL);
		std::string right = module ? module->gridStatus() : "NO GRID";
		nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, SOFT_COL);
		nvgText(args.vg, (PANEL_W - 6.f - GX0) * s, ty, right.c_str(), NULL);
	}
};

struct GridMutesWidget : ModuleWidget {
	GridLinkUI link;

	GridMutesWidget(GridMutes* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/GridMutes.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		GMGridDisplay* disp = new GMGridDisplay;
		disp->module = module;
		disp->box.pos = mm2px(Vec(GX0, GY0));
		disp->box.size = mm2px(Vec(16 * CELL, 8 * CELL));
		addChild(disp);

		for (int c = 0; c < GM_CH; c++) {
			float y = CH_Y0 + (c % 6) * CH_DY;
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(IN_X[c / 6], y)), module, GridMutes::IN_INPUT + c));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[c / 6], y)), module, GridMutes::OUT_OUTPUT + c));
			addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(OUT_X[c / 6] + LIGHT_DX, y + LIGHT_DY)), module, GridMutes::MUTE_LIGHT + c));
		}
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_CLOCK, JACK_Y)), module, GridMutes::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RESET, JACK_Y)), module, GridMutes::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_SCENE, JACK_Y)), module, GridMutes::SCENE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_TRIG, JACK_Y)), module, GridMutes::TRIG_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_FADE, JACK_Y)), module, GridMutes::FADE_PARAM));
		addChild(createLightCentered<MediumLight<GreenRedLight>>(mm2px(Vec(X_GRIDLIGHT, JACK_Y)), module, GridMutes::GRID_LIGHT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_MIX, JACK_Y)), module, GridMutes::MIX_OUTPUT));
	}

	void step() override {
		ModuleWidget::step();
		GridMutes* m = dynamic_cast<GridMutes*>(module);
		if (!m) return;
		uint8_t lv[8][16];
		std::memcpy(lv, m->uiLv, sizeof(lv));
		bool live = link.update(m, lv);
		m->lights[GridMutes::GRID_LIGHT + 0].setBrightness(live ? 1.f : 0.f);
		m->lights[GridMutes::GRID_LIGHT + 1].setBrightness(!live && !m->wantedId.empty() ? 0.6f : 0.f);
	}

	void appendContextMenu(Menu* menu) override {
		GridMutes* m = dynamic_cast<GridMutes*>(module);
		if (!m) return;
		GridLinkUI::appendMenu(menu, m);
		menu->addChild(new MenuSeparator);
		static const int PPB[4] = {4, 2, 1, 24};
		menu->addChild(createIndexSubmenuItem("Clock input", {"16th notes", "8th notes", "Quarter notes", "24 PPQN"},
			[=]() { for (int i = 0; i < 4; i++) if (m->core.pulsesPerBeat == PPB[i]) return i; return 0; },
			[=](int i) { m->core.pulsesPerBeat = PPB[i]; }));
		menu->addChild(createIndexSubmenuItem("Beats per bar", {"2", "3", "4", "5", "6", "7"},
			[=]() { return m->core.beatsPerBar - 2; }, [=](int i) { m->core.beatsPerBar = i + 2; }));
		menu->addChild(createMenuItem("Clear all scenes", "", [=]() { m->clearScenesReq = true; }));
	}
};

Model* modelGridMutes = createModel<GridMutes, GridMutesWidget>("GridMutes");
