#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fserve/request.hpp"

namespace fserve {

// One sampled token, for one sequence, produced by one decode step.
struct StepOut {
    SeqId seq;
    TokenId token;
    bool eos;  // the model emitted end-of-generation
};

// The seam between fserve's scheduler and whatever actually runs the model.
//
// Note what is absent: there is no generate(), no run(), nothing that loops to
// completion. An Engine can only advance every live sequence by exactly one
// token. Deciding *which* sequences are live, when to admit new ones, and when
// to give up on one is the scheduler's job. That absence is the continuous
// batching design -- the loop belongs to fserve, not to the engine.
//
// THREADING CONTRACT
// ------------------
// Every method here is called from the batcher thread and only the batcher
// thread. llama_context is not safe for concurrent llama_decode, so the real
// engine has exactly one caller by construction. There is no mutex in here,
// and adding one would paper over a design violation rather than fix it.
//
// Corollary: no method on this interface may touch a socket. A blocked write
// to a dead client would stall the thread that owns llama_context, and every
// other in-flight sequence with it.
class Engine {
public:
    virtual ~Engine() = default;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Maximum sequences that can be live at once -- i.e. KV cache slots.
    virtual int capacity() const = 0;

    // Tokenize the prompt, claim a KV slot, run prefill.
    //
    // Returns nullopt when the engine is already at capacity. That is a normal
    // condition, not an error: the scheduler leaves the request queued and
    // retries once a slot frees. It is a distinct signal from "the queue is
    // full", which is what produces an HTTP 429 -- different causes, different
    // correct responses.
    virtual std::optional<SeqId> begin(const GenParams& params,
                                       std::string_view prompt) = 0;

    // Release the sequence's KV blocks and return its slot to the pool.
    // Must be idempotent.
    //
    // Skipping this leaks KV cells until admission stalls. Worse, reusing a
    // SeqId without clearing it lets a new request attend over the previous
    // request's cached keys and values -- cross-request contamination that
    // looks like plausible output and is miserable to trace.
    virtual void release(SeqId seq) = 0;

    // Advance every sequence in `active` by exactly one token.
    //
    // This is one llama_decode over one llama_batch holding a distinct seq_id
    // per request. The point is that model weights stream out of memory once
    // and are reused across the whole batch, turning per-sequence GEMVs into
    // a single GEMM. Calling this once per sequence instead would re-pay the
    // dominant memory-bandwidth cost N times per logical iteration.
    //
    // The scheduler owns `active`; the engine does not track liveness itself.
    virtual std::vector<StepOut> step(std::span<const SeqId> active) = 0;

    // Convert one token to text, in the context of its sequence.
    //
    // Stateful and per-sequence on purpose. A llama.cpp token can be a partial
    // UTF-8 sequence -- half a CJK character or an emoji -- so emitting each
    // token's raw bytes would produce invalid UTF-8 in the SSE payload. The
    // engine buffers the incomplete tail, returns "" for a token that does not
    // finish a character, and flushes on a later call. A free function could
    // not do this.
    virtual std::string detokenize(SeqId seq, TokenId token) = 0;

    // Identifies the backing implementation, for /health and logs.
    virtual std::string name() const = 0;

protected:
    Engine() = default;
};

}  // namespace fserve
