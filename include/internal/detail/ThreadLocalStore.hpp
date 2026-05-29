#pragma once

// TODO: Implement ThreadLocalStore once the threadLocal ADR is resolved.
//
// Open decisions (specs-internal §15.3):
//   1. Store form: native `thread_local` storage (one static per type)
//      vs. an indexed table keyed by (threadId, DescriptorId).
//   2. Bean<T> handle representation for threadLocal (Form 1 variant or separate Form 3).
//   3. Destruction policy: destroy on thread exit (on the exiting thread) vs.
//      on root.stop() for threads still alive at shutdown.
//
// The ADR must address all three before any implementation begins here.
//
// Known contract (specs-api §8):
//   - One instance per context owner, key, and thread.
//   - A threadLocal resolved from a ScopedContext is resolved as if from the root.
//   - Instance is destroyed with the full destruction lifecycle on thread exit.
//   - root.stop() destroys remaining instances on still-alive threads; user code
//     must ensure no concurrent access to those instances at that time.
