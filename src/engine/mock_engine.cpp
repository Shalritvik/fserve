#include "fserve/mock_engine.hpp"

#include <algorithm>
#include <thread>

namespace fserve {
namespace {

// The mock's entire "vocabulary". A TokenId is just an index into this.
const std::vector<std::string>& vocab() {
    static const std::vector<std::string> v = {
        "the", "quick", "brown", "fox",  "jumps", "over",
        "a",   "lazy",  "dog",   "and",  "then",  "stops",
    };
    return v;
}

}  // namespace

MockEngine::MockEngine(MockConfig cfg)
    : cfg_(cfg), slot_used_(static_cast<std::size_t>(std::max(1, cfg.capacity)), false) {}

int MockEngine::capacity() const {
    return static_cast<int>(slot_used_.size());
}

std::optional<SeqId> MockEngine::begin(const GenParams& params,
                                       std::string_view prompt) {
    // The mock ignores both -- it has no tokenizer and no sampler. The real
    // engine tokenizes `prompt` and builds a sampler chain from `params`.
    (void)params;
    (void)prompt;

    const auto it = std::find(slot_used_.begin(), slot_used_.end(), false);
    if (it == slot_used_.end()) {
        return std::nullopt;  // at capacity -- normal, not an error
    }

    *it = true;
    const auto seq = static_cast<SeqId>(std::distance(slot_used_.begin(), it));
    live_[seq] = SeqState{};
    return seq;
}

void MockEngine::release(SeqId seq) {
    // Idempotent: the scheduler may release on a path that already released,
    // and double-release must not corrupt the slot pool.
    if (seq < 0 || static_cast<std::size_t>(seq) >= slot_used_.size()) {
        return;
    }
    slot_used_[static_cast<std::size_t>(seq)] = false;
    live_.erase(seq);
}

std::vector<StepOut> MockEngine::step(std::span<const SeqId> active) {
    // One sleep per step() call, not per sequence -- one step is one decode
    // over the whole batch, however many sequences are in it.
    if (cfg_.step_delay.count() > 0) {
        std::this_thread::sleep_for(cfg_.step_delay);
    }

    max_batch_seen_ = std::max(max_batch_seen_, active.size());

    std::vector<StepOut> out;
    out.reserve(active.size());

    for (const SeqId seq : active) {
        auto it = live_.find(seq);
        if (it == live_.end()) {
            continue;  // released mid-flight; skip rather than fabricate
        }

        SeqState& st = it->second;
        const auto token = static_cast<TokenId>(st.emitted % vocab().size());
        ++st.emitted;

        out.push_back(StepOut{
            .seq = seq,
            .token = token,
            .eos = st.emitted >= cfg_.tokens_per_seq,
        });
    }

    return out;
}

std::string MockEngine::detokenize(SeqId seq, TokenId token) {
    if (token < 0 || static_cast<std::size_t>(token) >= vocab().size()) {
        return {};
    }
    const std::string& word = vocab()[static_cast<std::size_t>(token)];

    // Per-sequence state in action: no leading space on a sequence's first
    // token. The real engine uses this same hook for something harder --
    // buffering a partial UTF-8 sequence until it completes a character.
    const auto it = live_.find(seq);
    const bool first = (it != live_.end() && it->second.emitted <= 1);
    return first ? word : " " + word;
}

std::string MockEngine::name() const {
    return "mock";
}

int MockEngine::live_count() const {
    return static_cast<int>(live_.size());
}

std::size_t MockEngine::max_batch_seen() const {
    return max_batch_seen_;
}

}  // namespace fserve
