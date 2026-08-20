// Composition root. Parses flags, picks an Engine implementation, and hands it
// to the Server. Deliberately holds no logic of its own -- everything worth
// testing lives in fserve_core, which the test binary links directly.

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "fserve/mock_engine.hpp"
#include "fserve/server.hpp"

namespace {

fserve::Server* g_server = nullptr;

// PHASE 1 SCAFFOLD. Calling stop() from a signal handler is not strictly
// async-signal-safe. Phase 3 replaces this with a proper shutdown path that
// also drains in-flight work rather than just closing the listener.
void on_signal(int) {
    if (g_server != nullptr) {
        g_server->stop();
    }
}

void print_usage() {
    std::cout <<
        "fserve -- CPU-only LLM inference server (learning build)\n"
        "\n"
        "  --host <addr>        listen address (default 127.0.0.1)\n"
        "  --port <n>           listen port, 0 for ephemeral (default 8080)\n"
        "  --capacity <n>       concurrent sequences / KV slots (default 4)\n"
        "  --mock-tokens <n>    tokens the mock emits before EOS (default 16)\n"
        "  --mock-delay-ms <n>  simulated cost of one decode step (default 0)\n"
        "  --help\n";
}

bool parse_int(const char* text, int& out) {
    try {
        out = std::stoi(text);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

int main(int argc, char** argv) {
    fserve::ServerConfig server_cfg;
    fserve::MockConfig mock_cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = (i + 1 < argc);

        auto need_int = [&](int& target) {
            int value = 0;
            if (!has_value || !parse_int(argv[i + 1], value)) {
                std::cerr << "fserve: " << arg << " requires an integer value\n";
                std::exit(2);
            }
            target = value;
            ++i;
        };

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--host" && has_value) {
            server_cfg.host = argv[++i];
        } else if (arg == "--port") {
            need_int(server_cfg.port);
        } else if (arg == "--capacity") {
            need_int(mock_cfg.capacity);
        } else if (arg == "--mock-tokens") {
            need_int(mock_cfg.tokens_per_seq);
        } else if (arg == "--mock-delay-ms") {
            int ms = 0;
            need_int(ms);
            mock_cfg.step_delay = std::chrono::milliseconds(ms);
        } else {
            std::cerr << "fserve: unrecognised argument '" << arg << "'\n";
            print_usage();
            return 2;
        }
    }

    // The only place an implementation is named. Phase 5 adds a --engine flag
    // that selects the llama.cpp engine here; nothing downstream changes.
    fserve::MockEngine engine(mock_cfg);
    fserve::Server server(engine, server_cfg);

    const int port = server.bind();
    if (port < 0) {
        std::cerr << "fserve: failed to bind " << server_cfg.host << ":"
                  << server_cfg.port << "\n";
        return 1;
    }

    g_server = &server;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    std::cout << "fserve listening on " << server_cfg.host << ":" << port
              << "  engine=" << engine.name()
              << "  capacity=" << engine.capacity() << "\n";

    server.run();

    std::cout << "fserve stopped\n";
    return 0;
}
