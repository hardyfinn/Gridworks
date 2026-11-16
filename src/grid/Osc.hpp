// Tiny OSC (Open Sound Control) message writer and reader.
// Only what serialosc needs: int32 ('i') and string ('s') arguments.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gridworks {

struct OscArg {
	char type = 'i';
	int32_t i = 0;
	std::string s;
};

struct OscMessage {
	std::string address;
	std::vector<OscArg> args;

	int32_t intAt(size_t k, int32_t def = 0) const {
		return (k < args.size() && args[k].type == 'i') ? args[k].i : def;
	}
	std::string strAt(size_t k) const {
		return (k < args.size() && args[k].type == 's') ? args[k].s : std::string();
	}
};

// Builds one OSC packet.
struct OscWriter {
	std::vector<uint8_t> buf;
	std::string types = ",";
	std::vector<uint8_t> argData;
	std::string address;

	explicit OscWriter(const std::string& addr) : address(addr) {}

	static void padString(std::vector<uint8_t>& out, const std::string& s) {
		out.insert(out.end(), s.begin(), s.end());
		size_t n = 4 - (s.size() % 4);   // always at least one terminating zero
		out.insert(out.end(), n, 0);
	}
	OscWriter& i(int32_t v) {
		types += 'i';
		uint32_t u = (uint32_t) v;
		argData.push_back((u >> 24) & 0xff);
		argData.push_back((u >> 16) & 0xff);
		argData.push_back((u >> 8) & 0xff);
		argData.push_back(u & 0xff);
		return *this;
	}
	OscWriter& s(const std::string& v) {
		types += 's';
		padString(argData, v);
		return *this;
	}
	const std::vector<uint8_t>& data() {
		buf.clear();
		padString(buf, address);
		padString(buf, types);
		buf.insert(buf.end(), argData.begin(), argData.end());
		return buf;
	}
};

// Parses a packet (message or bundle) into messages. Returns false on garbage.
inline bool oscParse(const uint8_t* p, size_t n, std::vector<OscMessage>& out, int depth = 0) {
	auto readStr = [&](size_t& pos, std::string& s) -> bool {
		size_t start = pos;
		while (pos < n && p[pos] != 0) pos++;
		if (pos >= n) return false;
		s.assign((const char*) p + start, pos - start);
		pos = (pos + 4) & ~(size_t) 3;   // skip zero(s) to the next multiple of 4
		return pos <= n;
	};
	auto readInt = [&](size_t& pos, int32_t& v) -> bool {
		if (pos + 4 > n) return false;
		uint32_t u = ((uint32_t) p[pos] << 24) | ((uint32_t) p[pos + 1] << 16) | ((uint32_t) p[pos + 2] << 8) | p[pos + 3];
		v = (int32_t) u;
		pos += 4;
		return true;
	};
	if (n < 4 || depth > 4) return false;
	if (n >= 16 && std::memcmp(p, "#bundle", 8) == 0) {
		size_t pos = 16;   // "#bundle\0" + 8-byte time tag
		while (pos + 4 <= n) {
			int32_t len;
			readInt(pos, len);
			if (len <= 0 || pos + (size_t) len > n) return false;
			oscParse(p + pos, (size_t) len, out, depth + 1);
			pos += len;
		}
		return true;
	}
	OscMessage m;
	size_t pos = 0;
	if (!readStr(pos, m.address)) return false;
	std::string types;
	if (pos >= n) { out.push_back(m); return true; }
	if (!readStr(pos, types) || types.empty() || types[0] != ',') return false;
	for (size_t k = 1; k < types.size(); k++) {
		OscArg a;
		a.type = types[k];
		if (a.type == 'i') {
			if (!readInt(pos, a.i)) return false;
		}
		else if (a.type == 's') {
			if (!readStr(pos, a.s)) return false;
		}
		else if (a.type == 'f') {
			int32_t raw;
			if (!readInt(pos, raw)) return false;
			float f;
			std::memcpy(&f, &raw, 4);
			a.type = 'i';
			a.i = (int32_t) f;
		}
		else {
			return false;   // unknown type: stop
		}
		m.args.push_back(a);
	}
	out.push_back(m);
	return true;
}

} // namespace gridworks
