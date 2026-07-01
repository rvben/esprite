#pragma once
#include <string>

// POST a body to the running target's webserver (localhost) and pump a few
// steps so the request is handled and the UI refreshes. Fire-and-forget: does
// not wait for the HTTP response (the single-threaded pump handles it later).
// Returns false if the request could not be delivered (connect failed or the
// body exceeds the server's bounded read size).
bool sim_wifi_post(const std::string& path, const std::string& body);

// Run a JSON scenario file against an already-selected default target (or the
// scenario's own "target" field). Returns 0 on success.
int scenario_run(const std::string& path, const std::string& default_target);
