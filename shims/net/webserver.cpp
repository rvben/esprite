#include "WebServer.h"
#include "WiFi.h"
#include "ESPmDNS.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cctype>

WiFiClass     WiFi;
MDNSResponder MDNS;

// Bind status of the most recent WebServer::begin(): -1 = never started,
// 0 = bind failed (port in use), >0 = bound on that port. Lets the CLI detect a
// failed bind before entering a long-running serve loop.
static int g_bind_status = -1;
int sim_http_bind_status() { return g_bind_status; }

WebServer::~WebServer() {
    if (listen_fd_ >= 0) close(listen_fd_);
}

void WebServer::on(const char* path, int method, Handler h) {
    if (method == HTTP_POST) post_[path] = h;
    else                     get_[path] = h;
}

void WebServer::on(const char* path, int method, Handler finishFn, Handler uploadFn) {
    // Register the finish handler as the request handler so POST /update runs it
    // (auth + no-firmware paths behave faithfully); the sim does not stream the
    // multipart body, so the upload handler is stored but not driven.
    if (method == HTTP_POST) post_[path] = finishFn;
    else                     get_[path] = finishFn;
    upload_handlers_[path] = uploadFn;
}

void WebServer::begin() {
    signal(SIGPIPE, SIG_IGN);   // a client that closed before we reply must not kill us
    const char* env = getenv("CLAWDSIM_HTTP_PORT");
    bound_port_ = env ? atoi(env) : port_;

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)bound_port_);
    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close(listen_fd_); listen_fd_ = -1;
        g_bind_status = 0;   // bind failed (port likely already in use)
        return;
    }
    listen(listen_fd_, 8);
    fcntl(listen_fd_, F_SETFL, O_NONBLOCK);
    // If port 0 was requested, the OS assigned an ephemeral port; read it back so
    // the reported bind status is the real, connectable port (used by tests that
    // want a collision-free port).
    if (bound_port_ == 0) {
        sockaddr_in actual{};
        socklen_t len = sizeof(actual);
        if (getsockname(listen_fd_, (sockaddr*)&actual, &len) == 0)
            bound_port_ = ntohs(actual.sin_port);
    }
    g_bind_status = bound_port_;
}

void WebServer::handleClient() {
    if (listen_fd_ < 0) return;
    int fd = accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) return;
    // The accepted socket may inherit O_NONBLOCK from the listener on macOS;
    // force it blocking with a short recv timeout so the request body is read
    // reliably without ever wedging the single-threaded pump.
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    timeval tv{0, 200000};   // 200 ms
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Accumulate the whole request: headers plus the full body per Content-Length
    // (a multipart /update upload spans several recv chunks). Bounded so a runaway
    // body can never grow memory without limit; the sim only receives small test
    // uploads, not real multi-megabyte firmware.
    static const size_t MAX_REQ = 262144;   // 256 KB
    std::string req;
    char buf[16384];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) { close(fd); return; }
    req.append(buf, (size_t)n);
    size_t content_len = std::string::npos;   // npos until a Content-Length header is seen
    while (req.size() < MAX_REQ) {
        size_t he = req.find("\r\n\r\n");
        if (he != std::string::npos) {
            if (content_len == std::string::npos) {
                std::string head = req.substr(0, he);
                for (char& c : head) c = (char)tolower((unsigned char)c);
                size_t cp = head.find("content-length:");
                if (cp != std::string::npos) content_len = (size_t)strtoul(head.c_str() + cp + 15, nullptr, 10);
            }
            if (content_len == std::string::npos) break;               // no body length: take what we have
            if (req.size() - (he + 4) >= content_len) break;           // full body received
        }
        ssize_t m = recv(fd, buf, sizeof(buf), 0);
        if (m <= 0) break;                                             // peer closed or timed out
        req.append(buf, (size_t)m);
    }

    std::string method = req.substr(0, req.find(' '));
    size_t p1 = req.find(' ');
    std::string path;
    if (p1 != std::string::npos) {
        p1 += 1;
        size_t p2 = req.find(' ', p1);
        path = req.substr(p1, p2 - p1);
    }
    size_t bodyPos = req.find("\r\n\r\n");
    if (bodyPos == std::string::npos)      body_ = "";
    else if (content_len != std::string::npos) body_ = req.substr(bodyPos + 4, content_len);
    else                                   body_ = req.substr(bodyPos + 4);

    // Parse request headers (lowercased keys) so hasHeader/header work.
    headers_.clear();
    size_t hdr_limit = (bodyPos == std::string::npos) ? req.size() : bodyPos;
    size_t line_start = req.find("\r\n");
    if (line_start != std::string::npos) line_start += 2;
    while (line_start != std::string::npos && line_start < hdr_limit) {
        size_t eol = req.find("\r\n", line_start);
        if (eol == std::string::npos || eol > hdr_limit) break;
        std::string line = req.substr(line_start, eol - line_start);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon), val = line.substr(colon + 1);
            while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(0, 1);
            for (char& ch : key) ch = (char)tolower((unsigned char)ch);
            headers_[key] = val;
        }
        line_start = eol + 2;
    }

    client_fd_ = fd;
    auto& table = (method == "POST") ? post_ : get_;
    auto it = table.find(path);
    if (it != table.end()) {
        // Drive a multipart file upload (e.g. OTA /update) before the finish
        // handler, so firmware whose real work happens in the upload callback
        // (Update.begin/write/end) can reach its success path in the sim. This
        // ordering mirrors real Arduino WebServer, which streams the body into
        // the upload handler before invoking the main handler; there is no
        // route-level auth gate on hardware, so an upload handler must
        // authorize itself (Clawdmeter's checks X-Clawdmeter at
        // UPLOAD_FILE_START and rejects before touching Update). Keeping the
        // shim faithful here means a firmware that forgets that guard fails in
        // the sim exactly as it would on the device.
        if (method == "POST") {
            auto uh = upload_handlers_.find(path);
            if (uh != upload_handlers_.end()) drive_upload(uh->second);
        }
        it->second();
    }
    else send(404, "text/plain", String("not found\n"));
    close(client_fd_);
    client_fd_ = -1;
}

// Parse a multipart/form-data body and drive the upload callback through the
// START/WRITE/END lifecycle for the first part that carries a filename. If the
// body is not multipart or has no file part, the callback is not driven and the
// finish handler sees no upload (faithful "no firmware" path).
static bool extract_file_part(const std::string& body, const std::string& ctype,
                              std::string& out_data, std::string& out_filename) {
    size_t bp = ctype.find("boundary=");
    if (bp == std::string::npos) return false;
    std::string boundary = ctype.substr(bp + 9);
    if (!boundary.empty() && boundary.front() == '"') {
        boundary.erase(0, 1);
        size_t q = boundary.find('"');
        if (q != std::string::npos) boundary.erase(q);
    } else {
        size_t sc = boundary.find_first_of("; \r\n");
        if (sc != std::string::npos) boundary.erase(sc);
    }
    if (boundary.empty()) return false;
    std::string delim = "--" + boundary;
    size_t pos = body.find(delim);
    while (pos != std::string::npos) {
        size_t part_start = pos + delim.size();
        size_t next = body.find(delim, part_start);
        if (next == std::string::npos) break;
        std::string part = body.substr(part_start, next - part_start);
        size_t hend = part.find("\r\n\r\n");
        if (hend != std::string::npos) {
            std::string phdr = part.substr(0, hend);
            if (phdr.find("filename=") != std::string::npos) {
                std::string content = part.substr(hend + 4);
                if (content.size() >= 2 && content.compare(content.size() - 2, 2, "\r\n") == 0)
                    content.erase(content.size() - 2);   // strip the CRLF before the delimiter
                out_data = content;
                size_t fn = phdr.find("filename=\"");
                if (fn != std::string::npos) {
                    fn += 10;
                    size_t fe = phdr.find('"', fn);
                    out_filename = phdr.substr(fn, fe == std::string::npos ? std::string::npos : fe - fn);
                }
                return true;
            }
        }
        pos = next;
    }
    return false;
}

void WebServer::drive_upload(const Handler& cb) {
    auto ct = headers_.find("content-type");
    if (ct == headers_.end() || ct->second.find("multipart/form-data") == std::string::npos) return;
    std::string data, fname;
    if (!extract_file_part(body_, ct->second, data, fname)) return;

    upload_ = HTTPUpload{};
    upload_.filename = String(fname.c_str());
    upload_.name     = String("firmware");

    upload_.status = UPLOAD_FILE_START;
    cb();
    upload_.status      = UPLOAD_FILE_WRITE;
    upload_.buf         = (uint8_t*)data.data();
    upload_.currentSize = data.size();
    cb();
    upload_.status      = UPLOAD_FILE_END;
    upload_.buf         = nullptr;
    upload_.totalSize   = data.size();
    upload_.currentSize = 0;
    cb();
}

void WebServer::send(int code, const char* type, const String& body) {
    if (client_fd_ < 0) return;
    std::string b = body.c_str();
    char hdr[256];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        code, type, b.size());
    ::send(client_fd_, hdr, (size_t)hn, 0);
    ::send(client_fd_, b.data(), b.size(), 0);
}

bool WebServer::hasArg(const char* name) {
    return std::string(name) == "plain" && !body_.empty();
}
String WebServer::arg(const char* name) {
    return (std::string(name) == "plain") ? String(body_.c_str()) : String("");
}

// The sim collects every request header, so the declared list is a no-op.
void WebServer::collectHeaders(const char**, size_t) {}

static std::string lower(const char* s) {
    std::string r(s ? s : "");
    for (char& c : r) c = (char)tolower((unsigned char)c);
    return r;
}
bool WebServer::hasHeader(const char* name) { return headers_.count(lower(name)) != 0; }
String WebServer::header(const char* name) {
    auto it = headers_.find(lower(name));
    return it == headers_.end() ? String("") : String(it->second.c_str());
}

void WebServer::stop() {
    if (listen_fd_ >= 0) { close(listen_fd_); listen_fd_ = -1; }
}
