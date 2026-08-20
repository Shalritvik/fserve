#pragma once

#include <memory>
#include <string>

#include "fserve/engine.hpp"

namespace fserve {

struct ServerConfig {
    std::string host = "127.0.0.1";

    // 0 binds an ephemeral port. Tests use that so parallel runs cannot
    // collide on a fixed port; the real port comes back from bind().
    int port = 8080;
};

// HTTP front end. Owns no engine -- it holds a reference, so the composition
// root in main() decides which implementation is in play.
//
// httplib.h is a very large single header, so it stays behind a PIMPL rather
// than being pulled into every translation unit that touches a Server.
class Server {
public:
    Server(Engine& engine, ServerConfig cfg = {});
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Binds the listening socket. Returns the bound port, or -1 on failure.
    // Split from run() so a caller can learn an ephemeral port before serving.
    int bind();

    // Serves until stop(). Blocks. Call after a successful bind().
    void run();

    // Safe to call from another thread.
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fserve
