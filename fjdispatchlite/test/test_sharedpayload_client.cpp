// test_sharedpayload_client.cpp
//
// Client for FJSharedMem payload notify.
// Reads one line from stdin (until '\n'), sends it as payload, and waits for echo.

#include "fjsharedmem.h"

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <unistd.h>

// Message IDs (must match server)
static const fjt_msg_t MID_ECHO_REQ  = 50001;
static const fjt_msg_t MID_ECHO_RESP = 50002;

// Payload (<=512 bytes)
struct EchoPayload {
	pid_t client_pid;
	int   seq;
	short text_len;
	char  text[64];
};

class EchoClient : public FJSharedMem {
public:
	EchoClient(const std::string& name, const char* srcfunc)
		: FJSharedMem(name, 0, srcfunc, std::vector<fjt_msg_t>{ MID_ECHO_RESP }),
		  pid_self_(getpid()),
		  got_(false) {
	}

	bool sendLine(const std::string& line) {
		EchoPayload p;
		memset(&p, 0, sizeof(p));
		p.client_pid = pid_self_;
		p.seq = ++seq_;
		// Truncate and keep printable
		std::string s = line;
		if (!s.empty() && s.back() == '\n') s.pop_back();
		if (s.size() >= sizeof(p.text)) {
			s.resize(sizeof(p.text) - 1);
		}
		memcpy(p.text, s.c_str(), s.size());
		p.text_len = (short)s.size();

		{
			std::lock_guard<std::mutex> lk(mu_);
			got_ = false;
		}
		return notify(this, MID_ECHO_REQ, &p, sizeof(p));
	}

	bool waitEcho(EchoPayload& out, int timeout_ms) {
		std::unique_lock<std::mutex> lk(mu_);
		return cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return got_; })
			? (out = last_, true)
			: false;
	}

	void updateWithData(FJSharedMem* /*obj*/, fjt_msg_t msg, const void* buf, size_t size) override {
		if (msg != MID_ECHO_RESP) {
			return;
		}
		if (!buf || size < sizeof(EchoPayload)) {
			return;
		}
		EchoPayload p;
		memcpy(&p, buf, sizeof(p));
		if (p.client_pid != pid_self_) {
			// Broadcast response addressed to another client.
			return;
		}
		p.text[sizeof(p.text) - 1] = '\0';

		{
			std::lock_guard<std::mutex> lk(mu_);
			last_ = p;
			got_ = true;
		}
		cv_.notify_all();
	}

private:
	pid_t pid_self_;
	std::mutex mu_;
	std::condition_variable cv_;
	EchoPayload last_;
	bool got_;
	int seq_ = 0;
};

int main() {
	EchoClient client("/fjtest_echo", __PRETTY_FUNCTION__);

	std::cerr << "[client] started. Type a line and press Enter. Ctrl+D to exit.\n";
	std::string line;
	while (std::getline(std::cin, line)) {
		line.push_back('\n');
		if (!client.sendLine(line)) {
			std::cerr << "[client] notify failed (is server running?)\n";
			continue;
		}
		EchoPayload r;
		if (client.waitEcho(r, 5000)) {
			std::cout << "echo: seq=" << r.seq << " text=\"" << r.text << "\"\n";
		} else {
			std::cerr << "[client] timeout waiting for echo\n";
			client.profileAndGC(true, 5000);
		}
	}
	std::cerr << "[client] end\n";
	return 0;
}
