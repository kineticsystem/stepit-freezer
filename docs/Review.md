# StepIt Freezer Review

## Table of Contents <!-- omit in toc -->

- [Scope and Method](#scope-and-method)
- [Verdict](#verdict)
- [The Packages and Their Boundaries](#the-packages-and-their-boundaries)
- [Interfaces and Abstractions](#interfaces-and-abstractions)
- [Separation of Concerns](#separation-of-concerns)
- [SOLID](#solid)
- [The Safety Nets](#the-safety-nets)
- [The Threads of the Node](#the-threads-of-the-node)
- [The Serial Link](#the-serial-link)
  - [The Choice](#the-choice)
  - [What It Costs](#what-it-costs)
  - [How the Node Recovers](#how-the-node-recovers)
- [The Board Page](#the-board-page)
- [Coupling and Duplication](#coupling-and-duplication)
- [Testability and Tests](#testability-and-tests)
- [Implementation Issues](#implementation-issues)
- [The Review of 2026-11-02](#the-review-of-2026-11-02)
- [Recommendations](#recommendations)

Reviewed on 2026-11-15, at commit `f4aa88e`, the commit StepIt Macro pins in its `stepit.repos`. It judges the design that [docs/ARCHITECTURE.md](ARCHITECTURE.md) describes.

## Scope and Method

This review covers the whole module: the firmware of the Nano R4 in [`src/freezer_mcu`](../src/freezer_mcu), 11 files and 1859 lines; the host library [`freezer_driver`](../src/freezer_driver), 2085 lines with its fake, and its tests; the node [`freezer_node`](../src/freezer_node), 1269 lines with its launch file, and its test; the messages of [`freezer_msgs`](../src/freezer_msgs); and the board page in [`web`](../web), 988 lines of TypeScript, judged against the patterns of the `react-developer` skill. The neighbours are read where the Freezer meets them: `rig.yaml` and `rig.launch.py` of StepIt Macro, its `Shoot` behavior, the files of StepIt UI in `ui/src/freezer`, and the copy of the firmware's protocol layer in StepIt Motors. `serial`, third-party, and `framed-serial` have reviewers of their own: they are cited where the Freezer depends on them, not reviewed.

It replaces the review of 2026-11-02, written at `e37f0d1`. Since then, 3 commits changed how `serial` and `framed-serial` are fetched, from `freezer.repos` instead of git submodules, and no line of C++, of the firmware or of the page. This review was made from the code first, then compared with the earlier one: [The Review of 2026-11-02](#the-review-of-2026-11-02) says what became of each of its findings.

We read every file of the parts above. A finding marked **verified** failed a throwaway test, which was then deleted; the others are **from reading**. The tests ran inside the `stepit-macro` image, on ROS domain 83, on copies of `freezer_driver`, `freezer_node` and `web` in a scratch folder, built on top of the rig's installed workspace, so that nothing was added to the repository:

- **The node**: a gmock test with the fixture of `test_freezer_node`, and a `Driver` that wraps a `FakeDriver` and loses the reply to a chosen command after the fake has executed it.
- **The link**: the real `DefaultDriver` and `DefaultFramedSerial` on a fake `framed_serial::Serial` that plays the controller byte by byte, with the logic of `FakeDriver` and the layouts of the firmware.
- **The runner**: `ShotRunner` on a `FakeDriver` and a clock the test moves.
- **The firmware**: its `main.cpp`, `SerialPort.cpp`, `DataBuffer.cpp` and `CrcUtils.cpp`, compiled unchanged with `g++` against a stub of `Arduino.h` and `tusb.h`, 30 lines, and a stub of the sequencer. The timer, the interrupt and USB cannot run on a host: what depends on them is from reading, and no board was touched.
- **The page**: a vitest test in `jsdom`, which mounts the hook `useFreezer` on a fake `WebSocket`.
- **The threads**: `freezer_driver` and `freezer_node` built with `-fsanitize=thread`, and `test_freezer_node` run under `setarch -R`. Its 22 cases pass with 3 reports, all inside CycloneDDS, none with a racing frame in our code; the probes of the node add none.

The Arduino core of the Nano R4 and TinyUSB are not in the image. Two points of the earlier review rest on their source, and could be neither confirmed nor refuted: they are marked **not examined again**.

The whole suite, run once: `colcon test` of `freezer_driver` and `freezer_node` passes 78 gmock cases in 6 programs, with no failure and no skip, in 7.8 s; `tsc` and `vitest run` of the board page pass, 29 tests in 4 files, in 0.3 s.

## Verdict

The design is sound, and its one rule holds in the firmware: once a shot starts, the timer owns it, every table is checked whole on both sides, every shot ends with all outputs off, and an oversized or damaged frame can load no table. The packages keep to what they own. `ShotRunner`, on an injected clock, is the best seam of the module, and the firmware is small enough to read in an hour.

The weak part is **the node, not the controller**: what the node tells about the board, and how it behaves when one reply does not come.

- **`~/outputs` can tell a pattern the board does not have** (issue 1, verified). After a shot that failed on the host, the latched topic keeps the pattern of a step, e.g. the camera's two lines, while the board has every output off; after a `set_outputs` whose reply was lost, it tells off what is on. StepIt UI switches the lights by writing that pattern back with two bits changed, so a click on Lights can close the camera's focus and shutter lines, which then stay closed. It is the one finding that can leave an output on.
- **Stopping the node during a shot ends its process with `std::terminate`** (issue 2, verified).
- **Reconnecting restarts the controller, whether a shot runs or not** (issue 4, from reading): a shot can be cut short by the node's own recovery.
- **The link has no way back after a late or damaged reply**, a choice made on 2026-11-02 and judged in [The Serial Link](#the-serial-link): its costs are verified, and one of them, every shot firing while its goal aborts, lasts until the node restarts.
- **The board page never connects under `pnpm run dev`** (issue 5, verified): React's `StrictMode` closes the connection the page keeps.

The earlier review found no safety issue. We find two, issues 1 and 4, both through the node and both needing a failure first; the firmware has none.

## The Packages and Their Boundaries

| Rule | Verdict | Evidence |
|---|---|---|
| `freezer_msgs` holds no code | Kept | One action, four messages, one service. |
| `freezer_driver` has no ROS interface, and does not know `freezer_msgs` | Kept | It links `framed_serial` and `rclcpp`; its one ROS include is `rclcpp/logging.hpp`, in [`default_driver.cpp`](../src/freezer_driver/src/default_driver.cpp). |
| `freezer_node` reaches the controller only through `Driver` | Kept | Every call goes through `SynchronizedDriver`. 3 `dynamic_cast`s to `FakeDriver`, 2 in the constructor of [`freezer_node.cpp`](../src/freezer_node/src/freezer_node.cpp) and 1 in `handle_press_trigger()`, name the device `fake` and press IN1. |
| `freezer_node` includes `framed_serial` in one place | Kept | `create_driver()`, its composition root. |
| `web` knows the node only through rosbridge | Kept | [`freezer.ts`](../web/src/freezer/freezer.ts) uses 2 topics, 3 services, the action and `get_parameters`. It does not subscribe to `~/status`. |
| No cycle | Kept | `freezer_node` → `freezer_driver` → `framed_serial` → `serial`; `freezer_node` → `freezer_msgs`. In the firmware, `main.cpp` → `Sequencer`, `SerialPort` → `DataBuffer` → `Buffer`, `CrcUtils`. In the page, `components` → `freezer` → `ros`. |

`FakeDriver` ships in the production library, under `include/freezer_driver/fake`. That is a choice, not a leak: the node runs with it by default.

## Interfaces and Abstractions

| Interface | Where | Judgement |
|---|---|---|
| `Driver` | [`driver.hpp`](../src/freezer_driver/include/freezer_driver/driver.hpp) | One method per command, one request and one response each. 8 methods, 3 implementations that substitute for each other, and a fourth in an afternoon: the probes of this review wrapped a `FakeDriver` in 40 lines. `connect()` returns a `bool` and drops the `InfoResponse` it read, so `FreezerNode::connect()` asks `get_info()` again for the limits: 2 `Info` round trips per connection. |
| `ShotRunner` with `Clock` and `Sleep` | [`shot_runner.hpp`](../src/freezer_driver/include/freezer_driver/shot_runner.hpp) | The best seam of the module. Its `try` also catches what a callback throws, and reports it as a failed shot, see issue 2. |
| `ShotRunner::Callbacks` | same | `start`, `started` and `progress` keep the cancel and stop rules in the node and the polling in the runner. |
| `SynchronizedDriver` and `with_driver()` | [`synchronized_driver.hpp`](../src/freezer_driver/include/freezer_driver/synchronized_driver.hpp) | A decorator that serialises the calls. It holds its mutex for a whole `connect()`, up to 1.5 s; no caller waits on it, because each checks `connected_` first. |
| `framed_serial::FramedSerial` | the shared library | Mocked by [`mock_framed_serial.hpp`](../src/freezer_driver/tests/mock/mock_framed_serial.hpp): the tests of `DefaultDriver` see whole frames, each the answer to its request, never bytes. It has no call to drop what is waiting on the port. |
| The firmware's `SerialPort` | [`SerialPort.cpp`](../src/freezer_mcu/src/SerialPort.cpp) | Calls the global `Serial`. A stub of `Arduino.h` is enough to run it on a host, with `main.cpp` on top, see [Testability and Tests](#testability-and-tests). |
| `sequencer::` | [`Sequencer.h`](../src/freezer_mcu/include/Sequencer.h) | A free-function module, `load`, `start`, `stop`, `reset`, `setOutputs`, `status`: the timer and the registers stay behind it, and `main.cpp` never touches a register. It is what made `main.cpp` testable on a host with a stub of 12 lines. |
| `Freezer`, the store of the page | [`freezer.ts`](../web/src/freezer/freezer.ts) | A plain class with `subscribe` and `getState`, read by `useSyncExternalStore`; `applyShotMessage()` is a pure function with 8 tests. `Rosbridge` takes its `WebSocket` as a factory, and has 9 tests on a fake one. |

## Separation of Concerns

- **The trigger's shots are followed in the node.** `FreezerNode::watch()`, lines 744 to 860, tells a new shot id from a reset, estimates a start, follows a shot to its end and tells a stop: 117 lines of state machine in the ROS class, on the steady clock, testable only through a node. `ShotRunner` shows where it would sit.
- **The recipes are known twice in the node.** `declare_sequence_parameters()` and `build_sequence()` each hold an `if` chain over `flash` and `timed_light`, with the parameter names spelled in both; the defaults are written a third time in [`recipes.hpp`](../src/freezer_driver/include/freezer_driver/recipes.hpp) and a fourth in [`freezer.yaml`](../src/freezer_node/config/freezer.yaml).
- **The node builds the table twice.** `handle_goal()` builds and validates it; `execute()` builds it again from the parameters, line 504, and does not validate it. A parameter changed between the two goes to the controller unchecked by the host; the firmware checks it, so no table fires unchecked.
- **What the outputs are is known in three places, none of them the board.** The node publishes what it sent or what a step of the table says, never what the controller reports: `Status` has no field for the outputs. Issue 1 follows from it.
- **The firmware keeps its layers.** `main.cpp` holds the commands and their checks, `Sequencer.cpp` the timer and the registers, `SerialPort.cpp` the framing. Nothing of the protocol reaches the interrupt.
- **The page keeps its rules mostly out of its components**, see [The Board Page](#the-board-page).

## SOLID

| Principle | Verdict | Where |
|---|---|---|
| Single responsibility | Partly violated | [`freezer_node.cpp`](../src/freezer_node/src/freezer_node.cpp), 861 lines, holds the action, the services, the watch of the trigger, the reconnection thread and the status. The driver classes and `ShotRunner` each do one thing. |
| Open/closed | Partly violated | A new recipe changes the two `if` chains of the node. A new command changes `Driver` and its 3 implementations, which is expected of a protocol. |
| Liskov substitution | No violation found | The node and `ShotRunner` run the same on `DefaultDriver` and `FakeDriver`. The fake differs in details, issue 14, not in contract. |
| Interface segregation | No violation found | `Driver` has 8 methods, all used by the node. |
| Dependency inversion | Kept | `ShotRunner` depends on `Driver`, a clock and a sleep; the node takes a `Driver` in its second constructor. The page does not: `useFreezer()` names `Rosbridge` itself. |

## The Safety Nets

Each net of [Safety Nets](ARCHITECTURE.md#safety-nets) was traced through the code:

| Net | Verdict | Evidence |
|---|---|---|
| A shot ends with every output off | Kept, twice | `validate()` of [`sequence.cpp`](../src/freezer_driver/src/sequence.cpp) and `loadSequenceCommand()` of [`main.cpp`](../src/freezer_mcu/src/main.cpp) both refuse a last step that is not `0x0000`; `runDueBoundaries()` ends a shot only after the last pattern was latched; `stop()` and `reset()` latch zeros. So a controller that reads idle after a shot has every output off, which recommendation 1 builds on. |
| A table is checked whole before anything changes | Kept, verified | `loadSequenceCommand()` decodes into a local array and calls `sequencer::load()` after every check. On the host build of the firmware, a `LoadSequence` of 40 steps, 281 bytes, is answered malformed, and one of 28 steps, which fills the 200 bytes of the read buffer exactly, invalid table: 0 tables loaded. `Buffer::add()` cuts an oversized frame without a sign, but the exact length checks of every command but `Echo` then refuse it. |
| Outputs start off | Kept | `setup()` raises `OE` before any pin is driven, and `clearOutputs()` clears and latches before enabling. |
| The shot owns the outputs | Kept | `setOutputs()` checks `running` under `InterruptLock`; `LoadSequence` and `Shoot` answer busy. |
| Only `Stop` interrupts a shot | Kept on the wire, broken by the port | `handle_cancel()` refuses once `started_` is set. But opening the port stops a shot too, by design, and the node opens it by itself: issue 4. |
| The handshake refuses the wrong device and version | Kept | `DefaultDriver::connect()` checks the name and the major version. |
| A shot that never ends is abandoned | Kept | The deadline of `ShotRunner::run()`, line 123 of [`shot_runner.cpp`](../src/freezer_driver/src/shot_runner.cpp). |
| IN1 is debounced | Kept | `updateTrigger()`, 20 ms, fires on the press only. |
| No watchdog, by design | Judged sound for the controller | An output left on outside a shot is a choice of the host. It makes the host's picture of the outputs the only guard, and that picture can be wrong: issue 1. |

The arithmetic of the timer was checked by hand: the longest table is 4.8 × 10⁸ ticks, under the 2³¹ that the signed comparison of `runDueBoundaries()` allows, and `load()` cannot overflow `holdUs * 48`.

## The Threads of the Node

[Threads](ARCHITECTURE.md#threads) names three: the executor, the shot's thread and the reconnector. What they share, and how:

| Shared | Guard | Verdict |
|---|---|---|
| The serial port | The mutex of `SynchronizedDriver`, one call at a time | Sound. |
| `busy_` | Atomic, set by a compare-and-swap in `handle_goal()`, cleared last by `execute()` | Sound. |
| `started_`, `cancel_requested_`, `stop_requested_`, `stop_time_`, `known_shot_id_` | `start_mutex_` | Sound for a stop. The requests are not tied to a goal: a cancel that passes the mutex after its goal ended stays for the next goal, issue 6. The mutex is held across a call to the controller, issue 12. |
| `connected_`, and the status | An atomic, and `status_mutex_`: set one after the other, by two threads | The two can end up disagreeing, issue 13. |
| `limits_` | `limits_mutex_` | Sound. |
| `watched_`, `failed_polls_` | The executor only | Sound. |
| `failure_reported_` | The constructor, then the reconnector only | Sound. |
| `ShotRunner::loaded_` | None: `watch()` reads it when `busy_` is false, the shot's thread writes it | Sound only because the executor is single-threaded, so no goal starts while `watch()` runs. For the same reason, the second check of `busy_` in `watch()`, line 777, can never be true. A multi-threaded executor would make both wrong; nothing says so in the header. |

What is not sound is the end of the shot's thread: it calls the action server with no `try` around it, and the destructor joins it. Issue 2.

## The Serial Link

### The Choice

**One request, one response, and no recovery.** On 2026-11-02, the pull request [Remove the Recovery of Lost Replies](pr/remove-lost-reply-recovery.md) took out what [#18](pr/018-tolerate-a-lost-reply.md) had added, asking again and dropping late replies, because firmware 2.1.1 fixed the cause of the lost replies: "if replies go missing again, a new test will find why". So `ShotRunner::run()` ends the goal on the first query that fails, and `DefaultDriver` reads the next frame as the answer to its request, whatever it is.

**Judged: the first half is sound, the second is not finished.** Not asking again is right: a `Shoot` asked twice can fire twice. Not dropping what is waiting is different: it is not a recovery, it is what keeps the sentence of [The Protocol](ARCHITECTURE.md#the-protocol) true, "a frame the host did not ask for can never be mistaken for the answer to its next request". Without it, one late reply puts the host one reply behind, and the host has no way back. USB makes the cause rare, since the bus checks and retries every packet; the price, when it happens, is every shot.

### What It Costs

All verified on the host, with the real `DefaultDriver` and `DefaultFramedSerial` on a fake port:

- **A late reply leaves the host one reply behind, for good.** One status reply that arrives after its query timed out: the next 5 status queries all succeed, each reading the reply before. Then 3 goals in a row each fire on the controller, which counts shot ids 1, 2 and 3, and each aborts, with `Shoot response is too short: 11 00 00 27 10.`, the reply to `LoadSequence`, then `Status response is too short: 11 00 02 00 00 27 10.`, the reply to `Shoot`. After them, 50 status queries are all answered, one behind, with 17 bytes waiting: `watch()` never sees 3 failures in a row, so nothing closes the port. On the rig, every `TakeShot` fails while its picture is taken, until the node restarts.
- **A reply damaged at its start is never read past.** [`DefaultFramedSerial::read()`](../modules/framed-serial/src/default_framed_serial.cpp) throws `start delimiter missing` after consuming one byte, and `incorrect frame length` on two delimiters in a row. After one status reply that lost its first byte, 0 of the next 20 queries were answered, and 355 bytes were waiting. The firmware's reader, by contrast, skips to the next delimiter.
- **A reply waiting when the port opens fails the handshake, or passes it.** A stale status reply, 13 bytes, reads as an `Info` with an empty name: `connect()` returns `false` at once, `Unexpected device on serial port: expected "FREEZER", got "".`. A stale `Info` reply passes the handshake, and the first status then fails with `Unknown controller state 02.`, one behind. Whether the Nano can keep a reply across a reopening of its port depends on TinyUSB's buffers, which the firmware's `updateHost()` does not clear: not examined again.
- **`timeout` is per byte, not per answer.** `DefaultSerial::set_timeout()` sets a constant timeout, and the reader asks for one byte at a time: a reply that never comes costs 0.2 s, one whose bytes trickle in can take longer. The comment `# s, for one answer` of [`freezer.yaml`](../src/freezer_node/config/freezer.yaml) says otherwise. From reading.

### How the Node Recovers

- **From a damaged frame, by letting the controller go.** Every query fails, so a running goal aborts at the first, and `watch()` closes the port after 3, 0.6 s. The reconnector opens it within 2 s, the controller starts again, and the link is clean. It works; its price is issue 4.
- **From a late reply, not at all.** See above. The earlier review said the same.
- **From a lost reply, at once.** A reply that never comes leaves the pairing intact: the goal aborts, the next query is answered. What the node then tells about the shot is wrong, issues 1 and 3.

## The Board Page

Judged against the patterns of the `react-developer` skill:

| Pattern | Verdict | Where |
|---|---|---|
| One store, derived state | Kept | `Freezer` holds the outputs and the shots; `running`, the LEDs and the trace are computed during render. The page keeps nothing of the board, as [The Packages](ARCHITECTURE.md#the-packages) asks. |
| Services injected, one composition root | Not kept | `useFreezer()` builds `new Freezer(new Rosbridge(url))` inside `useMemo`. The constructor of `Rosbridge` opens a `WebSocket`: a side effect during render, which is what breaks the page under `StrictMode`, issue 5. |
| A component knows only its props | Kept | `TimingDiagram` and `Shots` take values; `Controls` takes the store. |
| Hooks are the view models | Partly | `Controls.tsx` holds its rules itself: it fetches the sequences and `use_fake`, and decides that a result whose message starts with `Stopped` is not an error, line 63, a rule kept in step by hand with two strings of `freezer_node.cpp`. |
| Few effects, at the edges | Kept, but one | `TimingDiagram` reports the hovered step to its parent through an effect, line 101, where the mouse handler could call `onHover` itself. |
| Stale work dropped | Kept | The `current` flag of the effect of `Controls`. |
| Errors as values | Mixed | `sendGoal()` returns an outcome; `callService()` throws. |
| Storage keys in one file | One key, inline | `freezer.rosbridge`, in `App.tsx`. |
| Tests | The pure parts only | 29 tests: the bits, the timeline, the shots, rosbridge. No component and no hook is rendered: the project has no `jsdom`, which is why issue 5 passed. |

Two smaller points, from reading. The menu shows `example 1`, `example 2` for the sequences, with the name only as a tooltip: on the rig, `test_shot` reads `example 1`. And the sequences are read once per connection to rosbridge: a node restarted with other sequences, behind a rosbridge that stays up, keeps the old menu.

## Coupling and Duplication

Values kept in step by hand, all compared today:

| Value | Places | Agree |
|---|---|---|
| The limits of a table, 16 steps, 40 µs, 10 s | `Sequencer.h`, `FakeDriver::kLimits`; the real driver reads them at the handshake | Yes |
| The command ids, 6 of them, and the layouts | `main.cpp` of the firmware, `default_driver.cpp` | Yes; `Echo`, `0x79`, is in the firmware only |
| The bits of a jack | `recipes.cpp`, `Step.msg`, `web/src/freezer/board.ts`, and `ui/src/freezer/outputs.ts` of the rig | Yes, 4 places |
| The jack of the lights | `LIGHTS_JACK = 1` in the rig's `outputs.ts`, `lights: [1]` in `rig.yaml` | Yes; nothing checks it |
| The messages | `freezer_msgs`, and `freezer.ts` of the page | Yes |
| The version | Firmware 2.1.1, `FakeDriver` 2.0.0 | No, harmless: the major is what counts |
| The serial port | `/dev/ttyACM0` in the node, the launch file and `freezer.yaml`; `/dev/ttyUSB0` in `rig.yaml` | No: issue 11 |
| `Stopped`, the prefix of a message | `freezer_node.cpp`, `Controls.tsx` | Yes |

- **The firmware's protocol layer is a copy of StepIt Motors'.** 8 files: `Buffer.h`, `CrcUtils.h`, `CrcUtils.cpp`, `DataBuffer.h`, `DataBuffer.cpp`, `Guard.h`, `SerialPort.h`, `SerialPort.cpp`, against `stepit-motors/src/stepit_mcu`. Apart from the licence header, BSD-3-Clause here and MIT there, `diff` shows two differences: `DataBuffer::addInt()` and `removeInt()` take an `int16_t` here, and `SerialPort::reset()` exists only here. Both are needed by the R4, neither is a divergence by accident. A fix to the framing, e.g. issue 10, is made twice. Weight: medium.
- **The host's `framed-serial` and `serial` are shared, not copied**, at `d386c91` and `d8d1606` in `freezer.repos`, in StepIt Motors' `.repos` file and in `stepit.repos`: the three agree.
- **Dead code in the firmware copy**: `Guard.h` is included nowhere; `SerialPort::flush()` and `isBusyWriting()`, `crc_utils::crc_ccitt()`, `DataBuffer::removeLong()`, `addFloat()`, `removeFloat()` and `getCapacity()` are called nowhere. Weight: low; it is the price of the copy.
- **`rig.yaml` sets `baudrate: 9600` for the Freezer**, which the Nano R4 ignores. Weight: none, but it reads as if it mattered.

## Testability and Tests

| What | Tested | How |
|---|---|---|
| The frames of each command and their decoding | Yes, 16 cases | `test_default_driver`, on a mock of `FramedSerial`: whole frames, each the answer to its request. |
| The fake controller | Yes, 17 cases | `test_fake_driver`. |
| The sequence and its rules, the recipes | Yes, 8 and 5 cases | `test_sequence`, `test_recipes`. |
| A shot from load to end, its failures | Yes, 10 cases | `test_shot_runner`, on the fake's clock. |
| The node end to end | Yes, 22 cases | `test_freezer_node`, on the steady clock, with a fake. |
| The pure parts of the page | Yes, 29 tests | vitest. |
| A reply lost, late or damaged | No | `FakeDriver::Failure` has `NeverFinish` and `ResetDuringShot` only, and the mock of `FramedSerial` cannot misalign. Issues 1 and 3 and [What It Costs](#what-it-costs) have no test that could see them. |
| A cancel through the action | No | As [Tests](ARCHITECTURE.md#tests) says. Issue 6 lives there. |
| The node stopped during a shot | No | Issue 2. |
| A hook or a component of the page | No | Issue 5. |
| The firmware | No | By design for the timing. Not needed for the rest: see below. |

The seams that are missing, each proved cheap by a probe of this review:

- **A driver that loses replies.** A `Driver` that wraps a `FakeDriver`, executes the command and throws: 40 lines, or 3 more values of `FakeDriver::Failure`.
- **A fake port under the real framing.** A `framed_serial::Serial` that plays the controller byte by byte: about 150 lines, the "new test" that the note of the removed recovery counts on.
- **The firmware's commands on a host.** `main.cpp`, `SerialPort.cpp`, `DataBuffer.cpp` and `CrcUtils.cpp` compile unchanged with `g++` against a stub of `Arduino.h` and `tusb.h`, 30 lines, and a stub of `sequencer::`, 12 lines. Every command, every error and every rule of a table can then be tested in CI, with the same frames as `test_default_driver`. Only `Sequencer.cpp` needs the board.
- **`jsdom` for the page**, one development dependency, to render `useFreezer` and `Controls`.

## Implementation Issues

Ordered by severity. **Safety** says whether the issue can leave an output on, cut a shot short or fire a table that was not checked.

1. **`~/outputs` can tell a pattern the board does not have, and StepIt UI writes it back.** Verified in the node; the page's side from reading. Two ways:
   - *After a shot that failed on the host.* `execute()` publishes all off after a shot that ended or was stopped, and nothing after one that failed, line 591; `watch()` then finds the shot id it knows and returns, line 827. The probe lost one status reply during the rig's `test_shot`, made ten times shorter: the goal aborted with `timeout`, and 0.5 s later, after 25 polls of `watch()`, the board had `0x0000` and the latched topic `0xC000`, the focus and shutter lines of OUT8, the rig's camera. It stays so until the next shot, pattern or stop. [What the Node Tells](ARCHITECTURE.md#what-the-node-tells) says the node publishes nothing then, because it does not know the outputs; but it will know them at its next status query, since a controller that reads idle after a shot has every output off.
   - *After a `set_outputs` whose reply was lost.* `handle_set_outputs()` publishes only on a success, line 646. The probe lost the reply to `set_outputs(0xC000)`: the board had `0xC000`, the topic `0x0000`.

   Alone, the first is a wrong LED. With the rig, it is more: `setLights()` of `ui/src/freezer/lights.ts`, in StepIt Macro, sends `withLights(outputs, on)`, the pattern the topic told with the two bits of OUT1 changed. With `0xC000` on the topic, a click on Lights sends `0xC003`: the controller, idle, latches it, and the camera's focus and shutter lines close and stay closed, with no watchdog, until a stop, a shot or the next pattern; the node then publishes `0xC003`, and every click after it keeps the two bits. A failed shot is exactly when we switch the lights on to look. **Safety: yes, for the rig**: an output left on, on the camera's jack. It needs one lost reply first.
2. **Stopping the node during a shot ends its process with `std::terminate`.** Verified. `main.cpp` drops the node when `rclcpp::spin()` returns, after Ctrl-C has shut the context down. The shot's thread is still polling: its next `publish_feedback()` throws `Failed to publish feedback: feedback publisher is invalid`, which `ShotRunner::run()` catches as a failed shot; `execute()` then calls `goal_handle->abort()`, which throws `Asked to publish result for goal that does not exist` with nothing to catch it. The probe shut the context down 0.2 s into a 1 s shot: the process aborted with a core dump, exit code 134. Without the exception, the destructor joins the thread, and waits for the shot: 2.0 s for a 2 s shot in a second probe, up to 10 s, twice the 5 s a launch file gives before it kills. The shot itself runs to its end on the controller, and the port is closed by the system. **Safety: no.**
3. **A shot whose `Shoot` reply was lost is told as a shot of IN1.** Verified. `ShotRunner::run()` lets the exception of `driver_.shoot()` abort the goal; `execute()` records `known_shot_id_` only when the result has a shot id, line 622, so the next `watch()` takes the new id for the trigger's. The probe: the goal ended `ABORTED` with `timeout`, and `~/shots` told shot 1 as `STARTED` and `ENDED` with `SOURCE_TRIGGER`. On the rig, `TakeShot` fails while the camera took its picture, and running it again takes a second one. **Safety: no.**
4. **Reconnecting restarts the controller, whether a shot runs or not.** From reading. Opening the port makes the firmware start again, `updateHost()`, which stops a shot and switches every output off: the design, and what makes a restart of the node safe. But the node also opens the port by itself. A goal aborts at its first failed query while its shot runs on; `watch()` closes the port after 3 more failures, 0.6 s; the reconnector opens it up to 2 s later. A shot longer than that, and the rig's lasts 1.2 s, is stopped where it is, e.g. with the shutter open and the lights not yet fired, by a node that only meant to find its controller again. The node knows the duration of the shot it started, and does not wait for it. **Safety: a shot cut short**, with every output off; it needs a link that fails and comes back within the shot, which a cable that is unplugged does not do, since the Nano then loses its power.
5. **The board page never connects under `pnpm run dev`.** Verified in `jsdom`. `main.tsx` mounts the page in `StrictMode`, which in development calls `useMemo` twice and runs every effect, its cleanup, and the effect again. `useFreezer()` makes its `Rosbridge` in `useMemo` and closes it in the cleanup of an effect, line 8 of [`useFreezer.ts`](../web/src/freezer/useFreezer.ts), and nothing opens it again. The probe: 2 sockets made, the one the page keeps closed, the other left open with no owner, and the page says `Connecting…` for good. Without `StrictMode`, 1 socket and `connected`: the built page, which the launch file serves, works. [How to Extend](ARCHITECTURE.md#how-to-extend-stepit-freezer) sends us to `pnpm run dev` to change the page. **Safety: no.**
6. **A cancel that arrives as its goal ends can make the next goal wait forever.** From reading. `handle_cancel()` sets `cancel_requested_` whenever `started_` is false, line 469, and `execute()` clears both as its last step. A cancel that the action server found cancellable, and that reaches the mutex just after that last step, is accepted for a goal that is over; the server then fails to move the goal, and the flag stays. The next goal reads it in its `start` callback, ends as `Canceled`, and waits in `while (!goal_handle->is_canceling())`, line 609, for a cancel nobody sent: its thread never ends, `busy_` stays true, every goal is rejected, and the destructor never returns. The window is a few instructions wide, and StepIt Macro's `Shoot` behavior sends a cancel each time it is halted; we could not provoke it, and did not try to with a stress test. **Safety: no**: nothing is fired.
7. **With `reconnect_period` 0, three failed queries lose the controller until the node restarts.** Verified: unplugged and plugged in again, the fake was still not connected 1 s later. [Losing the Controller](ARCHITECTURE.md#losing-the-controller) says so, and the README does not. Judged a trap rather than a choice: the parameter turns the reconnection off, not the letting go, and a setting meant to be the careful one makes 0.6 s of silence final. **Safety: no.**
8. **A stop that comes just after a shot ended tells it as stopped.** Verified. `execute()` turns any success into `Stopped.` when a stop was requested, line 558, and a stop is recorded whenever a goal is busy. The probe sent the stop 90 ms after a 10 ms shot had ended, before the runner's next poll, slowed to 0.4 s: the goal aborted with `Stopped.`, and `~/shots` told `STOPPED` with `elapsed_us` 10000 of 10000. With the default `poll_period`, the window is about 15 ms. `watch()` does the same for a shot of IN1, within `watch_period`. The node has what it needs to tell: the stop came after the start plus the duration. **Safety: no.**
9. **A shot of IN1 right after the node's shot fails the node's goal.** Verified. While the controller reads running, `ShotRunner::run()` does not look at the shot id, line 108: the trigger's shot is reported as the progress of the node's, until its deadline. The probe pressed IN1 as a 10 ms shot ended: `Shot 1 did not end within its 10000 us and the margin.`, after 16 progress reports. IN1 is for testing. **Safety: no.**
10. **The firmware accepts a frame longer than its buffer, and answers a long `Echo` with a frame that has no end.** Verified on the host build of the firmware. `Buffer::add()` drops a byte when full, while the CRC runs over every byte received, so an oversized frame passes the check, cut to 198 bytes. Every command but `Echo` then refuses it on its length, see [The Safety Nets](#the-safety-nets). An `Echo` of 196 bytes or more, the largest that fits being 197, overflows the write buffer: the reply has 200 bytes and one delimiter, where one of 195 has two. The host then reads the start of the next reply as its end. `Driver` has no method for `Echo`. **Safety: no.**
11. **`rig.yaml` names a port the Nano R4 does not have.** From reading. `launch.freezer.usb_port` is `/dev/ttyUSB0`, the port of the Nano 3's FT232; the R4 is `/dev/ttyACM0`. Harmless while `use_fake` is `true`, as it is today; with `false`, the node never finds the board. **Safety: no.**
12. **The `started` callback calls the controller under `start_mutex_`.** From reading. Line 519 sends `Stop` and waits for its reply while holding the mutex that `handle_cancel()` and `handle_stop()` take on the executor, which can then wait up to `timeout`. **Safety: no.**
13. **The status can say "not connected" while the node is connected.** From reading. `disconnect()` clears `connected_`, then sets the status, then closes the port, lines 330 to 334. With the fake, the reconnector can connect and set its status between the first two, and the status of `watch()` then lands last. With the real driver, the port is still open at that moment: `serial::Serial::open()` throws `Serial port already open`, `connect()` closes it, and the next try, a period later, succeeds; the price is one odd line in the log. **Safety: no.**
14. **The fake differs from the firmware in small ways.** From reading. `FakeDriver::reset()` keeps the latches a shot had planned, so `outputs()` after a reset still follows the table, where the Nano switches everything off; `worst_lateness_us_` is not cleared when a shot starts; `trigger()` works while unplugged. **Safety: no**, but a test of a reset on the fake does not see the outputs go off.
15. **Comments and documents that say another thing than the code.** From reading. [`Sequencer.h`](../src/freezer_mcu/include/Sequencer.h) speaks of `Timer1` twice, lines 45 and 73, and `runDueBoundaries()` of a wrap "32.8 ms later", line 204 of `Sequencer.cpp`, both the Nano 3's: GPT1 wraps after about 89 s. [`ControllerStatus.msg`](../src/freezer_msgs/msg/ControllerStatus.msg) gives `/dev/ttyUSB0` as its example. The module's `CLAUDE.md` asks for the MIT header; 38 of the 39 C++ files and the 3 `package.xml` carry BSD-3-Clause, as `LICENSE` does, and `Sequencer.h` carries none. `freezer.yaml` calls `timeout` "for one answer". In the architecture document: `~/outputs` is also published all off by the constructor when no controller answered, line 126; and `LoadSequence` with no payload is answered malformed, not busy, during a shot. **Safety: no.**

No security issue was found. The node opens only the configured port, and the page reaches the node only through rosbridge, which is open to the network on port 9092, as the launch file intends.

## The Review of 2026-11-02

What became of each of its findings. None is fixed: the code has not changed.

| Finding of 2026-11-02 | Today |
|---|---|
| 1. A late or stale reply leaves the host one reply behind | Holds, verified again. Now judged as the price of a choice, in [The Serial Link](#the-serial-link), since the pull request that removed the recovery is part of the design. |
| 2. A reply that loses its first byte puts the reader out of step | Holds in the reader, verified again, 0 of 20. Overstated in how likely it is: it reasoned from "a lost packet", and USB does not lose a packet without the host knowing. Its recovery has a price it did not see, issue 4. |
| 3. A lost reply to `Shoot` is told as a shot of IN1 | Holds, verified again: issue 3. |
| 4. `rig.yaml` names a port of the Nano 3 | Holds: issue 11. |
| 5. A stale status reply refuses the Freezer at the handshake | Holds on the host, verified again; the firmware's side not examined again. |
| 6. A shot of IN1 right after the node's shot fails the goal | Holds, verified again: issue 9. |
| 7. `loop()` can stall in `Serial.write()` | Not examined again: it rests on the source of the Arduino core. |
| 8. A lost reply to `SetOutputs` leaves `~/outputs` wrong | Holds, and understated: verified now, and with the rig's page it is a safety issue. Issue 1. |
| 9. The reconnection thread can open the port that `watch()` is closing | Wrong for the real driver: the port is still open, and `open()` throws. What remains is issue 13. |
| 10. The `started` callback calls `stop()` under `start_mutex_` | Holds: issue 12. |
| 11. A long `Echo` reply is cut | Holds, verified now, with its threshold: issue 10. |
| 12. Comments that say another thing | Holds, all of them: issue 15. |
| 13. The fake differs from the firmware | Holds: issue 14. |
| TinyUSB keeps what it has not sent across a reopening | Not examined again. |
| "No finding is a safety issue" | No longer our verdict: issues 1 and 4. |

New in this review: issues 1 as a whole, 2, 4, 5, 6, 7, 8, and [The Threads of the Node](#the-threads-of-the-node) and [The Board Page](#the-board-page).

## Recommendations

Ordered by value against effort; each fits one pull request.

1. **Make `~/outputs` true again after a failure, and stop the page writing it back** (issue 1). In the node: after a shot that failed, let `watch()` publish all off at its first status that reads idle, which the safety nets guarantee is true; and send `SetOutputs` again, once, when its reply did not come, since the command can be repeated without harm. In `freezer_msgs`: a mask in `SetOutputs`, so that a client changes its own lines and no other, and StepIt UI stops sending back what it was told. Trade-off: a new field in the service, with a default that keeps today's meaning; the rig's page changes with it.
2. **End the shot's thread cleanly** (issue 2). In `execute()`, a `try` around the calls to the goal handle, and a check of `rclcpp::ok()` in the `progress` callback, so that a node being stopped leaves the shot to the controller and returns. Trade-off: none; add the test of a shutdown during a shot.
3. **Drop what is waiting before each request, and add the test the removed recovery counts on** (The Serial Link). A `flush_input()` on `framed_serial::Serial`, over `serial::Serial::flushInput()`, called by `DefaultDriver` before it writes; and the fake port of the probes, with a late, a stale and a damaged reply, as a test of `freezer_driver`. It is not the retry that was removed: nothing is asked twice. Trade-off: it changes the shared library, so StepIt Motors moves to the same commit; a reply later than the next request is still read once out of turn, which only a command id in each reply, a new major version, would rule out.
4. **Record a shot whose reply was lost** (issue 3). When `shoot()` throws, ask the status once: a last shot id past the one known before means the shot fired, and the runner returns its id. Trade-off: one more query when `Shoot` fails.
5. **Do not open the port while a shot may run** (issue 4). The reconnector waits until the deadline of the last shot the node started, or saw start. Trade-off: up to 10 s more without a controller, after a failure during a long shot.
6. **Open the page's connection in an effect** (issue 5). `useFreezer()` makes the store without connecting, opens it in the effect and closes it in its cleanup; `jsdom` and one test that mounts it in `StrictMode`. Trade-off: one development dependency.
7. **Tie a cancel and a stop to their goal** (issues 6, 8, 12). Keep the goal's id with the requests, refuse a cancel for another goal, replace the wait on `is_canceling()` by one that ends, compare the time of a stop with the end of the shot, and send the late `Stop` after releasing the mutex. Trade-off: none; add the test of a cancel through the action, which is missing.
8. **Let `reconnect_period` 0 mean what it says** (issue 7). Either `watch()` keeps the port when nothing will reopen it, or the README says that 0 makes a silence final. Trade-off: none.
9. **Test the firmware's commands on a host** (issue 10, testability). The stubs of the probe, as a test of `freezer_driver` or a PlatformIO `native` environment; then drop a frame that overflows its buffer, in both copies of `SerialPort`. Trade-off: the copy in StepIt Motors changes too.
10. **Follow the trigger outside the node** (separation of concerns, issue 9). Move the logic of `watch()` into `freezer_driver`, on an injected clock, and let `ShotRunner` end its shot when another id runs. Trade-off: moves about 117 lines and their tests.
11. **Smaller fixes in one pass** (issues 11, 13, 14, 15). `usb_port: /dev/ttyACM0` in `rig.yaml`, or the `by-id` path of the R4, and no `baudrate`; set the status and `connected_` together in `disconnect()`; switch the outputs off in `FakeDriver::reset()`; correct the comments, the example port, the licence named in `CLAUDE.md` and the two sentences of the architecture document.
