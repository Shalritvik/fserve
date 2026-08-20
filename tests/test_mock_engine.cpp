#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "fserve/mock_engine.hpp"

namespace fserve {
namespace {

TEST(MockEngine, BeginClaimsASlotAndCapacityIsFinite) {
    MockEngine engine(MockConfig{.capacity = 2});
    EXPECT_EQ(engine.capacity(), 2);
    EXPECT_EQ(engine.live_count(), 0);

    const auto a = engine.begin(GenParams{}, "hello");
    const auto b = engine.begin(GenParams{}, "world");
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_NE(*a, *b) << "each sequence needs its own seq_id, or their KV "
                         "cache entries would collide";
    EXPECT_EQ(engine.live_count(), 2);
}

TEST(MockEngine, AtCapacityReturnsNulloptRatherThanFailing) {
    // At-capacity is a normal condition the scheduler handles by waiting.
    // It is deliberately NOT the same signal as a full queue, which is what
    // produces an HTTP 429.
    MockEngine engine(MockConfig{.capacity = 1});
    ASSERT_TRUE(engine.begin(GenParams{}, "first").has_value());
    EXPECT_FALSE(engine.begin(GenParams{}, "second").has_value());
}

TEST(MockEngine, ReleaseReturnsTheSlotToThePool) {
    MockEngine engine(MockConfig{.capacity = 1});
    const auto first = engine.begin(GenParams{}, "first");
    ASSERT_TRUE(first.has_value());
    ASSERT_FALSE(engine.begin(GenParams{}, "blocked").has_value());

    engine.release(*first);
    EXPECT_EQ(engine.live_count(), 0);
    EXPECT_TRUE(engine.begin(GenParams{}, "now admitted").has_value());
}

TEST(MockEngine, ReleaseIsIdempotent) {
    // The scheduler may release on a path that already released; a double
    // release must not corrupt the slot pool.
    MockEngine engine(MockConfig{.capacity = 2});
    const auto seq = engine.begin(GenParams{}, "x");
    ASSERT_TRUE(seq.has_value());

    engine.release(*seq);
    engine.release(*seq);
    engine.release(kInvalidSeq);
    engine.release(9999);

    EXPECT_EQ(engine.live_count(), 0);
    EXPECT_EQ(engine.capacity(), 2);
}

TEST(MockEngine, SignalsEosAtItsOwnCannedLengthNotAtMaxTokens) {
    // EOS belongs to the engine. GenParams::max_tokens is the scheduler's to
    // enforce, so setting it here must not change when the engine stops.
    MockEngine engine(MockConfig{.tokens_per_seq = 3, .capacity = 1});
    GenParams params;
    params.max_tokens = 999;

    const auto seq = engine.begin(params, "prompt");
    ASSERT_TRUE(seq.has_value());
    const SeqId active = *seq;

    int steps = 0;
    bool saw_eos = false;
    while (steps < 10 && !saw_eos) {
        const auto outs = engine.step(std::span<const SeqId>(&active, 1));
        ASSERT_EQ(outs.size(), 1u);
        saw_eos = outs.front().eos;
        ++steps;
    }

    EXPECT_TRUE(saw_eos);
    EXPECT_EQ(steps, 3) << "engine must stop at tokens_per_seq regardless of "
                           "max_tokens";
}

TEST(MockEngine, OneStepAdvancesEverySequenceInTheBatch) {
    // The property the whole design rests on: a single step() call covers all
    // active sequences, rather than the caller looping once per sequence.
    MockEngine engine(MockConfig{.tokens_per_seq = 8, .capacity = 4});

    std::vector<SeqId> active;
    for (int i = 0; i < 4; ++i) {
        const auto seq = engine.begin(GenParams{}, "prompt");
        ASSERT_TRUE(seq.has_value());
        active.push_back(*seq);
    }

    const auto outs = engine.step(active);
    ASSERT_EQ(outs.size(), 4u);
    EXPECT_EQ(engine.max_batch_seen(), 4u);

    std::vector<SeqId> returned;
    for (const auto& out : outs) {
        returned.push_back(out.seq);
        EXPECT_FALSE(out.eos);
    }
    std::sort(returned.begin(), returned.end());
    EXPECT_EQ(returned, active);
}

TEST(MockEngine, StepSkipsSequencesReleasedMidFlight) {
    MockEngine engine(MockConfig{.tokens_per_seq = 8, .capacity = 2});
    const auto a = engine.begin(GenParams{}, "a");
    const auto b = engine.begin(GenParams{}, "b");
    ASSERT_TRUE(a.has_value() && b.has_value());

    engine.release(*a);

    const std::vector<SeqId> active{*a, *b};
    const auto outs = engine.step(active);
    ASSERT_EQ(outs.size(), 1u) << "a released sequence must not produce tokens";
    EXPECT_EQ(outs.front().seq, *b);
}

TEST(MockEngine, DetokenizeProducesTextAndNamesItself) {
    MockEngine engine(MockConfig{.tokens_per_seq = 4, .capacity = 1});
    EXPECT_EQ(engine.name(), "mock");

    const auto seq = engine.begin(GenParams{}, "prompt");
    ASSERT_TRUE(seq.has_value());
    const SeqId active = *seq;

    std::string text;
    for (int i = 0; i < 3; ++i) {
        const auto outs = engine.step(std::span<const SeqId>(&active, 1));
        ASSERT_EQ(outs.size(), 1u);
        text += engine.detokenize(outs.front().seq, outs.front().token);
    }

    EXPECT_EQ(text, "the quick brown")
        << "first token carries no leading space -- detokenize is per-sequence "
           "and stateful";
}

}  // namespace
}  // namespace fserve
