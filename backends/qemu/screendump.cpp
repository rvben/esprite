#include "screendump.h"

namespace {

// Reads the next whitespace-delimited header token starting at *pos, skipping
// PPM comment lines ('#' to end of line). Returns false when the input ends
// before a token does.
bool next_token(const std::string& s, size_t* pos, std::string* tok) {
    size_t i = *pos;
    for (;;) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
        if (i < s.size() && s[i] == '#') {
            while (i < s.size() && s[i] != '\n') i++;
            continue;
        }
        break;
    }
    if (i >= s.size()) return false;
    size_t start = i;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n') i++;
    *tok = s.substr(start, i - start);
    *pos = i;
    return true;
}

bool parse_dim(const std::string& tok, int* out) {
    if (tok.empty() || tok.size() > 5) return false;
    long v = 0;
    for (char c : tok) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    // esp_rgb caps at 800x600; 4096 is a defensive bound against a malformed
    // dump asking this process for a giant allocation.
    if (v < 1 || v > 4096) return false;
    *out = (int)v;
    return true;
}

}  // namespace

bool ppm_decode_rgb565(const std::string& ppm, int* w, int* h,
                       std::vector<uint16_t>* px, std::string* err) {
    size_t pos = 0;
    std::string tok;
    if (!next_token(ppm, &pos, &tok) || tok != "P6") {
        if (err) *err = "not a binary PPM (P6 magic missing)";
        return false;
    }
    std::string ws, hs, maxval;
    if (!next_token(ppm, &pos, &ws) || !next_token(ppm, &pos, &hs) ||
        !next_token(ppm, &pos, &maxval)) {
        if (err) *err = "truncated PPM header";
        return false;
    }
    if (!parse_dim(ws, w) || !parse_dim(hs, h)) {
        if (err) *err = "PPM dimensions invalid or out of range (1..4096): " + ws + "x" + hs;
        return false;
    }
    if (maxval != "255") {
        if (err) *err = "unsupported PPM maxval " + maxval + " (only 8-bit supported)";
        return false;
    }
    pos++;   // exactly one whitespace byte separates the header from pixel data
    size_t need = (size_t)*w * (size_t)*h * 3;
    if (ppm.size() < pos + need) {
        if (err) *err = "PPM pixel data truncated";
        return false;
    }
    px->resize((size_t)*w * (size_t)*h);
    const unsigned char* p = (const unsigned char*)ppm.data() + pos;
    for (size_t i = 0; i < px->size(); i++, p += 3) {
        (*px)[i] = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
    }
    return true;
}
