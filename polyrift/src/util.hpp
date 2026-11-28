// Small shared helpers: vector math, RNG, binary writer/reader, JSON escaping.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

constexpr float PI = 3.14159265f;
constexpr float TAU = 6.28318531f;

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    float len2() const { return x * x + y * y; }
    float len() const { return std::sqrt(len2()); }
    Vec2 norm() const {
        float l = len();
        return l > 1e-5f ? Vec2{x / l, y / l} : Vec2{};
    }
    float angle() const { return std::atan2(y, x); }
};

inline Vec2 fromAngle(float a, float r = 1) { return {std::cos(a) * r, std::sin(a) * r}; }
inline float wrapAngle(float a) {
    while (a > PI) a -= TAU;
    while (a < -PI) a += TAU;
    return a;
}
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Rng {
    std::mt19937 gen;
    explicit Rng(uint32_t seed) : gen(seed) {}
    float uni() { return (gen() >> 8) * (1.0f / 16777216.0f); }       // [0,1)
    float range(float a, float b) { return a + (b - a) * uni(); }
    int irange(int a, int b) { return a + int(gen() % uint32_t(b - a + 1)); } // inclusive
    bool chance(float p) { return uni() < p; }
};

// Little-endian binary writer used for the world-update packets.
class ByteWriter {
public:
    std::string buf;
    void u8(uint32_t v) { buf.push_back(char(v & 0xff)); }
    void u16(uint32_t v) { u8(v); u8(v >> 8); }
    void u32(uint32_t v) { u8(v); u8(v >> 8); u8(v >> 16); u8(v >> 24); }
    void f32(float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        u32(v);
    }
    void str8(const std::string& s) {
        size_t n = std::min<size_t>(s.size(), 255);
        u8(uint32_t(n));
        buf.append(s.data(), n);
    }
    void setU16(size_t at, uint32_t v) {
        buf[at] = char(v & 0xff);
        buf[at + 1] = char((v >> 8) & 0xff);
    }
};

class ByteReader {
public:
    ByteReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool ok = true;
    size_t left() const { return n_ - pos_; }
    uint32_t u8() {
        if (pos_ + 1 > n_) { ok = false; return 0; }
        return p_[pos_++];
    }
    uint32_t u16() { uint32_t a = u8(); return a | (u8() << 8); }
    uint32_t u32() { uint32_t a = u16(); return a | (u16() << 16); }
    float f32() {
        uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    std::string rest() {
        std::string s(reinterpret_cast<const char*>(p_ + pos_), n_ - pos_);
        pos_ = n_;
        return s;
    }

private:
    const uint8_t* p_;
    size_t n_, pos_ = 0;
};

inline std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20) o += ' ';
        else o.push_back(char(c));
    }
    return o;
}

// Trim a player-supplied name to a safe, valid UTF-8 string of limited length.
inline std::string sanitizeName(const std::string& in, size_t maxChars = 18) {
    std::string out;
    size_t i = 0, chars = 0;
    while (i < in.size() && chars < maxChars) {
        unsigned char c = (unsigned char)in[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > in.size()) break;
        bool good = true;
        for (size_t k = 1; k < n; k++)
            if (((unsigned char)in[i + k] >> 6) != 2) good = false;
        if (!good) break;
        if (n == 1 && c < 0x20) { i++; continue; }
        out.append(in, i, n);
        i += n;
        chars++;
    }
    size_t a = out.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    size_t b = out.find_last_not_of(' ');
    return out.substr(a, b - a + 1);
}

// One line of chat: valid UTF-8 of limited length, without control characters, without the
// invisible and text-direction characters that can hide or scramble a line, and with runs of
// spaces squeezed to one.
inline std::string sanitizeChat(const std::string& in, size_t maxChars = 80) {
    std::string out;
    size_t i = 0, chars = 0;
    bool space = true;  // (so leading spaces are dropped)
    while (i < in.size() && chars < maxChars) {
        unsigned char c = (unsigned char)in[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > in.size()) break;
        uint32_t cp = n == 1 ? c : n == 2 ? (c & 0x1f) : n == 3 ? (c & 0x0f) : (c & 0x07);
        bool good = true;
        for (size_t k = 1; k < n; k++) {
            unsigned char x = (unsigned char)in[i + k];
            if ((x >> 6) != 2) good = false;
            cp = (cp << 6) | (x & 0x3f);
        }
        if (!good) break;
        const bool control = cp < 0x20 || (cp >= 0x7f && cp < 0xa0);
        const bool hidden = (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x202a && cp <= 0x202e) || (cp >= 0x2060 && cp <= 0x2069) ||
                            cp == 0xfeff || cp == 0x00ad || (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff;
        const bool blank = cp == ' ' || cp == 0xa0 || cp == 0x3000 || control;  // a control character counts as a space
        if (hidden || (blank && space)) { i += n; continue; }
        if (blank) out.push_back(' ');
        else out.append(in, i, n);
        space = blank;
        i += n;
        chars++;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}
