// Shared grid plumbing for Gridworks modules:
//   GridModule  - mix into a Module: owns the key queue and which grid to use.
//   GridLinkUI  - put in a ModuleWidget: connects the grid, sends LEDs, adds the menu.
#pragma once
#include <rack.hpp>
#include <atomic>
#include "GridHub.hpp"

namespace gridworks {

struct GridKeyEvent {
	int8_t x, y, down, fromScreen;
};

struct GridModule : GridClient {
	// key presses arrive from two other threads; each has its own one-way queue
	rack::dsp::RingBuffer<GridKeyEvent, 256> gridQueue;    // network thread -> audio
	rack::dsp::RingBuffer<GridKeyEvent, 256> screenQueue;  // UI thread -> audio

	std::string wantedId;          // the grid this module should use (saved with the patch)
	bool autoConnect = true;       // a new module takes the first free grid
	std::atomic<bool> releaseGrid{false};

	~GridModule() {
		GridHub::get().sendAllOff(this);
		GridHub::get().disconnect(this);
	}

	void gridKey(int x, int y, int state) override {
		if (!gridQueue.full()) gridQueue.push(GridKeyEvent{(int8_t) x, (int8_t) y, (int8_t) (state ? 1 : 0), 0});
	}
	void screenKey(int x, int y, bool down) {
		if (!screenQueue.full()) screenQueue.push(GridKeyEvent{(int8_t) x, (int8_t) y, (int8_t) down, 1});
	}

	void gridToJson(json_t* root) {
		std::string id = GridHub::get().ownedId(this);
		json_object_set_new(root, "grid", json_string(id.empty() ? wantedId.c_str() : id.c_str()));
	}
	void gridFromJson(json_t* root) {
		if (json_t* j = json_object_get(root, "grid")) {
			wantedId = json_string_value(j) ? json_string_value(j) : "";
			autoConnect = false;   // a saved patch remembers its grid; don't grab another one
		}
	}
	// Short text for the panel's status line.
	std::string gridStatus() {
		GridDevice d;
		if (GridHub::get().ownedDevice(this, d)) return "GRID " + d.id;
		if (!wantedId.empty()) return "GRID OFF";
		if (!GridHub::get().serialoscSeen()) return "NO SERIALOSC";
		return "NO GRID";
	}
};

struct GridLinkUI {
	uint8_t sent[8][16];
	bool sentValid = false;
	double lastSend = 0.0, lastConnectTry = -10.0;
	std::string lastOwned;
	bool askedHub = false;

	// Call from ModuleWidget::step(). Returns true while the grid is connected.
	bool update(GridModule* m, const uint8_t lv[8][16]) {
		GridHub& hub = GridHub::get();
		double now = rack::system::getTime();

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
		if (!hub.ownedDevice(m, dev)) return false;

		// Send the LEDs when they change (at most ~60 times a second), plus a refresh every second.
		bool changed = !sentValid || std::memcmp(lv, sent, sizeof(sent)) != 0;
		if ((changed && now - lastSend > 1.0 / 60.0) || now - lastSend > 1.0) {
			for (int q = 0; q < 2 && q * 8 < dev.width; q++) {
				uint8_t block[64];
				for (int r = 0; r < 8; r++)
					for (int k = 0; k < 8; k++) block[r * 8 + k] = lv[r][q * 8 + k];
				hub.sendLevelMap(m, q * 8, 0, block);
			}
			std::memcpy(sent, lv, sizeof(sent));
			sentValid = true;
			lastSend = now;
		}
		return true;
	}

	static void appendMenu(rack::ui::Menu* menu, GridModule* m) {
		using namespace rack;
		GridHub& hub = GridHub::get();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("monome grid"));
		std::vector<GridDevice> devs = hub.devices();
		std::string owned = hub.ownedId(m);
		if (devs.empty())
			menu->addChild(createMenuLabel(hub.serialoscSeen() ? "No grid plugged in" : "serialosc not found: is it installed and running?"));
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
	}
};

} // namespace gridworks
