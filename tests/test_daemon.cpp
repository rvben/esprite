#include "doctest.h"
#include "cli.h"
#include <cstdio>
#include <cstdlib>
#include <string>

// Drive a full `run` session in-process: feed newline-delimited JSON commands,
// return the concatenated replies. Uses the real esprite_daemon entry point.
static std::string run_daemon(const std::string& input) {
    FILE* in = fmemopen((void*)input.data(), input.size(), "r");
    char* buf = nullptr;
    size_t len = 0;
    FILE* out = open_memstream(&buf, &len);
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    esprite_daemon(in, out);
    fclose(in);
    fclose(out);
    std::string reply(buf, len);
    free(buf);
    return reply;
}

TEST_CASE("run session: an invalid serial-expect regex is an error reply, not a crash") {
    // Regression: a malformed pattern threw std::regex_error out of the session
    // loop, killing the whole persistent session (and process).
    std::string out = run_daemon(
        "{\"cmd\":\"boot\",\"target\":\"cyd\"}\n"
        "{\"cmd\":\"serial\",\"sub\":\"expect\",\"regex\":\"(\"}\n"
        "{\"cmd\":\"logs\"}\n");
    CHECK(out.find("\"error\"") != std::string::npos);   // the bad regex is reported
    CHECK(out.find("\"serial\"") != std::string::npos);  // the session survives to answer logs
}
