#pragma once
#include "Arduino.h"
#include <functional>
#include <map>
#include <string>

#define HTTP_GET  1
#define HTTP_POST 2

// Multipart upload status (OTA /update). The sim does not stream multipart
// bodies, so upload() reports an idle END state; the finish handler still runs
// so the authorized/no-firmware paths behave faithfully.
enum { UPLOAD_FILE_START = 0, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload {
    int      status      = UPLOAD_FILE_END;
    uint8_t* buf         = nullptr;
    size_t   currentSize = 0;
    size_t   totalSize   = 0;
    String   filename;
    String   name;
};

// Real localhost TCP webserver so app HTTP handlers run against genuine POSTs.
// The bound port is the constructor port unless env ESPRITE_HTTP_PORT overrides
// it (lets tests avoid privileged port 80).
class WebServer {
public:
    typedef std::function<void()> Handler;
    explicit WebServer(int port);
    ~WebServer();
    void on(const char* path, int method, Handler h);
    // Upload form (e.g. OTA /update): finishFn runs when the request completes;
    // uploadFn is kept for fidelity though the sim doesn't stream the body.
    void on(const char* path, int method, Handler finishFn, Handler uploadFn);
    HTTPUpload& upload() { return upload_; }
    void begin();
    void handleClient();
    void send(int code, const char* type, const String& body);
    void send(int code, const char* type, const char* body) { send(code, type, String(body)); }
    void send_P(int code, const char* type, const char* body) { send(code, type, String(body)); }
    bool hasArg(const char* name);
    String arg(const char* name);
    void collectHeaders(const char** names, size_t count);  // sim collects all; no-op list
    bool hasHeader(const char* name);
    String header(const char* name);
    void stop();
    int boundPort() const { return bound_port_; }
private:
    void drive_upload(const Handler& cb);   // parse a multipart file part, run the upload cb
    int port_;
    int bound_port_ = 0;
    int listen_fd_ = -1;
    std::map<std::string, Handler> get_, post_;
    std::map<std::string, Handler> upload_handlers_;   // per-path upload callbacks (fidelity only)
    std::map<std::string, std::string> headers_;   // current request headers (lowercased keys)
    std::string body_;
    int client_fd_ = -1;
    HTTPUpload upload_;
};

// Bind status of the most recent WebServer::begin(): -1 = never started,
// 0 = bind failed, >0 = bound on that port.
int sim_http_bind_status();
