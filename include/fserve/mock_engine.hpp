#pragma once

#include <chrono>
#include <cstddef>
#include <unordered_map>
#include <vector>

#include "fserve/engine.hpp"

namespace fserve {

struct MockConfig {
    // Simulated cost of one decode step. Zero by default so logic tests stay
    // fast; tests that are *about* timing (queue depth, 429, drain) set this
    // explicitly, which makes the delay part of the test's stated intent.
    //
    // Constant regardless of batch size, on purpose. Real decode latency is
    // near-flat as the batch grows and then rises once KV traffic overtakes
    // weight traffic. Approximating that here would produce a fake scaling
    // curve, and a fake curve is worse than none. This mock exists to exercise
    // scheduler logic; every performance number comes from the real engine.
    std::chrono::milliseconds step_delay{0};

    // How many tokens a sequence emits before signalling EOS.
    //
    // Note this is the *engine's* stopping point, independent of
    // GenParams::max_tokens. EOS belongs to the engine; max_tokens is the
    // scheduler's to enforce. Setting tokens_per_seq far above max_tokens is
    // how a test proves the scheduler is actually doing that job.
    int tokens_per_seq = 16;

    // Concurrent sequences, i.e. simulated KV slots.
    int capacity = 4;
};

// A canned-token Engine that needs no model, so the queue, scheduler,
// backpressure and shutdown paths can all be tested on their own -- and so CI
// runs in seconds instead of building llama.cpp.
class MockEngine final : public Engine {
public:
    explicit MockEngine(MockConfig cfg = {});

    int capacity() const override;
    std::optional<SeqId> begin(const GenParams& params,
                               std::string_view prompt) override;
    void release(SeqId seq) override;
    std::vector<StepOut> step(std::span<const SeqId> active) override;
    std::string detokenize(SeqId seq, TokenId token) override;
    std::string name() const override;

    // Test hooks -- how many slots are currently claimed, and the largest
    // batch step() has ever seen. The latter is how a test proves that
    // sequences were genuinely batched rather than serialized.
    int live_count() const;
    std::size_t max_batch_seen() const;

private:
    struct SeqState {
        int emitted = 0;
    };

    MockConfig cfg_;
    std::unordered_map<SeqId, SeqState> live_;
    std::vector<bool> slot_used_;
    std::size_t max_batch_seen_ = 0;
};

}  // namespace fserve
