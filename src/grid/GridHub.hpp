// GridHub: one shared connection to serialosc for every Gridworks module.
//
// serialosc is monome's little background program on the Mac. It finds grids
// plugged in over USB and talks to apps with OSC messages over UDP on this
// computer (port 12002). This is the same route Dewb's monome-rack modules use.
//
// GridHub listens on its own UDP port (14100 and up), asks serialosc for the
// list of grids, and lets one module at a time "own" each grid. Owning a grid
// means the grid's key presses are sent to us (we set its /sys/port and
// /sys/prefix), so it moves over from whichever app had it before, including
// monome-rack. We use the prefix "/gridworks", so monome-rack ignores our keys.
#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>

namespace gridworks {

struct GridDevice {
	std::string id;      // serial number, e.g. "m1000123"
	std::string type;    // e.g. "monome 128"
	int port = 0;        // UDP port serialosc gave this grid
	int width = 16;
	int height = 8;
	bool sizeKnown = false;
};

// Implemented by a module that wants grid key presses.
struct GridClient {
	virtual ~GridClient() {}
	// Called from GridHub's network thread: just queue the event.
	virtual void gridKey(int x, int y, int state) = 0;
};

struct GridHub {
	static GridHub& get();

	// Devices serialosc has told us about (copy, safe from any thread).
	std::vector<GridDevice> devices();
	bool serialoscSeen() const { return serviceSeen.load(); }

	// Take a grid by id. Returns false if that grid isn't plugged in right now.
	bool connect(GridClient* client, const std::string& id);
	void disconnect(GridClient* client);
	// Id of the grid this client owns now ("" if none: never had one, or another module took it).
	std::string ownedId(GridClient* client);
	bool ownedDevice(GridClient* client, GridDevice& out);
	// Id of a plugged-in grid nobody in Gridworks owns, "" if none.
	std::string firstFreeId();

	// LEDs. levels = 64 brightness values 0..15, row by row, for the 8x8 block
	// whose top-left key is (xOffset, yOffset). Only sent if the client owns a grid.
	void sendLevelMap(GridClient* client, int xOffset, int yOffset, const uint8_t* levels);
	void sendAllOff(GridClient* client);

	// Ask serialosc for the list again (e.g. from a menu "rescan").
	void rescan();

	~GridHub();

private:
	GridHub();
	void run();
	void sendTo(int port, const std::vector<uint8_t>& packet);
	void sendList();
	void sendNotify();
	void claim(const GridDevice& d);

	std::mutex mutex;
	std::vector<GridDevice> devs;
	std::map<std::string, GridClient*> owners;
	std::thread thread;
	std::atomic<bool> running{false};
	std::atomic<bool> serviceSeen{false};
	std::atomic<bool> rescanWanted{false};
	intptr_t sock = -1;
	int listenPort = 0;
};

} // namespace gridworks
