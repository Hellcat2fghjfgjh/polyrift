// Dependency-free HTTP + WebSocket server (single thread, select() based).
// Serves the static client from a folder and upgrades /ws/* requests to WebSockets.
#pragma once
#include <cstdint>
#include <functional>
#include <list>
#include <string>

namespace net {

#ifdef _WIN32
using sock_t = uintptr_t;
#else
using sock_t = int;
#endif

struct Conn {
    sock_t fd = 0;
    std::string in, out;
    size_t outPos = 0;
    bool ws = false;       // upgraded to WebSocket
    bool closing = false;  // close once the output buffer is flushed
    bool dead = false;     // remove at end of poll
    std::string path;      // request path of the WebSocket upgrade
    std::string ip;        // peer address (or the proxy-reported one with trustProxy)
    void* user = nullptr;  // owned by the application
    int64_t created = 0, lastActive = 0;  // milliseconds, steady clock
    int64_t msgWindow = 0;
    int msgCount = 0;
};

class Server {
public:
    std::function<void(Conn&)> onOpen;
    std::function<void(Conn&)> onClose;
    std::function<void(Conn&, const uint8_t*, size_t)> onMessage;

    // Abuse limits. Sensible defaults for a small public server; all adjustable from main.cpp.
    int maxConns = 600;        // total sockets
    int maxPerIp = 6;          // WebSocket connections per address, 0 = unlimited
    bool trustProxy = false;   // take the client address from X-Forwarded-For (only behind your own reverse proxy)
    int httpTimeoutMs = 10000; // a plain HTTP request must finish within this
    int wsIdleMs = 45000;      // drop WebSockets that send nothing for this long (the client pings every 10 s)
    int maxMsgsPerSec = 200;   // per connection

    std::function<std::string()> onStatus;  // body for GET /status (JSON)

    // bindAddr is a dotted IPv4 address: "0.0.0.0" = reachable from other machines, "127.0.0.1" = this machine only.
    bool listen(int port, const std::string& publicDir, const std::string& bindAddr = "0.0.0.0");
    void shutdown();  // flush what we can and close every socket
    // Wait up to timeoutMs for network activity and process it.
    void poll(int timeoutMs);
    void send(Conn& c, const std::string& payload, bool binary);
    size_t connections() const { return conns_.size(); }

private:
    void handleRead(Conn& c);
    bool handleHttp(Conn& c);
    void handleFrames(Conn& c);
    void serveFile(Conn& c, std::string path);
    void flush(Conn& c);

    sock_t lfd_ = 0;
    std::string dir_;
    std::list<Conn> conns_;
};

}  // namespace net
