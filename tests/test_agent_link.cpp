#include "doctest.h"
#include "agent_link.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

// A one-shot in-process agent: listens on a unix socket, accepts one
// connection on a background thread, optionally writes a banner, then
// replies to each received line from a script. Exercises the real AgentLink
// wire behavior without a QEMU guest.
struct FakeAgent {
    std::string dir, sock_path;
    int listen_fd = -1;
    std::thread th;
    std::vector<std::string> replies;   // one per received line, in order
    bool banner = false;                // write "esprite-agent v1\n" on accept
    bool silent = false;                // never reply (client must time out)

    FakeAgent() {
        char tmpl[] = "/tmp/esprite_agent_XXXXXX";
        char* d = mkdtemp(tmpl);
        REQUIRE(d != nullptr);
        dir = d;
        sock_path = dir + "/agent.sock";
        listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(listen_fd >= 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
        REQUIRE(::bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd, 1) == 0);
    }

    void start() {
        th = std::thread([this] {
            int fd = ::accept(listen_fd, nullptr, nullptr);
            if (fd < 0) return;
            if (banner) { const char* b = "esprite-agent v1\n"; ::send(fd, b, strlen(b), 0); }
            for (const auto& reply : replies) {
                std::string line;
                char c;
                while (::recv(fd, &c, 1, 0) == 1 && c != '\n') line.push_back(c);
                if (silent) break;
                std::string out = reply + "\n";
                ::send(fd, out.data(), out.size(), 0);
            }
            if (silent) {
                char c;
                while (::recv(fd, &c, 1, 0) == 1) {}   // swallow forever until close
            }
            ::close(fd);
        });
    }

    ~FakeAgent() {
        if (th.joinable()) th.join();
        if (listen_fd >= 0) ::close(listen_fd);
        unlink(sock_path.c_str());
        rmdir(dir.c_str());
    }
};

}  // namespace

TEST_CASE("agent_link: ok reply resolves true, err reply carries the message") {
    FakeAgent fa;
    fa.replies = {"ok", "err pin out of range"};
    fa.start();
    AgentLink link;
    std::string err;
    REQUIRE_MESSAGE(link.connect_unix(fa.sock_path, 1000, &err), err);
    std::string reply;
    CHECK(link.request("gpio 9 0", &reply, &err));
    CHECK(reply == "ok");
    CHECK_FALSE(link.request("gpio 999 0", &reply, &err));
    CHECK(err == "pin out of range");
    link.close();
}

TEST_CASE("agent_link: banner before the first reply is consumed, not mistaken for it") {
    FakeAgent fa;
    fa.banner = true;
    fa.replies = {"ok esprite-agent v1"};
    fa.start();
    AgentLink link;
    std::string err, reply;
    REQUIRE(link.connect_unix(fa.sock_path, 1000, &err));
    CHECK(link.request("ping", &reply, &err));
    CHECK(reply == "ok esprite-agent v1");
    link.close();
}

TEST_CASE("agent_link: a silent agent times out with a transport error") {
    FakeAgent fa;
    fa.silent = true;
    fa.replies = {"unused"};
    fa.start();
    AgentLink link;
    std::string err, reply;
    REQUIRE(link.connect_unix(fa.sock_path, 1000, &err));
    CHECK_FALSE(link.request("ping", &reply, &err, 300));
    CHECK(!err.empty());
    link.close();
}

TEST_CASE("agent_link: connect to a missing socket fails with an error") {
    AgentLink link;
    std::string err;
    CHECK_FALSE(link.connect_unix("/tmp/esprite_agent_nowhere/agent.sock", 300, &err));
    CHECK(!err.empty());
    CHECK_FALSE(link.connected());
}
