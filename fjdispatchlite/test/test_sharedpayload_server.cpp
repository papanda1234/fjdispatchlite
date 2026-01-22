// test_sharedpayload_server.cpp
//
// "echo server" for FJSharedMem payload notify.
// In this repository's terminology, "server" just means "the first process you start".

#include "fjsharedmem.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <unistd.h>

// Message IDs
static const fjt_msg_t MID_ECHO_REQ  = 50001;
static const fjt_msg_t MID_ECHO_RESP = 50002;

// Payload (<=512 bytes)
struct EchoPayload {
	pid_t client_pid;       // who should consume the response
	int   seq;
	short text_len;
	char  text[64];
};

static std::atomic<bool> g_running(true);

static void on_sigint(int) {
	g_running.store(false);
}

class EchoServer : public FJSharedMem {
public:
	EchoServer(const std::string& name, const char* srcfunc)
		: FJSharedMem(name, 0, srcfunc, std::vector<fjt_msg_t>{ MID_ECHO_REQ }) {
	}

	void updateWithData(FJSharedMem* /*obj*/, fjt_msg_t msg, const void* buf, size_t size) override {
		if (msg != MID_ECHO_REQ) {
			return;
		}
		if (!buf || size < sizeof(EchoPayload)) {
			std::cerr << "[server] invalid payload size=" << size << "\n";
			return;
		}
		EchoPayload p;
		memcpy(&p, buf, sizeof(p));

		// Ensure NUL-termination for printing.
		p.text[sizeof(p.text) - 1] = '\0';

		std::cerr << "[server] req from pid=" << p.client_pid
		          << " seq=" << p.seq
		          << " text_len=" << p.text_len
		          << " text=\"" << p.text << "\"\n";

		// Echo back (broadcast). Clients will ignore responses not addressed to them.
		notify(this, MID_ECHO_RESP, &p, sizeof(p));
	}
};

int main() {
	signal(SIGINT, on_sigint);
	signal(SIGTERM, on_sigint);

	EchoServer server("/fjtest_echo", __PRETTY_FUNCTION__);
	std::cerr << "[server] started. MID_ECHO_REQ=" << MID_ECHO_REQ
	          << " MID_ECHO_RESP=" << MID_ECHO_RESP << "\n";
	std::cerr << "[server] Press Ctrl+C to stop.\n";

	while (g_running.load()) {
		sleep(1);
		// Optional: periodically show leaks and GC processed payloads.
		server.profileAndGC(false, 5000);
	}

	std::cerr << "[server] stopping...\n";
	server.profileAndGC(true, 5000);
	return 0;
}
