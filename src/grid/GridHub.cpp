#include "GridHub.hpp"
#include "Osc.hpp"
#include <chrono>
#include <cstdio>

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	typedef int socklen_t;
	#define CLOSESOCK closesocket
	#define BADSOCK INVALID_SOCKET
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/select.h>
	#include <sys/socket.h>
	#include <unistd.h>
	#define CLOSESOCK close
	#define BADSOCK (-1)
#endif

namespace gridworks {

static const int SERIALOSC_PORT = 12002;      // fixed by monome
static const int FIRST_LISTEN_PORT = 14100;   // monome-rack uses 13000+, keep clear of it
static const char* PREFIX = "/gridworks";

GridHub& GridHub::get() {
	static GridHub hub;
	return hub;
}

GridHub::GridHub() {
#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
	auto s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == BADSOCK) return;
	for (int p = FIRST_LISTEN_PORT; p < FIRST_LISTEN_PORT + 40; p++) {
		sockaddr_in a{};
		a.sin_family = AF_INET;
		a.sin_port = htons((uint16_t) p);
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (bind(s, (sockaddr*) &a, sizeof(a)) == 0) {
			listenPort = p;
			break;
		}
	}
	if (!listenPort) {
		CLOSESOCK(s);
		return;
	}
	sock = (intptr_t) s;
	running = true;
	thread = std::thread([this] { run(); });
}

GridHub::~GridHub() {
	running = false;
	if (thread.joinable()) thread.join();
	if (sock != -1) CLOSESOCK(sock);
}

void GridHub::sendTo(int port, const std::vector<uint8_t>& packet) {
	if (sock == -1 || port <= 0) return;
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = htons((uint16_t) port);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sendto(sock, (const char*) packet.data(), (int) packet.size(), 0, (sockaddr*) &a, sizeof(a));
}

void GridHub::sendList() {
	OscWriter w("/serialosc/list");
	w.s("127.0.0.1").i(listenPort);
	sendTo(SERIALOSC_PORT, w.data());
}

void GridHub::sendNotify() {
	// serialosc only tells us about ONE add/remove per /notify, so we re-send it each time.
	OscWriter w("/serialosc/notify");
	w.s("127.0.0.1").i(listenPort);
	sendTo(SERIALOSC_PORT, w.data());
}

void GridHub::claim(const GridDevice& d) {
	// Point the grid's key presses at us, with our prefix.
	{ OscWriter w("/sys/port"); w.i(listenPort); sendTo(d.port, w.data()); }
	{ OscWriter w("/sys/host"); w.s("127.0.0.1"); sendTo(d.port, w.data()); }
	// like monome-rack, send the prefix without its leading slash; serialosc adds it
	{ OscWriter w("/sys/prefix"); w.s(PREFIX + 1); sendTo(d.port, w.data()); }
}

void GridHub::rescan() {
	rescanWanted = true;
}

void GridHub::run() {
	using clock = std::chrono::steady_clock;
	auto lastList = clock::now() - std::chrono::seconds(10);
	uint8_t buf[2048];
	std::string keyAddr = std::string(PREFIX) + "/grid/key";
	sendNotify();

	while (running) {
		// Ask again every few seconds until a grid shows up (serialosc may start after Rack).
		bool haveDevs;
		{ std::lock_guard<std::mutex> lk(mutex); haveDevs = !devs.empty(); }
		auto now = clock::now();
		if (rescanWanted || (!haveDevs && now - lastList > std::chrono::seconds(3))) {
			rescanWanted = false;
			lastList = now;
			sendList();
			sendNotify();
		}

		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		timeval tv{0, 200000};
		int r = select((int) sock + 1, &fds, nullptr, nullptr, &tv);
		if (r <= 0) continue;

		sockaddr_in from{};
		socklen_t fromLen = sizeof(from);
		int n = (int) recvfrom(sock, (char*) buf, sizeof(buf), 0, (sockaddr*) &from, &fromLen);
		if (n <= 0) continue;
		int fromPort = ntohs(from.sin_port);

		std::vector<OscMessage> msgs;
		oscParse(buf, (size_t) n, msgs);
		for (const OscMessage& m : msgs) {
			if (m.address == keyAddr || m.address == "/" + keyAddr) {
				std::lock_guard<std::mutex> lk(mutex);
				for (auto& d : devs) {
					if (d.port != fromPort) continue;
					auto it = owners.find(d.id);
					if (it != owners.end() && it->second)
						it->second->gridKey(m.intAt(0), m.intAt(1), m.intAt(2));
				}
			}
			else if (m.address == "/serialosc/device" || m.address == "/serialosc/add") {
				serviceSeen = true;
				GridDevice nd;
				nd.id = m.strAt(0);
				nd.type = m.strAt(1);
				nd.port = m.intAt(2);
				if (nd.id.empty() || nd.port <= 0) continue;
				bool reclaim = false;
				{
					std::lock_guard<std::mutex> lk(mutex);
					bool found = false;
					for (auto& d : devs) {
						if (d.id == nd.id) {
							d.port = nd.port;
							d.type = nd.type;
							nd = d;
							found = true;
						}
					}
					if (!found) devs.push_back(nd);
					reclaim = owners.count(nd.id) > 0;
				}
				// Ask the grid for its size (answers come back to us as /sys/size).
				{ OscWriter w("/sys/info"); w.s("127.0.0.1").i(listenPort); sendTo(nd.port, w.data()); }
				if (reclaim) claim(nd);   // re-plugged grid: take it back
				if (m.address == "/serialosc/add") sendNotify();
			}
			else if (m.address == "/serialosc/remove") {
				std::string id = m.strAt(0);
				{
					std::lock_guard<std::mutex> lk(mutex);
					for (size_t k = 0; k < devs.size(); k++) {
						if (devs[k].id == id) { devs.erase(devs.begin() + k); break; }
					}
					// keep the owner entry so the grid comes back to the same module
				}
				sendNotify();
			}
			else if (m.address == "/sys/size") {
				std::lock_guard<std::mutex> lk(mutex);
				for (auto& d : devs) {
					if (d.port == fromPort) {
						d.width = m.intAt(0, 16);
						d.height = m.intAt(1, 8);
						d.sizeKnown = true;
					}
				}
			}
		}
	}
}

std::vector<GridDevice> GridHub::devices() {
	std::lock_guard<std::mutex> lk(mutex);
	return devs;
}

bool GridHub::connect(GridClient* client, const std::string& id) {
	GridDevice d;
	{
		std::lock_guard<std::mutex> lk(mutex);
		// a client owns at most one grid
		for (auto it = owners.begin(); it != owners.end();) {
			if (it->second == client) it = owners.erase(it);
			else ++it;
		}
		owners[id] = client;
		bool found = false;
		for (auto& x : devs) {
			if (x.id == id) { d = x; found = true; }
		}
		if (!found) return false;
	}
	claim(d);
	return true;
}

void GridHub::disconnect(GridClient* client) {
	std::lock_guard<std::mutex> lk(mutex);
	for (auto it = owners.begin(); it != owners.end();) {
		if (it->second == client) it = owners.erase(it);
		else ++it;
	}
}

std::string GridHub::ownedId(GridClient* client) {
	std::lock_guard<std::mutex> lk(mutex);
	for (auto& kv : owners)
		if (kv.second == client) return kv.first;
	return "";
}

bool GridHub::ownedDevice(GridClient* client, GridDevice& out) {
	std::lock_guard<std::mutex> lk(mutex);
	for (auto& kv : owners) {
		if (kv.second != client) continue;
		for (auto& d : devs) {
			if (d.id == kv.first) { out = d; return true; }
		}
	}
	return false;
}

std::string GridHub::firstFreeId() {
	std::lock_guard<std::mutex> lk(mutex);
	for (auto& d : devs)
		if (!owners.count(d.id)) return d.id;
	return "";
}

void GridHub::sendLevelMap(GridClient* client, int xOffset, int yOffset, const uint8_t* levels) {
	GridDevice d;
	if (!ownedDevice(client, d)) return;
	OscWriter w(std::string(PREFIX) + "/grid/led/level/map");
	w.i(xOffset).i(yOffset);
	for (int k = 0; k < 64; k++) w.i(levels[k] > 15 ? 15 : levels[k]);
	sendTo(d.port, w.data());
}

void GridHub::sendAllOff(GridClient* client) {
	GridDevice d;
	if (!ownedDevice(client, d)) return;
	OscWriter w(std::string(PREFIX) + "/grid/led/level/all");
	w.i(0);
	sendTo(d.port, w.data());
}

} // namespace gridworks
