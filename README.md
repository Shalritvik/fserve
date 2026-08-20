# fserve

A CPU-only LLM inference server in C++20, built to understand serving internals
from the ground up: continuous batching, queue backpressure, SSE streaming, and
graceful drain.

## Scope, stated honestly

This serves **one small quantized model on consumer CPU hardware**. It is a
learning build, not a production system:

- Target model is Llama 3.2 1B Instruct, Q4_K_M GGUF (~0.8 GB).
- **CPU only.** No CUDA, no Metal, no GPU path of any kind.
- Reference hardware for every benchmark number in this repo is an
  AMD Ryzen 7 6800H (8 cores / 16 threads) under WSL2.
- Throughput figures are what that machine does. They are not extrapolations
  and should not be read as representative of served models at scale.

The engine is llama.cpp, linked directly as a library against the `llama.h`
C API. It does not shell out to `llama-server` or `llama-cli` -- owning the
engine layer is the point of the exercise.

## Architecture

    HTTP threads          bounded queue        one batcher thread
    (cpp-httplib)   -->   (429 when full)  --> (owns llama_context)
                                                       |
                                                  Engine::step()
                                            one llama_decode over all
                                            live seq_ids in one batch

Two properties do the load-bearing work:

1. **One decode thread.** `llama_context` is not safe for concurrent
   `llama_decode`, so exactly one thread ever calls into it. That is enforced
   by construction rather than by a lock.
2. **One `step()` per iteration, not per sequence.** Model weights stream out
   of memory once and are reused across every live sequence in the batch.

## Build

Requires g++-12, CMake 3.22+, Ninja.

    cmake --preset dev
    cmake --build --preset dev
    ctest --preset dev

The real engine is behind `FSERVE_WITH_LLAMA` (default `OFF`). With it off the
build needs no model and no llama.cpp checkout: the mock engine exercises the
queue, scheduler, backpressure and shutdown paths on their own.

## Status

Under construction, phase by phase. See git history.
