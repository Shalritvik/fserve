#include "fserve/server.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <string>
#include <vector>

namespace fserve {

using json = nlohmann::json;

namespace {

void send_error(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(json{{"error", message}}.dump(), "application/json");
}

}  // namespace

class Server::Impl {
public:
    Impl(Engine& engine, ServerConfig cfg) : engine_(engine), cfg_(std::move(cfg)) {
        install_routes();
    }

    int bind() {
        // port 0 means "any free port"; the caller needs the one we got.
        if (cfg_.port == 0) {
            const int port = http_.bind_to_any_port(cfg_.host);
            return port > 0 ? port : -1;
        }
        return http_.bind_to_port(cfg_.host, cfg_.port) ? cfg_.port : -1;
    }

    void run() { http_.listen_after_bind(); }

    void stop() { http_.stop(); }

private:
    void install_routes() {
        http_.Get("/health", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content(json{{"status", "ok"},
                                 {"engine", engine_.name()},
                                 {"capacity", engine_.capacity()}}
                                .dump(),
                            "application/json");
        });

        http_.Post("/generate", [this](const httplib::Request& req, httplib::Response& res) {
            handle_generate(req, res);
        });
    }

    void handle_generate(const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (const json::exception& e) {
            send_error(res, 400, std::string("malformed JSON: ") + e.what());
            return;
        }

        if (!body.contains("prompt") || !body["prompt"].is_string()) {
            send_error(res, 400, "field 'prompt' is required and must be a string");
            return;
        }
        const auto prompt = body["prompt"].get<std::string>();
        if (prompt.empty()) {
            send_error(res, 400, "field 'prompt' must not be empty");
            return;
        }

        GenParams params;
        if (body.contains("max_tokens")) {
            if (!body["max_tokens"].is_number_integer()) {
                send_error(res, 400, "field 'max_tokens' must be an integer");
                return;
            }
            params.max_tokens = body["max_tokens"].get<int>();
            if (params.max_tokens < 1 || params.max_tokens > 4096) {
                send_error(res, 400, "field 'max_tokens' must be in [1, 4096]");
                return;
            }
        }

        // ------------------------------------------------------------------
        // PHASE 1 SCAFFOLD -- Phase 2 deletes this mutex.
        //
        // cpp-httplib runs a thread per connection, but Engine's contract says
        // one caller only. A mutex satisfies the contract and is otherwise a
        // bad answer: it serializes requests end to end, so the batch is
        // always size 1 and there is no batching at all. Concurrency here is
        // exactly 1 no matter how many clients connect.
        //
        // That failure is the motivation for Phase 2: a bounded queue plus one
        // batcher thread, so requests interleave inside a shared llama_decode
        // instead of queueing behind a lock.
        // ------------------------------------------------------------------
        std::lock_guard<std::mutex> guard(engine_mu_);

        const auto seq = engine_.begin(params, prompt);
        if (!seq) {
            send_error(res, 503, "engine at capacity");
            return;
        }

        std::string text;
        int emitted = 0;
        FinishReason reason = FinishReason::MaxTokens;
        const SeqId active_seq = *seq;

        while (emitted < params.max_tokens) {
            const auto outs = engine_.step(std::span<const SeqId>(&active_seq, 1));
            if (outs.empty()) {
                reason = FinishReason::Error;
                break;
            }

            const StepOut& out = outs.front();
            text += engine_.detokenize(out.seq, out.token);
            ++emitted;

            // EOS is the engine's signal. max_tokens is enforced by the loop
            // condition above -- a serving concern, not the engine's.
            if (out.eos) {
                reason = FinishReason::Eos;
                break;
            }
        }

        engine_.release(active_seq);

        res.set_content(json{{"text", text},
                             {"tokens", emitted},
                             {"finish_reason", to_string(reason)}}
                            .dump(),
                        "application/json");
    }

    Engine& engine_;
    ServerConfig cfg_;
    httplib::Server http_;
    std::mutex engine_mu_;
};

Server::Server(Engine& engine, ServerConfig cfg)
    : impl_(std::make_unique<Impl>(engine, std::move(cfg))) {}

Server::~Server() = default;

int Server::bind() {
    return impl_->bind();
}

void Server::run() {
    impl_->run();
}

void Server::stop() {
    impl_->stop();
}

}  // namespace fserve
