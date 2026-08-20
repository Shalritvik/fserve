#pragma once

#include <cstdint>

namespace fserve {

// These alias the llama.cpp C API types deliberately. When the real engine
// lands in Phase 5, llama_seq_id and llama_token drop in without a cast.
using SeqId = std::int32_t;
using TokenId = std::int32_t;

inline constexpr SeqId kInvalidSeq = -1;

// Sampling knobs. Owned by the engine, not the scheduler: shipping raw logits
// across the interface would mean ~128k floats per sequence per step, and
// llama.cpp's sampler chain is already tested code.
struct GenParams {
    int max_tokens = 128;
    float temperature = 0.8f;
    float top_p = 0.95f;
    int top_k = 40;
    std::uint32_t seed = 0;  // 0 == pick one nondeterministically
};

// Why a sequence stopped.
//
// Only Eos originates in the engine -- it is the model saying it is done.
// MaxTokens, Cancelled and Error are all *serving* concerns and are decided
// by the scheduler, which is why the Engine interface never mentions them.
enum class FinishReason {
    Eos,
    MaxTokens,
    Cancelled,
    Error,
};

constexpr const char* to_string(FinishReason r) noexcept {
    switch (r) {
        case FinishReason::Eos:       return "eos";
        case FinishReason::MaxTokens: return "max_tokens";
        case FinishReason::Cancelled: return "cancelled";
        case FinishReason::Error:     return "error";
    }
    return "unknown";
}

}  // namespace fserve
