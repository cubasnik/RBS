#pragma once
#include <memory>
#include <atomic>

namespace rbs::api {

// ─────────────────────────────────────────────────────────────────────────────
// RestServer — embedded HTTP/JSON REST server (cpp-httplib, no SSL).
//
// Endpoints:
//   GET  /api/v1/status  → version, nodeState, active RATs
//   GET  /api/v1/pm      → all OMS PM counters
//   GET  /api/v1/alarms  → active alarms
//   POST /api/v1/admit   → body: {"imsi":N,"rat":"LTE"} → {"status":"ok","crnti":N}
// ─────────────────────────────────────────────────────────────────────────────
class RestServer {
public:
    // port=0 binds to any available ephemeral port (recommended for tests).
    explicit RestServer(int port = 8080);
    ~RestServer();

    // Non-copyable, non-movable (owns a live socket).
    RestServer(const RestServer&)            = delete;
    RestServer& operator=(const RestServer&) = delete;

    // Start listening (non-blocking — server runs on a background thread).
    // Returns true if the socket was bound successfully.
    bool start();

    // Stop the server and join the background thread.
    void stop();

    bool isRunning() const;

    // Returns the actual bound port (useful when port=0 was requested).
    int  port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rbs::api
