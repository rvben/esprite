#pragma once
#include "Arduino.h"
#include <functional>
#include <map>
#include <string>

#define HTTP_GET  1
#define HTTP_POST 2

// Real localhost TCP webserver so app HTTP handlers run against genuine POSTs.
// The bound port is the constructor port unless env CLAWDSIM_HTTP_PORT overrides
// it (lets tests avoid privileged port 80).
class WebServer {
public:
    typedef std::function<void()> Handler;
    explicit WebServer(int port) : port_(port) {}
    ~WebServer();
    void on(const char* path, int method, Handler h);
    void begin();
    void handleClient();
    void send(int code, const char* type, const String& body);
    void send(int code, const char* type, const char* body) { send(code, type, String(body)); }
    bool hasArg(const char* name);
    String arg(const char* name);
    int boundPort() const { return bound_port_; }
private:
    int port_;
    int bound_port_ = 0;
    int listen_fd_ = -1;
    std::map<std::string, Handler> get_, post_;
    std::string body_;
    int client_fd_ = -1;
};

// Bind status of the most recent WebServer::begin(): -1 = never started,
// 0 = bind failed, >0 = bound on that port.
int sim_http_bind_status();
