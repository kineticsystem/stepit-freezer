# A Second Check of the Architecture, and the Review Again

Pull request [#24](https://github.com/kineticsystem/stepit-freezer/pull/24), branch `architecture-review`.

## Goal

A second, independent check of the documents, by a stronger model than the one that wrote them: every claim of the architecture document read against the code, and a design review with its suspicions proved by throwaway tests.

This pull request changes documents only. No code, no test and no configuration changes.

## Decisions

- **The architecture document is corrected, not rewritten.** What was wrong: `reconnect_period` 0, the length of a handshake on a silent device (about 1.5 s), the interrupt priorities, the number of threads of the node (three). What was missing: the Depends on and Must never columns of The Packages, the diagram of the includes, Threads, How to Extend.
- **`docs/Review.md` is rewritten for today's commit.** No code changed since the review of 2026-11-02, so none of its findings is fixed. One was wrong for the real driver (the reconnector cannot open a port being closed), one is raised: the old verdict, no safety issue, no longer holds.
- **The lost replies stay a deliberate choice.** The review judges them as the price of `021-remove-lost-reply-recovery.md`, not as bugs: not asking again is right, not dropping the waiting bytes is unfinished.

- **The review fixes nothing.** Each finding says whether a test proved it or it comes from reading, and whether it is a safety issue for the rig. The fixes are separate pull requests, in the order of the review's recommendations.
- **The throwaway tests are not kept.** They ran in the image of StepIt Macro, on a scratch copy of the packages, and were removed. A finding worth a permanent test gets one with its fix.
- **The architecture document describes the design and holds no review.** Where a probe of the review disproved a sentence of it, the sentence is corrected and points to `Review.md`.

## Open Items

- `~/outputs` can tell a pattern the board does not have, after a shot that failed or a `set_outputs` whose reply was lost. A client that writes the pattern back can leave a camera line closed. First recommendation of the review.
- Stopping the node during a shot ends the process with `std::terminate`.
- The board page does not connect under `pnpm run dev`.
- `CLAUDE.md` says MIT headers; the files are BSD-3-Clause.
