#include "doctest.h"
#include "qmp.h"
#include <ArduinoJson.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <map>
#include <string>
#include <thread>

namespace {

void fq_send_line(int fd, const std::string& line) {
    std::string out = line;
    out.push_back('\n');
    ::send(fd, out.data(), out.size(), 0);
}

// Byte-at-a-time is fine here: the fake server only ever handles the tiny
// scripted QMP lines a unit test sends.
bool fq_recv_line(int fd, std::string* line) {
    line->clear();
    for (;;) {
        char c;
        ssize_t n = ::recv(fd, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '\n') return true;
        line->push_back(c);
    }
}

// A one-shot in-process QMP server: listens on a unix socket in a mkdtemp'd
// dir, accepts a single connection on a background thread, and speaks a
// scripted session (greeting, qmp_capabilities negotiation, one interleaved
// event) before replying to registered commands. Exercises the real QmpClient
// wire protocol without a real QEMU process.
struct FakeQmpServer {
    std::string dir;
    std::string sock_path;
    int listen_fd = -1;
    std::thread th;
    std::map<std::string, std::string> returns;   // cmd -> raw "return" JSON value
    std::map<std::string, std::string> errors;     // cmd -> "error" desc
    bool silent_after_negotiate = false;            // simulate a hung QEMU: negotiate, then never reply

    FakeQmpServer() {
        char tmpl[] = "/tmp/esprite_qmp_XXXXXX";
        char* d = mkdtemp(tmpl);
        REQUIRE(d != nullptr);
        dir = d;
        sock_path = dir + "/qmp.sock";

        listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(listen_fd >= 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
        REQUIRE(::bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd, 1) == 0);

        // Reply/error registrations race-free: the client can only reach the
        // command-reply loop after connect_unix() completes, which requires
        // accept() to have already returned, which requires connect() to have
        // already run on the test thread after every reply()/error() call in
        // program order. The socket accept/connect pair is the synchronization.
        th = std::thread([this] { serve_one(); });
    }

    ~FakeQmpServer() {
        if (th.joinable()) th.join();
        if (listen_fd >= 0) ::close(listen_fd);
        std::string rm = "rm -rf " + dir;
        system(rm.c_str());
    }

    const std::string& path() const { return sock_path; }

    void reply(const std::string& cmd, const std::string& return_json) { returns[cmd] = return_json; }
    void error(const std::string& cmd, const std::string& desc) { errors[cmd] = desc; }

    void serve_one() {
        int fd = accept(listen_fd, nullptr, nullptr);
        if (fd < 0) return;

        fq_send_line(fd, R"({"QMP":{"version":{"qemu":{"major":8,"minor":0,"micro":0},"package":""},"capabilities":[]}})");

        std::string line;
        if (!fq_recv_line(fd, &line)) { ::close(fd); return; }   // qmp_capabilities
        fq_send_line(fd, R"({"return":{}})");
        // An async notification interleaved right after negotiation, to prove
        // the client skips it while waiting for a command's real reply.
        fq_send_line(fd, R"({"event":"STOP","data":{},"timestamp":{"seconds":0,"microseconds":0}})");

        if (silent_after_negotiate) {
            // Hold the connection open without ever replying, so the client's
            // execute() genuinely blocks in poll() until its deadline fires.
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            ::close(fd);
            return;
        }

        while (fq_recv_line(fd, &line)) {
            JsonDocument doc;
            if (deserializeJson(doc, line)) break;
            std::string cmd = doc["execute"] | std::string("");
            auto eit = errors.find(cmd);
            if (eit != errors.end()) {
                fq_send_line(fd, std::string(R"({"error":{"class":"GenericError","desc":")") + eit->second + "\"}}");
                continue;
            }
            auto rit = returns.find(cmd);
            fq_send_line(fd, std::string(R"({"return":)") + (rit != returns.end() ? rit->second : "{}") + "}");
        }
        ::close(fd);
    }
};

}  // namespace

TEST_CASE("qmp connects, negotiates, executes, skips events") {
    FakeQmpServer srv;
    srv.reply("query-status", R"({"status":"running","running":true})");
    QmpClient c;
    std::string err, result;
    REQUIRE(c.connect_unix(srv.path(), 2000, &err));
    REQUIRE(c.execute("query-status", "", &result, &err));
    CHECK(result.find("running") != std::string::npos);
}

TEST_CASE("qmp reports connect timeout on a dead socket") {
    QmpClient c;
    std::string err;
    CHECK(!c.connect_unix("/nonexistent/qmp.sock", 200, &err));
    CHECK(!err.empty());
}

TEST_CASE("qmp execute passes arguments and returns the raw value") {
    FakeQmpServer srv;
    srv.reply("query-name", R"({"name":"esprite-qemu"})");
    QmpClient c;
    std::string err, result;
    REQUIRE(c.connect_unix(srv.path(), 2000, &err));
    REQUIRE(c.execute("query-name", R"({"fast":true})", &result, &err));
    CHECK(result.find("esprite-qemu") != std::string::npos);
}

TEST_CASE("qmp execute returns false with the error desc on an error reply, and the session stays usable") {
    FakeQmpServer srv;
    srv.error("bogus-cmd", "The command bogus-cmd has not been found");
    srv.reply("query-status", R"({"status":"running","running":true})");
    QmpClient c;
    std::string err, result;
    REQUIRE(c.connect_unix(srv.path(), 2000, &err));
    CHECK(!c.execute("bogus-cmd", "", &result, &err));
    CHECK(err.find("has not been found") != std::string::npos);
    // A QMP-level error reply (as opposed to a transport failure) does not
    // desync or invalidate the session: the same connection keeps working.
    CHECK(c.connected());
    CHECK(c.execute("query-status", "", &result, &err));
    CHECK(result.find("running") != std::string::npos);
}

TEST_CASE("qmp execute times out when the server goes silent, and closes the now-unreliable connection") {
    FakeQmpServer srv;
    srv.silent_after_negotiate = true;
    QmpClient c;
    std::string err, result;
    REQUIRE(c.connect_unix(srv.path(), 2000, &err));
    CHECK(!c.execute("query-status", "", &result, &err, 150));
    CHECK(!err.empty());
    // A read timeout means a late reply could still land in the socket
    // buffer and desync the next command's read, so execute() must close
    // the connection rather than leave it looking usable.
    CHECK(!c.connected());
}
