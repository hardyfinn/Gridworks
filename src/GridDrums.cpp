// GridDrums: 6-track, 16-step trigger sequencer played from a monome grid
// (through serialosc) or by clicking the grid picture on the panel.
// 16 pattern slots; a new slot starts at the next bar.
#include "plugin.hpp"
#include "GridDrumsCore.hpp"
#include "grid/GridHub.hpp"

using namespace gridworks;

// ---- LAYOUT (same numbers in res/make_panel.py) ----
static const float CELL = 5.8f;
static const float GX0 = 20.0f, GY0 = 17.0f;
static const float GAP_MUTE = 5.0f, GAP_SLOT = 2.0f;
static const float STATUS_Y = 75.5f;
static const float IN_Y = 95.0f;
static const float X_CLOCK = 14.0f, X_RESET = 30.0f, X_SLOT = 46.0f, X_GRIDLIGHT = 104.0f;
static const float OUT_Y = 114.0f;
static const float X_OUT[6] = {12.0f, 26.0f, 40.0f, 54.0f, 68.0f, 82.0f};
static const float X_BAR = 106.0f;

static float rowY(int r) {
	float y = GY0 + r * CELL + CELL / 2;
	if (r >= 6) y += GAP_MUTE;
	if (r >= 7) y += GAP_SLOT;
	return y;
}

static const NVGcolor TRACK_COL[6] = {
	nvgRGB(0xF2, 0xA9, 0x3B), nvgRGB(0xE8, 0x68, 0x4A), nvgRGB(0xE9, 0xE1, 0xCF),
	nvgRGB(0x6C, 0xC3, 0xB5), nvgRGB(0x8F, 0xA8, 0xE8), nvgRGB(0xC9, 0x8B, 0xD9),
};

struct KeyEvent {
	int8_t x, y, down, fromScreen;
};

struct GridDrums : Module, GridClient {
	enum ParamId { PARAMS_LEN };
	enum InputId { CLOCK_INPUT, RESET_INPUT, SLOT_INPUT, INPUTS_LEN };
	enum OutputId { ENUMS(TRIG_OUTPUT, 6), BAR_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(GRID_LIGHT, 2), LIGHTS_LEN };

	GridDrumsCore core;

	// key presses arrive from two other threads; each has its own one-way queue
	dsp::RingBuffer<KeyEvent, 256> gridQueue;    // network thread -> audio
	dsp::RingBuffer<KeyEvent, 256> screenQueue;  // UI thread -> audio

	dsp::SchmittTrigger clockTrig, resetTrig;
	dsp::PulseGenerator trigPulse[6], barPulse;
	int firedMask = 0;          // tracks that fired on the current step (for gate mode)
	float flashTime[6] = {};    // seconds left of the "just fired" flash on the mute keys
	float sinceClock = 1e6f;    // seconds since the last clock
	float resetHold = 0.f;      // ignore clocks just after a reset (standard Rack behaviour)
	int lastCvSlot = -1;

	// options (right-click menu)
	bool gateMode = false;          // false = short triggers, true = gates as long as the clock pulse
	bool changeImmediately = false; // false = new slot waits for the next bar

	// grid connection
	std::string wantedId;           // the grid this module should use (saved with the patch)
	bool autoConnect = true;        // take the first free grid when added
	std::atomic<bool> releaseGrid{false};

	GridDrums() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configInput(CLOCK_INPUT, "Clock (one pulse per step)");
		configInput(RESET_INPUT, "Reset (next clock plays step 1)");
		configInput(SLOT_INPUT, "Slot select, 1 semitone (1/12 V) per slot from 0 V");
		for (int t = 0; t < 6; t++)
			configOutput(TRIG_OUTPUT + t, string::f("Track %d trigger", t + 1));
		configOutput(BAR_OUTPUT, "Bar (trigger on step 1)");
		configLight(GRID_LIGHT, "Grid connected");
	}

	~GridDrums() {
		GridHub::get().sendAllOff(this);
		GridHub::get().disconnect(this);
	}

	void gridKey(int x, int y, int state) override {
		if (!gridQueue.full()) gridQueue.push(KeyEvent{(int8_t) x, (int8_t) y, (int8_t) (state ? 1 : 0), 0});
	}

	bool clockRunning() const {
		return sinceClock < 2.f;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		core = GridDrumsCore();
		core.clearAll();
	}

	void onRandomize(const RandomizeEvent& e) override {
		// a fresh random beat in the slot that's playing; busier on the hat tracks
		const float density[6] = {0.25f, 0.15f, 0.55f, 0.15f, 0.2f, 0.2f};
		for (int t = 0; t < 6; t++) {
			uint16_t bits = 0;
			for (int s = 0; s < 16; s++)
				if (random::uniform() < density[t]) bits |= 1 << s;
			core.pat[core.cur][t] = bits;
		}
	}

	void process(const ProcessArgs& args) override {
		bool immediate = changeImmediately || !clockRunning();
		KeyEvent ev;
		while (!gridQueue.empty()) {
			ev = gridQueue.shift();
			core.key(ev.x, ev.y, ev.down, false, immediate);
		}
		while (!screenQueue.empty()) {
			ev = screenQueue.shift();
			core.key(ev.x, ev.y, ev.down, true, immediate);
		}

		// slot CV: one semitone per slot, starting at 0 V
		if (inputs[SLOT_INPUT].isConnected()) {
			int s = clamp((int) std::round(inputs[SLOT_INPUT].getVoltage() * 12.f), 0, 15);
			if (s != lastCvSlot) {
				lastCvSlot = s;
				core.requestSlot(s, immediate);
			}
		}
		else {
			lastCvSlot = -1;
		}

		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			core.reset();
			resetHold = 1e-3f;
			firedMask = 0;
		}
		resetHold -= args.sampleTime;
		sinceClock += args.sampleTime;

		bool clockHigh = inputs[CLOCK_INPUT].getVoltage() >= 1.f;
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f) && resetHold <= 0.f) {
			sinceClock = 0.f;
			firedMask = core.tick();
			for (int t = 0; t < 6; t++) {
				if (firedMask & (1 << t)) {
					trigPulse[t].trigger(5e-3f);
					flashTime[t] = 0.08f;
				}
			}
			if (core.step == 0) barPulse.trigger(5e-3f);
		}

		for (int t = 0; t < 6; t++) {
			bool on = trigPulse[t].process(args.sampleTime);
			if (gateMode) on = clockHigh && (firedMask & (1 << t)) && sinceClock < 4.f;
			outputs[TRIG_OUTPUT + t].setVoltage(on ? 10.f : 0.f);
			if (flashTime[t] > 0.f) flashTime[t] -= args.sampleTime;
		}
		outputs[BAR_OUTPUT].setVoltage(barPulse.process(args.sampleTime) ? 10.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* slots = json_array();
		for (int s = 0; s < 16; s++) {
			json_t* tr = json_array();
			for (int t = 0; t < 6; t++) json_array_append_new(tr, json_integer(core.pat[s][t]));
			json_array_append_new(slots, tr);
		}
		json_object_set_new(root, "slots", slots);
		json_t* mutes = json_array();
		for (int t = 0; t < 6; t++) json_array_append_new(mutes, json_boolean(core.mute[t]));
		json_object_set_new(root, "mutes", mutes);
		json_object_set_new(root, "slot", json_integer(core.queued >= 0 ? core.queued : core.cur));
		json_object_set_new(root, "gateMode", json_boolean(gateMode));
		json_object_set_new(root, "changeImmediately", json_boolean(changeImmediately));
		std::string id = GridHub::get().ownedId(this);
		json_object_set_new(root, "grid", json_string(id.empty() ? wantedId.c_str() : id.c_str()));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* slots = json_object_get(root, "slots")) {
			core.clearAll();
			for (int s = 0; s < 16 && s < (int) json_array_size(slots); s++) {
				json_t* tr = json_array_get(slots, s);
				for (int t = 0; t < 6 && t < (int) json_array_size(tr); t++)
					core.pat[s][t] = (uint16_t) json_integer_value(json_array_get(tr, t));
			}
		}
		if (json_t* mutes = json_object_get(root, "mutes")) {
			for (int t = 0; t < 6 && t < (int) json_array_size(mutes); t++)
				core.mute[t] = json_is_true(json_array_get(mutes, t));
		}
		if (json_t* j = json_object_get(root, "slot")) core.requestSlot(clamp((int) json_integer_value(j), 0, 15), true);
		if (json_t* j = json_object_get(root, "gateMode")) gateMode = json_is_true(j);
		if (json_t* j = json_object_get(root, "changeImmediately")) changeImmediately = json_is_true(j);
		if (json_t* j = json_object_get(root, "grid")) {
			wantedId = json_string_value(j) ? json_string_value(j) : "";
			// a saved patch remembers its grid; don't grab some other free grid instead
			autoConnect = false;
		}
	}
};

// The grid picture: shows the same lights as the hardware grid, and clicks act as key presses.
struct GridDisplay : Widget {
	GridDrums* module = nullptr;

	bool cellAt(Vec p, int& x, int& y) {
		// p is in pixels relative to this widget, which starts at (GX0, GY0) mm
		float mmX = p.x / mm2px(1.f) + GX0, mmY = p.y / mm2px(1.f) + GY0;
		x = (int) std::floor((mmX - GX0) / CELL);
		if (x < 0 || x > 15) return false;
		for (int r = 0; r < 8; r++) {
			if (std::fabs(mmY - rowY(r)) <= CELL / 2) {
				y = r;
				return true;
			}
		}
		return false;
	}

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && module) {
			int x, y;
			if (cellAt(e.pos, x, y)) {
				if (e.action == GLFW_PRESS && !module->screenQueue.full())
					module->screenQueue.push(KeyEvent{(int8_t) x, (int8_t) y, 1, 1});
				e.consume(this);
				return;
			}
		}
		Widget::onButton(e);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		GridDrumsCore preview;
		const GridDrumsCore& c = module ? module->core : preview;
		bool flash[6] = {};
		if (module)
			for (int t = 0; t < 6; t++) flash[t] = module->flashTime[t] > 0.f;
		bool blink = std::fmod(system::getTime(), 0.5) < 0.25;
		uint8_t lv[8][16];
		c.levels(lv, blink, flash);

		float s = mm2px(1.f);
		for (int r = 0; r < 8; r++) {
			for (int k = 0; k < 16; k++) {
				float cx = (k * CELL + CELL / 2) * s;
				float cy = (rowY(r) - GY0) * s;
				float half = (CELL / 2 - 0.45f) * s;
				NVGcolor base = r < 6 ? TRACK_COL[r] : nvgRGB(0xEC, 0xE6, 0xD8);
				float a = lv[r][k] / 15.f;
				// unlit key outline, darker between beats
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, cx - half, cy - half, 2 * half, 2 * half, 0.7f * s);
				nvgFillColor(args.vg, (k % 4 == 0) ? nvgRGB(0x2C, 0x2C, 0x31) : nvgRGB(0x22, 0x22, 0x26));
				nvgFill(args.vg);
				if (a > 0.f) {
					nvgBeginPath(args.vg);
					nvgRoundedRect(args.vg, cx - half, cy - half, 2 * half, 2 * half, 0.7f * s);
					nvgFillColor(args.vg, nvgTransRGBAf(base, 0.12f + 0.88f * a));
					nvgFill(args.vg);
				}
			}
		}

		// status line
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 3.6f * s);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		float ty = (STATUS_Y - GY0 + 1.2f) * s;
		std::string left = string::f("SLOT %d", c.cur + 1);
		if (c.queued >= 0) left += string::f("  > %d AT BAR", c.queued + 1);
		if (c.copyActive()) left = "COPY: PRESS A SLOT";
		if (c.clearActive()) left = "CLEAR: TRACK OR SLOT";
		nvgFillColor(args.vg, nvgRGB(0xEC, 0xE6, 0xD8));
		nvgText(args.vg, -14.f * s, ty, left.c_str(), NULL);

		std::string right = "NO GRID";
		if (module) {
			GridDevice d;
			if (GridHub::get().ownedDevice(module, d)) right = "GRID " + d.id;
			else if (!module->wantedId.empty()) right = "GRID OFF";
			else if (!GridHub::get().serialoscSeen()) right = "NO SERIALOSC";
		}
		nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, nvgRGB(0x9A, 0x94, 0x88));
		nvgText(args.vg, 16 * CELL * s, ty, right.c_str(), NULL);
	}
};

struct GridDrumsWidget : ModuleWidget {
	uint8_t sent[8][16];
	bool sentValid = false;
	double lastSend = 0.0, lastConnectTry = -10.0;
	std::string lastOwned;
	bool askedHub = false;

	GridDrumsWidget(GridDrums* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/GridDrums.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		GridDisplay* disp = new GridDisplay;
		disp->module = module;
		disp->box.pos = mm2px(Vec(GX0, GY0));
		disp->box.size = mm2px(Vec(16 * CELL, STATUS_Y + 2.f - GY0));
		addChild(disp);

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_CLOCK, IN_Y)), module, GridDrums::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RESET, IN_Y)), module, GridDrums::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_SLOT, IN_Y)), module, GridDrums::SLOT_INPUT));
		addChild(createLightCentered<MediumLight<GreenRedLight>>(mm2px(Vec(X_GRIDLIGHT, IN_Y)), module, GridDrums::GRID_LIGHT));
		for (int t = 0; t < 6; t++)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_OUT[t], OUT_Y)), module, GridDrums::TRIG_OUTPUT + t));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_BAR, OUT_Y)), module, GridDrums::BAR_OUTPUT));
	}

	// Runs on the UI thread every frame: keeps the grid connected and its lights up to date.
	void step() override {
		ModuleWidget::step();
		GridDrums* m = dynamic_cast<GridDrums*>(module);
		if (!m) return;
		GridHub& hub = GridHub::get();
		double now = system::getTime();

		if (m->releaseGrid.exchange(false)) {
			hub.sendAllOff(m);
			hub.disconnect(m);
			m->wantedId = "";
			m->autoConnect = false;
		}

		// Connect once: to the grid saved with the patch, or (new module) to the first free
		// grid, checking each second until one turns up. If the grid is unplugged, GridHub
		// remembers it's ours and takes it back when it returns. If another module takes it
		// later we don't grab it back, so two modules never fight over one grid.
		if (!askedHub && now - lastConnectTry > 1.0) {
			lastConnectTry = now;
			if (m->wantedId.empty() && m->autoConnect) m->wantedId = hub.firstFreeId();
			if (!m->wantedId.empty()) {
				hub.connect(m, m->wantedId);
				askedHub = true;
			}
		}
		std::string owned = hub.ownedId(m);
		if (owned != lastOwned) sentValid = false;
		lastOwned = owned;

		GridDevice dev;
		bool live = hub.ownedDevice(m, dev);
		m->lights[GridDrums::GRID_LIGHT + 0].setBrightness(live ? 1.f : 0.f);
		m->lights[GridDrums::GRID_LIGHT + 1].setBrightness(!live && !m->wantedId.empty() ? 0.6f : 0.f);
		if (!live) return;

		// Send the LEDs when they change (at most ~60 times a second), plus a refresh every second.
		bool flash[6];
		for (int t = 0; t < 6; t++) flash[t] = m->flashTime[t] > 0.f;
		bool blink = std::fmod(now, 0.5) < 0.25;
		uint8_t lv[8][16];
		m->core.levels(lv, blink, flash);
		bool changed = !sentValid || std::memcmp(lv, sent, sizeof(lv)) != 0;
		if ((changed && now - lastSend > 1.0 / 60.0) || now - lastSend > 1.0) {
			for (int q = 0; q < 2 && q * 8 < dev.width; q++) {
				uint8_t block[64];
				for (int r = 0; r < 8; r++)
					for (int k = 0; k < 8; k++) block[r * 8 + k] = lv[r][q * 8 + k];
				hub.sendLevelMap(m, q * 8, 0, block);
			}
			std::memcpy(sent, lv, sizeof(lv));
			sentValid = true;
			lastSend = now;
		}
	}

	void appendContextMenu(Menu* menu) override {
		GridDrums* m = dynamic_cast<GridDrums*>(module);
		if (!m) return;
		GridHub& hub = GridHub::get();

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("monome grid"));
		std::vector<GridDevice> devs = hub.devices();
		std::string owned = hub.ownedId(m);
		if (devs.empty()) {
			menu->addChild(createMenuLabel(hub.serialoscSeen() ? "No grid plugged in" : "serialosc not found: is it installed and running?"));
		}
		for (const GridDevice& d : devs) {
			std::string id = d.id;
			menu->addChild(createCheckMenuItem(d.type + " (" + d.id + ")", "",
				[=]() { return GridHub::get().ownedId(m) == id; },
				[=]() {
					m->wantedId = id;
					GridHub::get().connect(m, id);
				}));
		}
		menu->addChild(createMenuItem("Let go of the grid", "", [=]() { m->releaseGrid = true; }, owned.empty()));
		menu->addChild(createMenuItem("Look for grids again", "", [=]() { GridHub::get().rescan(); }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Gate outputs (follow clock pulse width)", "", &m->gateMode));
		menu->addChild(createBoolPtrMenuItem("Change slot at once (not at next bar)", "", &m->changeImmediately));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Clear the playing slot", "", [=]() {
			for (int t = 0; t < 6; t++) m->core.pat[m->core.cur][t] = 0;
		}));
		menu->addChild(createMenuItem("Unmute all tracks", "", [=]() {
			for (int t = 0; t < 6; t++) m->core.mute[t] = false;
		}));
	}
};

Model* modelGridDrums = createModel<GridDrums, GridDrumsWidget>("GridDrums");
