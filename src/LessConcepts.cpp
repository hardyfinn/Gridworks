// LessConcepts: cellular-automaton melody sequencer for two voices, after the
// norns script "less concepts" by dan derks and linus schrab. Played from a
// monome grid (same key map as the script) or the grid picture on the panel.
// CV only: two V/OCT + GATE pairs, a SEED CV and a STEP trigger.
#include "plugin.hpp"
#include "LessConceptsCore.hpp"
#include "grid/GridModule.hpp"

using namespace gridworks;

// ---- LAYOUT (same numbers in res/make_panel_lessconcepts.py) ----
static const float CELL = 6.0f;
static const float GX0 = 23.0f, GY0 = 18.0f;
static const float STATUS_Y = 71.0f;
static const float KNOB_Y = 88.0f;
static const float X_SEED = 12.0f, X_RULE = 30.0f, X_SCALE = 52.0f, X_TRANSP = 72.0f, X_PROB1 = 94.0f, X_PROB2 = 114.0f;
static const float X_GRIDLIGHT = 132.0f;
static const float JACK_Y = 111.0f;
static const float X_CLOCK = 42.0f, X_RESET = 54.0f, X_SEEDCV = 12.0f, X_RULECV = 30.0f;
static const float X_VOCT1 = 68.5f, X_GATE1 = 81.5f, X_VOCT2 = 97.0f, X_GATE2 = 110.0f, X_SEEDOUT = 122.5f, X_STEP = 134.0f;

static const NVGcolor V_COL[2] = {nvgRGB(0x7F, 0xD1, 0xC1), nvgRGB(0xF0, 0xA8, 0x68)};
static const NVGcolor INK_COL = nvgRGB(0xE8, 0xEC, 0xEF);

struct ScaleQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int i = clamp((int) std::round(getValue()), 0, NUM_SCALES - 1);
		return SCALES[i].name;
	}
};

struct LessConcepts : Module, GridModule {
	enum ParamId { SEED_PARAM, RULE_PARAM, SCALE_PARAM, TRANSPOSE_PARAM, PROB1_PARAM, PROB2_PARAM, PARAMS_LEN };
	enum InputId { CLOCK_INPUT, RESET_INPUT, SEED_INPUT, RULE_INPUT, INPUTS_LEN };
	enum OutputId { VOCT1_OUTPUT, GATE1_OUTPUT, VOCT2_OUTPUT, GATE2_OUTPUT, SEED_OUTPUT, STEP_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(GRID_LIGHT, 2), LIGHTS_LEN };

	LessConceptsCore core;

	dsp::SchmittTrigger clockTrig, resetTrig;
	dsp::PulseGenerator stepPulse;
	float gateTime[2] = {};
	float pitch[2] = {};
	float flash[2] = {};

	// clock: the input is turned into 96 ticks per quarter note
	int ticksPerPulse = 24;      // 96 / input PPQN (24 = one pulse per 16th)
	float sinceEdge = 0.f;
	float period = 0.125f;       // seconds between clock pulses (measured)
	int ticksDone = 0;           // ticks given since the last pulse
	bool haveEdge = false;
	float resetHold = 0.f;
	float timerAcc = 0.f;

	// options
	int clockMode = 0;   // 0 = 16ths, 1 = 8ths, 2 = quarters, 3 = 24 PPQN
	int gateMode = 1;    // 0 = trigger, 1 = half step, 2 = full step

	// knob/CV tracking, so a knob move or a CV change is applied once
	int lastSeedIn = -1, lastRuleIn = -1;

	LessConcepts() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(SEED_PARAM, 0.f, 255.f, 36.f, "Seed (the 8 bits it starts from)");
		configParam(RULE_PARAM, 0.f, 255.f, 30.f, "Rule (how the bits change each step)");
		configParam<ScaleQuantity>(SCALE_PARAM, 0.f, NUM_SCALES - 1, 0.f, "Scale");
		configParam(TRANSPOSE_PARAM, -24.f, 24.f, 0.f, "Transpose", " semitones");
		configParam(PROB1_PARAM, 0.f, 100.f, 100.f, "Voice 1 gate probability", "%");
		configParam(PROB2_PARAM, 0.f, 100.f, 100.f, "Voice 2 gate probability", "%");
		for (int p : {SEED_PARAM, RULE_PARAM, SCALE_PARAM, TRANSPOSE_PARAM, PROB1_PARAM, PROB2_PARAM})
			getParamQuantity(p)->snapEnabled = true;
		configInput(CLOCK_INPUT, "Clock (16th notes; change in the menu)");
		configInput(RESET_INPUT, "Reset (back to the start seed; next clock plays)");
		configInput(SEED_INPUT, "Seed CV (0-10 V covers 0-255, added to the knob)");
		configInput(RULE_INPUT, "Rule CV (0-10 V covers 0-255, added to the knob)");
		configOutput(VOCT1_OUTPUT, "Voice 1 pitch (1 V/oct, 0 V = C4)");
		configOutput(GATE1_OUTPUT, "Voice 1 gate");
		configOutput(VOCT2_OUTPUT, "Voice 2 pitch (1 V/oct, 0 V = C4)");
		configOutput(GATE2_OUTPUT, "Voice 2 gate");
		configOutput(SEED_OUTPUT, "Seed (the 8 bits as 0-10 V)");
		configOutput(STEP_OUTPUT, "Step trigger");
		configLight(GRID_LIGHT, "Grid connected");
		core.rng.state = (uint32_t) (random::u32() | 1u);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		uint32_t st = core.rng.state;
		core = LessConceptsCore();
		core.rng.state = st;
		lastSeedIn = lastRuleIn = -1;
	}

	void onRandomize(const RandomizeEvent& e) override {
		core.randomizeAll();   // like grid key (16,3); scale and probabilities stay
		syncKnobs();
	}

	int knobPlusCv(int param, int input) {
		float v = params[param].getValue();
		if (inputs[input].isConnected()) v += inputs[input].getVoltage() * 25.5f;
		return clamp((int) std::round(v), 0, 255);
	}

	// Move the SEED/RULE knobs to follow changes made on the grid (only when no CV is patched).
	void syncKnobs() {
		if (!inputs[SEED_INPUT].isConnected() && (int) params[SEED_PARAM].getValue() != core.newSeed) {
			params[SEED_PARAM].setValue(core.newSeed);
			lastSeedIn = core.newSeed;
		}
		if (!inputs[RULE_INPUT].isConnected() && (int) params[RULE_PARAM].getValue() != core.rule) {
			params[RULE_PARAM].setValue(core.rule);
			lastRuleIn = core.rule;
		}
	}

	void doTick(float stepSec) {
		LCStep st = core.tick();
		if (!st.stepped) return;
		stepPulse.trigger(1e-3f);
		for (int i = 0; i < 2; i++) {
			if (!st.gate[i]) continue;
			pitch[i] = st.pitch[i];
			flash[i] = 0.1f;
			if (gateMode == 0) gateTime[i] = 5e-3f;
			else if (gateMode == 1) gateTime[i] = std::max(2e-3f, 0.5f * stepSec);
			else gateTime[i] = std::max(2e-3f, stepSec - 1e-3f);   // tiny gap so envelopes retrigger
		}
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;
		GridKeyEvent ev;
		while (!gridQueue.empty()) {
			ev = gridQueue.shift();
			core.key(ev.x, ev.y, ev.down, false);
		}
		while (!screenQueue.empty()) {
			ev = screenQueue.shift();
			core.key(ev.x, ev.y, ev.down, true);
		}
		timerAcc += dt;
		if (timerAcc > 0.005f) {
			core.advanceTime(timerAcc);
			timerAcc = 0.f;
		}

		// knobs and CV
		int s = knobPlusCv(SEED_PARAM, SEED_INPUT);
		if (s != lastSeedIn) { lastSeedIn = s; if (s != core.newSeed) core.setSeed(s); }
		int r = knobPlusCv(RULE_PARAM, RULE_INPUT);
		if (r != lastRuleIn) { lastRuleIn = r; if (r != core.rule) core.setRule(r); }
		core.scale = clamp((int) std::round(params[SCALE_PARAM].getValue()), 0, NUM_SCALES - 1);
		core.transpose = (int) std::round(params[TRANSPOSE_PARAM].getValue());
		core.gateProb[0] = (int) std::round(params[PROB1_PARAM].getValue());
		core.gateProb[1] = (int) std::round(params[PROB2_PARAM].getValue());

		// reset
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			core.reset();
			ticksDone = 0;
			haveEdge = false;
			resetHold = 1e-3f;
		}
		resetHold -= dt;

		// clock -> 96 PPQN ticks: the pulse itself is a tick, and the ticks in between
		// are spread out using the time between the last two pulses
		static const int TPP[4] = {24, 48, 96, 4};
		ticksPerPulse = TPP[clamp(clockMode, 0, 3)];
		float stepSec = period / ticksPerPulse * (float) (96.0 / LessConceptsCore::timeDivision(core.timeRange, core.timeSel));
		sinceEdge += dt;
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f) && resetHold <= 0.f) {
			if (haveEdge) {
				if (sinceEdge > 1e-3f && sinceEdge < 4.f) period = sinceEdge;
				while (ticksDone < ticksPerPulse - 1) { doTick(stepSec); ticksDone++; }
			}
			doTick(stepSec);
			ticksDone = 0;
			sinceEdge = 0.f;
			haveEdge = true;
		}
		else if (haveEdge && sinceEdge < 4.f * period) {
			int target = std::min(ticksPerPulse - 1, (int) (sinceEdge / period * ticksPerPulse));
			while (ticksDone < target) { doTick(stepSec); ticksDone++; }
		}

		syncKnobs();

		for (int i = 0; i < 2; i++) {
			bool g = gateTime[i] > 0.f;
			gateTime[i] -= dt;
			if (flash[i] > 0.f) flash[i] -= dt;
			outputs[i == 0 ? GATE1_OUTPUT : GATE2_OUTPUT].setVoltage(g ? 10.f : 0.f);
			outputs[i == 0 ? VOCT1_OUTPUT : VOCT2_OUTPUT].setVoltage(pitch[i]);
		}
		outputs[SEED_OUTPUT].setVoltage(core.seed / 25.5f);
		outputs[STEP_OUTPUT].setVoltage(stepPulse.process(dt) ? 10.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		const LessConceptsCore& c = core;
		json_object_set_new(root, "seed", json_integer(c.newSeed));
		json_object_set_new(root, "rule", json_integer(c.rule));
		json_object_set_new(root, "bit1", json_integer(c.bit[0]));
		json_object_set_new(root, "bit2", json_integer(c.bit[1]));
		json_object_set_new(root, "oct1", json_integer(c.octave[0]));
		json_object_set_new(root, "oct2", json_integer(c.octave[1]));
		json_object_set_new(root, "low", json_integer(c.low));
		json_object_set_new(root, "high", json_integer(c.high));
		json_object_set_new(root, "timeSel", json_integer(c.timeSel));
		json_object_set_new(root, "timeRange", json_integer(c.timeRange));
		json_object_set_new(root, "duration", json_integer(c.defaultDuration));
		json_object_set_new(root, "tranAmount1", json_integer(c.tranAmount[0]));
		json_object_set_new(root, "tranAmount2", json_integer(c.tranAmount[1]));
		json_object_set_new(root, "tranProb1", json_integer(c.tranProb[0]));
		json_object_set_new(root, "tranProb2", json_integer(c.tranProb[1]));
		json_object_set_new(root, "octMin", json_integer(c.octMin));
		json_object_set_new(root, "octMax", json_integer(c.octMax));
		json_object_set_new(root, "snapCount", json_integer(c.snapCount));
		json_object_set_new(root, "selected", json_integer(c.selected));
		json_object_set_new(root, "cycleDir", json_integer(c.cycleDir));
		json_object_set_new(root, "destructive", json_boolean(c.destructive));
		json_t* snaps = json_array();
		for (int i = 1; i <= c.snapCount; i++) {
			const LCSnapshot& p = c.snaps[i];
			json_t* a = json_array();
			for (int v : {p.seed, p.rule, p.bit[0], p.bit[1], p.low, p.high, p.octave[0], p.octave[1], p.timeSel, p.duration})
				json_array_append_new(a, json_integer(v));
			json_array_append_new(snaps, a);
		}
		json_object_set_new(root, "snapshots", snaps);
		json_object_set_new(root, "clockMode", json_integer(clockMode));
		json_object_set_new(root, "gateMode", json_integer(gateMode));
		gridToJson(root);
		return root;
	}

	void dataFromJson(json_t* root) override {
		LessConceptsCore& c = core;
		auto geti = [&](const char* k, int def) {
			json_t* j = json_object_get(root, k);
			return j ? (int) json_integer_value(j) : def;
		};
		c.timeRange = clamp(geti("timeRange", 0), 0, 2);
		c.newSeed = c.seed = geti("seed", 36) & 255;
		c.rule = geti("rule", 30) & 255;
		c.bit[0] = clamp(geti("bit1", 1), 0, 8);
		c.bit[1] = clamp(geti("bit2", 8), 0, 8);
		c.octave[0] = clamp(geti("oct1", 0), -3, 3);
		c.octave[1] = clamp(geti("oct2", 0), -3, 3);
		c.low = clamp(geti("low", 1), 1, 32);
		c.high = clamp(geti("high", 14), 1, 32);
		c.timeSel = geti("timeSel", 3);
		c.clampTime();
		c.defaultDuration = clamp(geti("duration", 4), 1, 32);
		c.tranAmount[0] = clamp(geti("tranAmount1", 12), -24, 24);
		c.tranAmount[1] = clamp(geti("tranAmount2", 12), -24, 24);
		c.tranProb[0] = clamp(geti("tranProb1", 0), 0, 100);
		c.tranProb[1] = clamp(geti("tranProb2", 0), 0, 100);
		c.octMin = clamp(geti("octMin", -2), -3, 3);
		c.octMax = clamp(geti("octMax", 2), -3, 3);
		if (json_t* snaps = json_object_get(root, "snapshots")) {
			c.snapCount = 0;
			for (size_t i = 0; i < json_array_size(snaps) && i < 16; i++) {
				json_t* a = json_array_get(snaps, i);
				auto at = [&](size_t k, int def) { json_t* j = json_array_get(a, k); return j ? (int) json_integer_value(j) : def; };
				LCSnapshot& p = c.snaps[i + 1];
				p.seed = at(0, 36) & 255; p.rule = at(1, 30) & 255;
				p.bit[0] = clamp(at(2, 1), 0, 8); p.bit[1] = clamp(at(3, 8), 0, 8);
				p.low = clamp(at(4, 1), 1, 32); p.high = clamp(at(5, 14), 1, 32);
				p.octave[0] = clamp(at(6, 0), -3, 3); p.octave[1] = clamp(at(7, 0), -3, 3);
				p.timeSel = at(8, 3); p.duration = clamp(at(9, 4), 1, 32);
				c.snapCount++;
			}
		}
		c.selected = clamp(geti("selected", 0), 0, c.snapCount);
		c.cycleDir = clamp(geti("cycleDir", 0), 0, 3);
		if (json_t* j = json_object_get(root, "destructive")) c.destructive = json_is_true(j);
		if (c.snapCount == 0) { c.cycleDir = 0; c.destructive = false; }
		clockMode = clamp(geti("clockMode", 0), 0, 3);
		gateMode = clamp(geti("gateMode", 1), 0, 2);
		c.bang();
		lastSeedIn = lastRuleIn = -1;
		gridFromJson(root);
	}
};

// The grid picture: the same lights as the hardware grid; clicks act as key presses
// (press and release, so the hold keys work with the mouse too).
struct LCGridDisplay : Widget {
	LessConcepts* module = nullptr;
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
		LessConceptsCore preview;
		const LessConceptsCore& c = module ? module->core : preview;
		uint8_t lv[8][16];
		bool fired[2] = {true, true};
		c.levels(lv, fired);
		float s = mm2px(1.f);
		for (int r = 0; r < 8; r++) {
			for (int k = 0; k < 16; k++) {
				float cx = (k * CELL + CELL / 2) * s, cy = (r * CELL + CELL / 2) * s;
				float half = (CELL / 2 - 0.45f) * s;
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, cx - half, cy - half, 2 * half, 2 * half, 0.7f * s);
				nvgFillColor(args.vg, (k % 8 == 0 || r >= 3) ? nvgRGB(0x26, 0x2A, 0x30) : nvgRGB(0x20, 0x23, 0x28));
				nvgFill(args.vg);
				float a = lv[r][k] / 15.f;
				if (a > 0.f) {
					NVGcolor base = (r < 2 && k < 9) ? V_COL[r] : INK_COL;
					nvgBeginPath(args.vg);
					nvgRoundedRect(args.vg, cx - half, cy - half, 2 * half, 2 * half, 0.7f * s);
					nvgFillColor(args.vg, nvgTransRGBAf(base, 0.1f + 0.9f * a));
					nvgFill(args.vg);
				}
			}
		}

		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 3.3f * s);
		float ty = (STATUS_Y - GY0 + 1.2f) * s;
		std::string left = string::f("SEED %3d  RULE %3d  %s  %s", c.newSeed, c.rule,
			LessConceptsCore::timeName(c.timeRange, c.timeSel), SCALES[c.scale].name);
		if (c.snapCount > 0) left += string::f("  SNAP %d/%d %s", c.selected, c.snapCount, c.cycleName());
		if (c.armLow) left = "LOW: PRESS A NOTE (ROWS 4-5)";
		if (c.armHigh) left = "HIGH: PRESS A NOTE (ROWS 4-5)";
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, INK_COL);
		nvgText(args.vg, -GX0 * s + 6.f * s, ty, left.c_str(), NULL);
		std::string right = module ? module->gridStatus() : "NO GRID";
		nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, nvgRGB(0x8E, 0x97, 0x9F));
		nvgText(args.vg, (142.24f - 6.f - GX0) * s, ty, right.c_str(), NULL);
	}
};

struct LessConceptsWidget : ModuleWidget {
	GridLinkUI link;

	LessConceptsWidget(LessConcepts* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/LessConcepts.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		LCGridDisplay* disp = new LCGridDisplay;
		disp->module = module;
		disp->box.pos = mm2px(Vec(GX0, GY0));
		disp->box.size = mm2px(Vec(16 * CELL, 8 * CELL));
		addChild(disp);

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_SEED, KNOB_Y)), module, LessConcepts::SEED_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_RULE, KNOB_Y)), module, LessConcepts::RULE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_SCALE, KNOB_Y)), module, LessConcepts::SCALE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_TRANSP, KNOB_Y)), module, LessConcepts::TRANSPOSE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_PROB1, KNOB_Y)), module, LessConcepts::PROB1_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_PROB2, KNOB_Y)), module, LessConcepts::PROB2_PARAM));
		addChild(createLightCentered<MediumLight<GreenRedLight>>(mm2px(Vec(X_GRIDLIGHT, KNOB_Y)), module, LessConcepts::GRID_LIGHT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_CLOCK, JACK_Y)), module, LessConcepts::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RESET, JACK_Y)), module, LessConcepts::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_SEEDCV, JACK_Y)), module, LessConcepts::SEED_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RULECV, JACK_Y)), module, LessConcepts::RULE_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_VOCT1, JACK_Y)), module, LessConcepts::VOCT1_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_GATE1, JACK_Y)), module, LessConcepts::GATE1_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_VOCT2, JACK_Y)), module, LessConcepts::VOCT2_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_GATE2, JACK_Y)), module, LessConcepts::GATE2_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_SEEDOUT, JACK_Y)), module, LessConcepts::SEED_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_STEP, JACK_Y)), module, LessConcepts::STEP_OUTPUT));
	}

	void step() override {
		ModuleWidget::step();
		LessConcepts* m = dynamic_cast<LessConcepts*>(module);
		if (!m) return;
		uint8_t lv[8][16];
		bool fired[2] = {true, true};
		m->core.levels(lv, fired);
		bool live = link.update(m, lv);
		m->lights[LessConcepts::GRID_LIGHT + 0].setBrightness(live ? 1.f : 0.f);
		m->lights[LessConcepts::GRID_LIGHT + 1].setBrightness(!live && !m->wantedId.empty() ? 0.6f : 0.f);
	}

	void appendContextMenu(Menu* menu) override {
		LessConcepts* m = dynamic_cast<LessConcepts*>(module);
		if (!m) return;
		GridLinkUI::appendMenu(menu, m);
		LessConceptsCore* c = &m->core;

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Clock input",
			{"16th notes", "8th notes", "Quarter notes", "24 PPQN"}, &m->clockMode));
		menu->addChild(createIndexSubmenuItem("Time range (grid keys 1-3 on row 8)",
			{"Legacy 1/8 - 1/32", "Slow 1/1 - 1/16", "Full 2/1 - 1/32"},
			[=]() { return c->timeRange; }, [=](int i) { c->setTimeRange(i); }));
		menu->addChild(createIndexPtrSubmenuItem("Gate length", {"Trigger (5 ms)", "Half a step", "Whole step"}, &m->gateMode));

		static const std::vector<int> AMOUNTS = {-24, -12, -7, -5, 5, 7, 12, 24};
		static const std::vector<int> PROBS = {0, 10, 25, 50, 75, 100};
		for (int v = 0; v < 2; v++) {
			menu->addChild(createSubmenuItem(string::f("Voice %d note jump", v + 1), "", [=](Menu* sub) {
				sub->addChild(createMenuLabel("Jump size"));
				for (int a : AMOUNTS)
					sub->addChild(createCheckMenuItem(string::f("%+d semitones", a), "",
						[=]() { return c->tranAmount[v] == a; }, [=]() { c->tranAmount[v] = a; }));
				sub->addChild(new MenuSeparator);
				sub->addChild(createMenuLabel("Chance of a jump"));
				for (int p : PROBS)
					sub->addChild(createCheckMenuItem(string::f("%d%%", p), "",
						[=]() { return c->tranProb[v] == p; }, [=]() { c->tranProb[v] = p; }));
			}));
		}
		menu->addChild(createSubmenuItem("Snapshot cycle length", "", [=](Menu* sub) {
			for (int b : {1, 2, 4, 8, 16, 32})
				sub->addChild(createCheckMenuItem(string::f("%d beats", b), "",
					[=]() { return c->defaultDuration == b; },
					[=]() {
						c->defaultDuration = b;
						c->duration = b;
						if (c->selected > 0) c->snaps[c->selected].duration = b;
					}));
		}));
		menu->addChild(createSubmenuItem("Random octave range", "", [=](Menu* sub) {
			const int lims[][2] = {{0, 0}, {-1, 1}, {-2, 2}, {-3, 3}, {0, 2}, {-2, 0}};
			for (auto& l : lims) {
				int lo = l[0], hi = l[1];
				sub->addChild(createCheckMenuItem(string::f("%+d to %+d", lo, hi), "",
					[=]() { return c->octMin == lo && c->octMax == hi; }, [=]() { c->octMin = lo; c->octMax = hi; }));
			}
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Randomise everything (like grid key 16 on row 3)", "", [=]() { c->randomizeAll(); m->syncKnobs(); }));
		menu->addChild(createMenuItem("Clear all snapshots", "", [=]() { c->clearSnaps(); }));
	}
};

Model* modelLessConcepts = createModel<LessConcepts, LessConceptsWidget>("LessConcepts");
