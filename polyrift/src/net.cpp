#include "net.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
static void closeSock(net::sock_t s) { closesocket((SOCKET)s); }
static bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
static void setNonBlocking(net::sock_t s) {
    u_long m = 1;
    ioctlsocket((SOCKET)s, FIONBIO, &m);
}
#define SEND_FLAGS 0
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
static void closeSock(net::sock_t s) { ::close(s); }
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
static void setNonBlocking(net::sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif
#endif

namespace net {

// ---------------------------------------------------------------- SHA-1 / Base64
// Only needed for the WebSocket handshake (Sec-WebSocket-Accept).
static std::string sha1(const std::string& msg) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string m = msg;
    uint64_t bits = uint64_t(msg.size()) * 8;
    m.push_back(char(0x80));
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; i--) m.push_back(char((bits >> (i * 8)) & 0xff));
    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t(uint8_t(m[off + i * 4])) << 24) | (uint32_t(uint8_t(m[off + i * 4 + 1])) << 16) |
                   (uint32_t(uint8_t(m[off + i * 4 + 2])) << 8) | uint32_t(uint8_t(m[off + i * 4 + 3]));
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::string out;
    for (uint32_t v : h)
        for (int i = 3; i >= 0; i--) out.push_back(char((v >> (i * 8)) & 0xff));
    return out;
}

static std::string base64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) | uint8_t(in[i + 2]);
        out.push_back(T[(v >> 18) & 63]); out.push_back(T[(v >> 12) & 63]);
        out.push_back(T[(v >> 6) & 63]);  out.push_back(T[v & 63]);
    }
    if (i + 1 == in.size()) {
        uint32_t v = uint8_t(in[i]) << 16;
        out.push_back(T[(v >> 18) & 63]); out.push_back(T[(v >> 12) & 63]);
        out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8);
        out.push_back(T[(v >> 18) & 63]); out.push_back(T[(v >> 12) & 63]);
        out.push_back(T[(v >> 6) & 63]);  out.push_back('=');
    }
    return out;
}

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

// ---------------------------------------------------------------- server
bool Server::listen(int port, const std::string& publicDir, const std::string& bindAddr) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    dir_ = publicDir;
    lfd_ = (sock_t)::socket(AF_INET, SOCK_STREAM, 0);
#ifndef _WIN32
    // Lets the server restart immediately after a crash or redeploy. (On Windows this flag would
    // allow two servers on one port, so it is left off there.)
    int yes = 1;
    setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(bindAddr.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return false;
    addr.sin_addr.s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
    addr.sin_port = htons((uint16_t)port);
    if (::bind(lfd_, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
    if (::listen(lfd_, 64) != 0) return false;
    setNonBlocking(lfd_);
    return true;
}

void Server::poll(int timeoutMs) {
    fd_set rs, wsn;
    FD_ZERO(&rs);
    FD_ZERO(&wsn);
    FD_SET(lfd_, &rs);
    sock_t maxfd = lfd_;
    for (Conn& c : conns_) {
        FD_SET(c.fd, &rs);
        if (c.out.size() > c.outPos) FD_SET(c.fd, &wsn);
        maxfd = std::max(maxfd, c.fd);
    }
    timeval tv;
    if (timeoutMs < 0) timeoutMs = 0;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int n = ::select(int(maxfd) + 1, &rs, &wsn, nullptr, &tv);
    if (n > 0) {
        for (Conn& c : conns_) {
            if (c.dead) continue;
            if (FD_ISSET(c.fd, &rs)) handleRead(c);
            if (!c.dead && FD_ISSET(c.fd, &wsn)) flush(c);
        }
        if (FD_ISSET(lfd_, &rs)) {
            for (;;) {
                sockaddr_in peer{};
#ifdef _WIN32
                int plen = sizeof(peer);
                sock_t fd = (sock_t)::accept(lfd_, (sockaddr*)&peer, &plen);
                if (fd == (sock_t)INVALID_SOCKET) break;
#else
                socklen_t plen = sizeof(peer);
                sock_t fd = (sock_t)::accept(lfd_, (sockaddr*)&peer, &plen);
                if (fd < 0) break;
                if (fd >= FD_SETSIZE) { closeSock(fd); continue; }
#endif
                if (int(conns_.size()) >= maxConns) { closeSock(fd); continue; }
                setNonBlocking(fd);
                int yes = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&yes, sizeof(yes));
                uint32_t a = ntohl(peer.sin_addr.s_addr);
                conns_.emplace_back();
                Conn& c = conns_.back();
                c.fd = fd;
                c.ip = std::to_string(a >> 24) + "." + std::to_string((a >> 16) & 255) + "." +
                       std::to_string((a >> 8) & 255) + "." + std::to_string(a & 255);
                c.created = c.lastActive = nowMs();
            }
        }
    }
    // Slow or silent connections are not allowed to hold a slot forever.
    int64_t now = nowMs();
    for (Conn& c : conns_) {
        if (c.dead) continue;
        if (!c.ws && now - c.created > httpTimeoutMs) c.dead = true;
        if (c.ws && now - c.lastActive > wsIdleMs) c.dead = true;
    }
    for (auto it = conns_.begin(); it != conns_.end();) {
        if (it->dead) {
            if (it->ws && onClose) onClose(*it);
            closeSock(it->fd);
            it = conns_.erase(it);
        } else {
            ++it;
        }
    }
}

void Server::shutdown() {
    for (Conn& c : conns_) {
        if (!c.dead) flush(c);
        if (c.ws && onClose) onClose(c);
        closeSock(c.fd);
    }
    conns_.clear();
    closeSock(lfd_);
}

void Server::handleRead(Conn& c) {
    char buf[8192];
    for (;;) {
        int n = (int)::recv(c.fd, buf, sizeof(buf), 0);
        if (n > 0) {
            c.in.append(buf, n);
            if (c.in.size() > (1u << 20)) { c.dead = true; return; }
            if (n < (int)sizeof(buf)) break;
        } else if (n == 0) {
            c.dead = true;
            return;
        } else {
            if (!wouldBlock()) c.dead = true;
            break;
        }
    }
    if (c.dead) return;
    if (!c.ws) {
        if (!handleHttp(c)) return;
    }
    if (c.ws) handleFrames(c);
}

// Returns true when the connection was upgraded to a WebSocket.
bool Server::handleHttp(Conn& c) {
    size_t end = c.in.find("\r\n\r\n");
    if (end == std::string::npos) return false;
    std::string head = c.in.substr(0, end);
    c.in.erase(0, end + 4);

    std::istringstream ss(head);
    std::string line, method, path;
    std::getline(ss, line);
    {
        std::istringstream rl(line);
        rl >> method >> path;
    }
    std::map<std::string, std::string> hdr;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string k = lower(line.substr(0, colon));
        std::string v = line.substr(colon + 1);
        size_t a = v.find_first_not_of(' ');
        hdr[k] = a == std::string::npos ? "" : v.substr(a);
    }

    if (lower(hdr["upgrade"]).find("websocket") != std::string::npos && hdr.count("sec-websocket-key")) {
        if (trustProxy && hdr.count("x-forwarded-for")) {
            // The right-most entry is the one our own reverse proxy appended.
            std::string xff = hdr["x-forwarded-for"];
            size_t comma = xff.rfind(',');
            std::string last = comma == std::string::npos ? xff : xff.substr(comma + 1);
            size_t a = last.find_first_not_of(" \t"), b = last.find_last_not_of(" \t");
            if (a != std::string::npos) c.ip = last.substr(a, b - a + 1).substr(0, 64);
        }
        if (maxPerIp > 0) {
            int same = 0;
            for (const Conn& o : conns_)
                if (o.ws && !o.dead && o.ip == c.ip) same++;
            if (same >= maxPerIp) {
                c.out += "HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                c.closing = true;
                flush(c);
                return false;
            }
        }
        std::string accept = base64(sha1(hdr["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
        c.out += "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                 "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
        c.ws = true;
        c.lastActive = nowMs();
        c.path = path.substr(0, path.find('?'));
        flush(c);
        if (onOpen) onOpen(c);
        return !c.dead;
    }
    if (method != "GET") {
        c.out += "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        c.closing = true;
        flush(c);
        return false;
    }
    if (path == "/status" && onStatus) {
        std::string body = onStatus();
        c.out += "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
                 "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + body;
        c.closing = true;
        flush(c);
        return false;
    }
    serveFile(c, path);
    return false;
}

void Server::serveFile(Conn& c, std::string path) {
    path = path.substr(0, path.find('?'));
    if (path == "/") path = "/index.html";
    bool ok = !path.empty() && path[0] == '/' && path.find("..") == std::string::npos;
    for (char ch : path)
        if (!(std::isalnum((unsigned char)ch) || ch == '.' || ch == '_' || ch == '-' || ch == '/')) ok = false;
    std::string body;
    if (ok) {
        std::ifstream f(dir_ + path, std::ios::binary);
        if (f) body.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        else ok = false;
    }
    if (!ok) {
        std::string msg = "404 Not Found";
        c.out += "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: " +
                 std::to_string(msg.size()) + "\r\nConnection: close\r\n\r\n" + msg;
    } else {
        std::string ext = path.substr(path.rfind('.') + 1);
        const char* type = "application/octet-stream";
        if (ext == "html") type = "text/html; charset=utf-8";
        else if (ext == "js") type = "text/javascript; charset=utf-8";
        else if (ext == "css") type = "text/css; charset=utf-8";
        else if (ext == "json") type = "application/json";
        else if (ext == "png") type = "image/png";
        else if (ext == "svg") type = "image/svg+xml";
        else if (ext == "ico") type = "image/x-icon";
        c.out += "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(type) + "\r\nContent-Length: " +
                 std::to_string(body.size()) + "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
        c.out += body;
    }
    c.closing = true;
    flush(c);
}

void Server::handleFrames(Conn& c) {
    size_t pos = 0;
    while (!c.dead && !c.closing) {
        size_t avail = c.in.size() - pos;
        if (avail < 2) break;
        const uint8_t* p = (const uint8_t*)c.in.data() + pos;
        int opcode = p[0] & 0x0f;
        bool masked = (p[1] & 0x80) != 0;
        uint64_t len = p[1] & 0x7f;
        size_t hl = 2;
        if (len == 126) {
            if (avail < 4) break;
            len = (uint64_t(p[2]) << 8) | p[3];
            hl = 4;
        } else if (len == 127) {
            if (avail < 10) break;
            len = 0;
            for (int i = 0; i < 8; i++) len = (len << 8) | p[2 + i];
            hl = 10;
        }
        if (!masked || len > 65536) { c.dead = true; break; }  // clients must mask; our messages are tiny
        if (avail < hl + 4 + len) break;
        uint8_t mask[4] = {p[hl], p[hl + 1], p[hl + 2], p[hl + 3]};
        uint8_t* data = (uint8_t*)&c.in[pos + hl + 4];
        for (uint64_t i = 0; i < len; i++) data[i] ^= mask[i & 3];
        pos += hl + 4 + size_t(len);

        int64_t now = nowMs();
        c.lastActive = now;
        if (now - c.msgWindow >= 1000) { c.msgWindow = now; c.msgCount = 0; }
        if (++c.msgCount > maxMsgsPerSec) { c.dead = true; break; }  // flooding

        if (opcode == 1 || opcode == 2) {
            if (onMessage) onMessage(c, data, size_t(len));
        } else if (opcode == 8) {  // close
            c.out.push_back(char(0x88));
            c.out.push_back(char(0));
            c.closing = true;
            flush(c);
        } else if (opcode == 9) {  // ping -> pong
            if (len < 126) {
                c.out.push_back(char(0x8A));
                c.out.push_back(char(len));
                c.out.append((const char*)data, size_t(len));
                flush(c);
            }
        }
    }
    if (pos > 0) c.in.erase(0, pos);
}

void Server::send(Conn& c, const std::string& payload, bool binary) {
    if (c.dead || c.closing || !c.ws) return;
    size_t len = payload.size();
    c.out.push_back(char(0x80 | (binary ? 2 : 1)));
    if (len < 126) {
        c.out.push_back(char(len));
    } else if (len < 65536) {
        c.out.push_back(char(126));
        c.out.push_back(char((len >> 8) & 0xff));
        c.out.push_back(char(len & 0xff));
    } else {
        c.out.push_back(char(127));
        for (int i = 7; i >= 0; i--) c.out.push_back(char((uint64_t(len) >> (i * 8)) & 0xff));
    }
    c.out += payload;
    if (c.out.size() - c.outPos > (4u << 20)) {  // hopelessly slow client
        c.dead = true;
        return;
    }
    flush(c);
}

void Server::flush(Conn& c) {
    while (c.out.size() > c.outPos) {
        int n = (int)::send(c.fd, c.out.data() + c.outPos, int(std::min<size_t>(c.out.size() - c.outPos, 1 << 16)),
                            SEND_FLAGS);
        if (n > 0) {
            c.outPos += size_t(n);
        } else {
            if (!wouldBlock()) c.dead = true;
            break;
        }
    }
    if (c.outPos == c.out.size()) {
        c.out.clear();
        c.outPos = 0;
        if (c.closing) c.dead = true;
    } else if (c.outPos > (1u << 16)) {
        c.out.erase(0, c.outPos);
        c.outPos = 0;
    }
}

}  // namespace net
