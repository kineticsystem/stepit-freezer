# StepIt Freezer Review

## Table of Contents <!-- omit in toc -->

- [Scope and Method](#scope-and-method)
- [Verdict](#verdict)
- [The Packages and Their Boundaries](#the-packages-and-their-boundaries)
- [Interfaces and Abstractions](#interfaces-and-abstractions)
- [Separation of Concerns](#separation-of-concerns)
- [SOLID](#solid)
- [The Safety Nets](#the-safety-nets)
- [The Serial Link](#the-serial-link)
  - [The Firmware's Side](#the-firmwares-side)
  - [The Host's Side](#the-hosts-side)
  - [A Failed Query Ends the Goal](#a-failed-query-ends-the-goal)
- [Coupling and Duplication](#coupling-and-duplication)
- [Testability and Tests](#testability-and-tests)
- [Implementation Issues](#implementation-issues)
- [Recommendations](#recommendations)

Reviewed on 2026-11-02, at commit `e37f0d1`. It judges the design that [docs/ARCHITECTURE.md](ARCHITECTURE.md) describes.

## Scope and Method

This review covers the whole module: the firmware of the Nano R4 in [`src/freezer_mcu`](../src/freezer_mcu), 11 files and 1859 lines; the host library [`freezer_driver`](../src/freezer_driver), about 2100 lines with its fake, and its tests; the node [`freezer_node`](../src/freezer_node), 1123 lines, its launch file and its configuration; the messages of [`freezer_msgs`](../src/freezer_msgs); and the board page in [`web`](../web), read for its interface to the node. The host side of `framed-serial`, shared with StepIt Motors, is judged where the Freezer depends on it. `serial`, third-party, is not reviewed.

We read every file of the parts above; the Arduino Renesas core 1.6.0 that the firmware runs on, where the firmware depends on it (`SerialUSB.cpp`, `USB.cpp`, and TinyUSB's `cdc_device.c` and `usbd.c`); and the neighbours named by the rig's `CLAUDE.md`: `rig.yaml` of StepIt Macro, and the copies of the protocol in StepIt Motors. The safety nets and what is deliberately not there are judged as design, not reported as bugs.

Each suspected bug of the host was reproduced with a throwaway gmock probe, built with `freezer_driver` and `freezer_node` in a scratch workspace inside the `stepit-macro` container, on ROS domain 77, then deleted. The probes of the link put `DefaultDriver` and the real `DefaultFramedSerial` on a fake `framed_serial::Serial` that plays the controller byte by byte, with the logic of `FakeDriver` and the encoding of the firmware. The probe of the node went in `src/freezer_node/tests`, the only place that can start a node. A finding marked **verified** failed its probe; the others are **from reading**. The firmware has no automated test, and no board was touched: every finding of the firmware is from reading. Every probe failed as suspected, so no suspicion was dropped by a probe; one was dropped by reading, see [The Firmware's Side](#the-firmwares-side).

The whole suite: `colcon test` of `freezer_driver` and `freezer_node` passes 78 gmock tests in 6 programs, with no failure and no skip, in about 7.5 s; `vitest run` of the board page passes 29 tests in 4 files in 0.2 s.

## Verdict

The design is sound and its one rule holds: once a shot starts, the hardware timer owns it, every table is checked whole by the host and again by the firmware, and every shot ends with all outputs off. The packages keep to what they own: `freezer_driver` has no ROS interface, `ShotRunner` runs a shot on an injected clock, and the firmware is small enough to read in an hour. No finding of this review is a safety issue for a camera or a light in the sense of the project: no path was found that leaves an output on after a shot, cuts a shot short other than by `Stop`, or fires a table the firmware did not check.

The weakness is **the pairing of requests and replies on the host**. The architecture says that a frame the host did not ask for can never be mistaken for the answer to its next request; the code does not hold that. Nothing on the host drops unread bytes or checks that a reply belongs to its request, so a reply that comes after `timeout`, or one left in the Nano's USB buffer when the port opened, is read as the answer to the next request, and every reply after it is one behind until the port closes: every shot then fires on the controller and its goal aborts (finding 1, verified). A reply that loses its first byte leaves the reader out of step for good (finding 2, verified). Neither touches the outputs; both cost the rig its shots, and the first lasts until the node restarts.

## The Packages and Their Boundaries

| Rule | Verdict | Evidence |
|---|---|---|
| `freezer_msgs` holds no code | Kept | One action, four messages, one service. |
| `freezer_driver` has no ROS interface | Kept | Its only ROS include is `rclcpp/logging.hpp`, in [`default_driver.cpp`](../src/freezer_driver/src/default_driver.cpp), for its logger, as [The Packages](ARCHITECTURE.md#the-packages) says. It links `framed_serial` and `rclcpp`. |
| `freezer_node` reaches the controller only through `Driver` | Kept | Every call goes through `SynchronizedDriver`. Two `dynamic_cast`s to `FakeDriver` in the constructor of [`freezer_node.cpp`](../src/freezer_node/src/freezer_node.cpp) name the device `fake` and offer `~/fake/press_trigger`, and a third in `handle_press_trigger()` presses IN1. |
| `web` knows the node only through rosbridge | Kept | [`freezer.ts`](../web/src/freezer/freezer.ts) uses the topics, the services, the action and `get_parameters`; nothing else. |
| No cycle | Kept | `freezer_node` → `freezer_driver` → `framed_serial` → `serial`; `freezer_node` → `freezer_msgs`. In the firmware, `main.cpp` → `Sequencer`, `SerialPort` → `DataBuffer` → `Buffer`, `CrcUtils`: one direction. |

`FakeDriver` ships in the production library, under `include/freezer_driver/fake`. That is a choice, not a leak: the node runs with it by default, and the launch file selects it with `use_fake`.

## Interfaces and Abstractions

| Interface | Where | Judgement |
|---|---|---|
| `Driver` | [`driver.hpp`](../src/freezer_driver/include/freezer_driver/driver.hpp) | One method per command, one request and one response each: the protocol, seen from the host. 8 methods, three implementations that substitute for each other. `connect()` returns a `bool` and drops the `InfoResponse` it read, so [`FreezerNode::connect()`](../src/freezer_node/src/freezer_node.cpp) asks `get_info()` a second time for the limits (line 272): two `Info` round trips per connection. |
| `ShotRunner` with `Clock` and `Sleep` | [`shot_runner.hpp`](../src/freezer_driver/include/freezer_driver/shot_runner.hpp) | The best seam of the module: a shot runs in a test on the fake's clock, instantly and exactly, and 10 tests use it. |
| `ShotRunner::Callbacks` | same | `start`, `started` and `progress` keep the cancel and stop rules in the node and the polling in the runner. Clean. |
| `SynchronizedDriver` and `with_driver()` | [`synchronized_driver.hpp`](../src/freezer_driver/include/freezer_driver/synchronized_driver.hpp) | A decorator that serialises the calls, so a stop goes between two status queries. `driver()` hands out the wrapped driver for a `dynamic_cast`, a small test seam in the production interface. |
| `framed_serial::FramedSerial` | the submodule | Mocked by [`mock_framed_serial.hpp`](../src/freezer_driver/tests/mock/mock_framed_serial.hpp): the tests of `DefaultDriver` see whole frames, each the answer to its request, never bytes, which is why findings 1 and 2 have no test. It has no call to drop unread bytes. |
| The firmware's `SerialPort` | [`SerialPort.cpp`](../src/freezer_mcu/src/SerialPort.cpp) | Calls the global `Serial` directly: the framing of the firmware cannot be compiled and tested on the host. |
| `sequencer::` | [`Sequencer.h`](../src/freezer_mcu/include/Sequencer.h) | A free-function module with a small interface, `load`, `start`, `stop`, `reset`, `setOutputs`, `status`: the timer and the shift registers stay behind it, and `main.cpp` never touches a register. |

## Separation of Concerns

- **The trigger's shots are followed in the node.** [`FreezerNode::watch()`](../src/freezer_node/src/freezer_node.cpp), lines 744 to 860, tells a new shot id from a reset, estimates the start of a shot from its elapsed time, follows it to its end and tells a stop: a state machine of about 117 lines in the ROS class, testable only through a node and a fake on the steady clock. `ShotRunner` shows how the same logic would sit in `freezer_driver`, on an injected clock.
- **The recipes are known twice in the node.** `declare_sequence_parameters()` and `build_sequence()` each hold an `if` chain over `flash` and `timed_light`, with the parameter names spelled in both. A third recipe changes both.
- **The node builds the table twice.** `handle_goal()` builds and validates the table; `execute()` builds it again from the parameters (line 504) and does not validate it. A parameter changed between the two goes to the controller unchecked by the host; the firmware checks it, so it is not a table fired unchecked. Building once and handing the table to the thread would close the gap and drop one of the two builds.
- **The firmware keeps its layers.** `main.cpp` holds the commands and their checks, `Sequencer.cpp` the timer and the registers, `SerialPort.cpp` the framing. Nothing of the protocol reaches the interrupt.

## SOLID

| Principle | Verdict | Where |
|---|---|---|
| Single responsibility | Partly violated | [`freezer_node.cpp`](../src/freezer_node/src/freezer_node.cpp), 861 lines, holds the action, the services, the watch of the trigger, the reconnection thread and the status. The driver classes and `ShotRunner` each do one thing. |
| Open/closed | Partly violated | A new recipe changes the two `if` chains of the node, see above. A new command changes `Driver` and its 3 implementations, which is expected of a protocol. |
| Liskov substitution | No violation found | The node and `ShotRunner` run the same against `DefaultDriver` and `FakeDriver`. The fake differs in details, see finding 13, not in contract. |
| Interface segregation | No violation found | `Driver` has 8 methods, all used by the node. |
| Dependency inversion | Kept | `ShotRunner` depends on `Driver`, a clock and a sleep; the node takes a `Driver` in its second constructor and builds the real one in `create_driver()`, its composition root. |

## The Safety Nets

Each net of [Safety Nets](ARCHITECTURE.md#safety-nets) was traced through the code:

| Net | Verdict | Evidence |
|---|---|---|
| A shot ends with every output off | Kept, twice | `validate()` of [`sequence.cpp`](../src/freezer_driver/src/sequence.cpp) and `loadSequenceCommand()` of [`main.cpp`](../src/freezer_mcu/src/main.cpp) both refuse a last step that is not `0x0000`; `runDueBoundaries()` ends a shot only after the last step's pattern was latched. |
| A table is checked whole before anything changes | Kept | `loadSequenceCommand()` decodes into a local `steps` array and calls `sequencer::load()` only after every check; each hold is compared before it is summed, so the sum cannot overflow. |
| Outputs start off | Kept | `setup()` raises `OE` before any pin is driven, and `clearOutputs()` clears and latches before enabling. |
| The shot owns the outputs | Kept | `setOutputs()` and `start()` check `running` under `InterruptLock`; `LoadSequence` and `Shoot` answer busy. |
| Only `Stop` interrupts a shot | Kept | `handle_cancel()` refuses once `started_` is set; `stop()` replaces the shift stage with zeros and latches them under the lock. |
| The handshake refuses the wrong device and version | Kept, but see finding 5 | `DefaultDriver::connect()` checks the name and the major version. A stale reply in the way is refused as the wrong device; a stale `Info` reply is accepted, see finding 1. |
| A shot that never ends is abandoned | Kept | The deadline of `ShotRunner::run()`, line 99 of [`shot_runner.cpp`](../src/freezer_driver/src/shot_runner.cpp), checked after every running status. |
| IN1 is debounced | Kept | `updateTrigger()`, 20 ms, fires on the press only. |
| No watchdog, by design | Judged sound | Outputs left on outside a shot are a choice of the host, cleared by a restart of the node. Finding 7 is the one case where that choice is made by a stalled `loop()` rather than the host, and it needs a misbehaving host. |

## The Serial Link

### The Firmware's Side

**TinyUSB's task runs in the interrupt only, and that is right.** In core 1.6.0, `tud_task()` runs from the USB interrupt (`USB.cpp`), and in thread mode only from `_SerialUSB::operator bool()` (`SerialUSB.cpp` line 126). [`updateHost()`](../src/freezer_mcu/src/main.cpp) reads DTR with `tud_cdc_connected()`, which only reads `tud_ready()` and the line state, and nothing else in `loop()` calls `operator bool()`. Not `Serial.dtr()`, which would set `ignore_dtr` and let `Serial.write()` send with the port closed: also right.

**The endpoint claims, suspected and dropped.** `Serial.read()` and `Serial.write()` reach `usbd_edpt_claim()` from `loop()`, and with no RTOS its lock is empty, so a check and a set of `claimed` could in principle interleave with the interrupt. `_SerialUSB::write()` masks the USB interrupt around `tud_cdc_write()`, the interrupt claims the IN endpoint only after a transfer on it completes, which `loop()`'s pre-check sees as busy, and the OUT endpoint is re-armed by the interrupt itself while the 256-byte FIFO has room. With this traffic the two sides do not meet; the note [TinyUSB's Task in the Interrupt Only](pr/usb-task-in-the-interrupt-only.md) measures 0 unanswered queries in about 52000 on the rig.

**Four points of the firmware, from reading:**

1. **`SerialPort::update()` passes one byte to `Serial.write()` per pass of `loop()`** (line 211), and `_SerialUSB::write()` flushes after each. When the IN endpoint is idle, that byte leaves in a transfer of its own; while it is busy, the next bytes wait in the FIFO and leave together. So a reply goes in at least 2 packets, the first often holding only the start delimiter. It costs latency, not correctness, and it makes finding 2 the likely outcome of a lost packet. Not a safety issue.
2. **`Serial.write()` spins while TinyUSB's FIFO is full.** A full FIFO loops on `tud_cdc_write_flush()`, and `connected()` is checked once, before the loop: a host that closes the port then does not free it. It takes a host that keeps the port open, sends requests and stops reading; the node always reads its reply, so the node alone never causes it. Finding 7.
3. **A request with a bad CRC gets no reply** (lines 179 to 185): ordinary for a framed protocol, since an error reply would itself need a request to answer. Its price is that a lost `Shoot` request, which fired nothing, and a lost `Shoot` reply, which fired, look the same to the host: finding 3.
4. **`MIN_HOLD_US` is 40 µs, the Nano 3's limit, not measured on the R4**, kept on purpose ([Running a Shot on a Hardware Timer](ARCHITECTURE.md#running-a-shot-on-a-hardware-timer)). It errs on the safe side, and a step shorter than the shift takes is not lost: `runDueBoundaries()` checks the time again after arming the match, latches late, and reports the lateness in `Status`. Not a bug.

**TinyUSB keeps what it has not sent across a reopening of the port.** `updateHost()` clears the firmware's own buffers when DTR rises, but TinyUSB's 256-byte TX FIFO is cleared only by `cdcd_reset()`, at a USB bus reset; a DTR change only makes it overwritable (`cdc_device.c` line 398). A reply the previous host did not collect, e.g. one in flight when the node stopped, comes first after the next open. The architecture's "drops the bytes it had not sent" holds for the firmware's buffer, not for TinyUSB's. This feeds findings 1 and 5.

### The Host's Side

**A request and its reply are paired by order alone.** Each method of [`DefaultDriver`](../src/freezer_driver/src/default_driver.cpp) writes its request and reads the next frame, and nothing drops what is waiting before a request. A frame that comes after its request gave up is therefore the answer to the next one, and so on: the host stays one reply behind. The length checks catch some of the mismatches, a 7-byte `Shoot` reply read as a 13-byte status, but not the reverse: a status reply passes as the answer to `Shoot`, `LoadSequence`, `SetOutputs` or `Stop`. Finding 1.

**The reader does not resynchronise.** [`DefaultFramedSerial::read()`](../modules/framed-serial/src/default_framed_serial.cpp) throws `start delimiter missing` on a first byte that is not `0x7E` (line 94) after consuming that byte only, and `incorrect frame length` on a delimiter after fewer than 3 bytes (line 108). After a reply that lost its first byte, each read consumes one or two bytes while the controller adds a whole reply per request. The firmware's reader, by contrast, skips everything up to a delimiter in `Waiting`, and starts a new frame on a delimiter after fewer than 3 bytes. Finding 2.

**`timeout` is per byte, not per answer.** [`DefaultSerial::set_timeout()`](../modules/framed-serial/src/default_serial.cpp) sets the constant of `serial::Timeout::simpleTimeout()`, and the reader calls `read()` one byte at a time. A reply that never comes costs 0.2 s; one whose bytes trickle in can take longer without failing. The comment `# s, for one answer` of [`freezer.yaml`](../src/freezer_node/config/freezer.yaml) says otherwise.

### A Failed Query Ends the Goal

**`ShotRunner::run()` ends the goal on the first query that fails**, by design: one request, one response, no retry. A status query that times out during a shot aborts the goal at once with `SerialException: timeout.`, while the controller runs the shot to its end; verified with a probe that loses one status reply. The runner keeps its table, the node records the shot id, and the next query reads the right reply, since a reply that never comes leaves the pairing intact. A goal sent at once after the abort may find the shot still running and be refused busy, from reading.

**Judged: sound while the firmware does not lose replies, and simple.** Its price is one goal, and with it the shot of a stack, per lost reply; StepIt Macro's `TakeShot` fails, and running it again takes a second picture. What is not sound is the case the rule does not cover: a reply that is late rather than lost, which finding 1 turns from one failed goal into every goal failing.

## Coupling and Duplication

- **The firmware's protocol layer is copied in StepIt Motors.** 8 files: `Buffer.h`, `CrcUtils.h`, `CrcUtils.cpp`, `DataBuffer.h`, `DataBuffer.cpp`, `Guard.h`, `SerialPort.h`, `SerialPort.cpp`, against `modules/stepit-motors/src/stepit_mcu`. Apart from their licence header (BSD-3-Clause here, MIT there), they differ in `DataBuffer::addInt()` and `removeInt()` (`int16_t` here, for the R4's 4-byte `int`; `int` there), and in `SerialPort::reset()`, which only the Freezer has. A fix to the framing of the firmware, e.g. writing a frame at once (recommendation 5), is made twice. Weight: medium.
- **The host's `framed-serial` and `serial` are shared, not copied**, at the same commits in the Freezer and in StepIt Motors (`d386c91` and `d8d1606`), as the rig's build requires. Finding 2 is in that shared reader, so it is StepIt Motors' too.
- **Dead code in the firmware copy**: `Guard.h` is included nowhere; `SerialPort::flush()`, `isBusyWriting()`, `crc_utils::crc_ccitt()`, `DataBuffer::removeLong()`, `addFloat()`, `removeFloat()` and `getCapacity()` are called nowhere. Weight: low; it is the price of the copy.
- **The limits are kept in three places**: the firmware's `Sequencer.h`, `FakeDriver::kLimits`, and the host reads the real ones at the handshake. The fake's must follow the firmware by hand. Weight: low.
- **`rig.yaml` names the Nano 3's port.** StepIt Macro's `rig.yaml` sets the Freezer's `usb_port` to `/dev/ttyUSB0` at its last commit, and to an FTDI FT232R `by-id` path in its working tree, both ports of the Nano 3's FT232; the Nano R4 is a `ttyACM` device, and the Freezer supports only the R4. With `use_fake: false`, the node never finds the board. Its `freezer` section keeps `baudrate: 9600`, which the R4 ignores. Weight: high for the rig, one line to fix there. Not a safety issue.

## Testability and Tests

| What | Tested | How |
|---|---|---|
| The frames of each command and their decoding | Yes, 16 tests | `test_default_driver`, against a mock of `FramedSerial`: whole frames, each the answer to its request. |
| The fake controller | Yes, 17 tests | `test_fake_driver`. |
| The sequence and its rules, the recipes | Yes, 8 and 5 tests | `test_sequence`, `test_recipes`. |
| A shot from load to end, its failures | Yes, 10 tests | `test_shot_runner`, on the fake's clock. |
| The node end to end | Yes, 22 tests | `test_freezer_node`, on the steady clock, with a fake. |
| The board page | Yes, 29 tests | vitest: the bits, the timeline, the shots, rosbridge. |
| A byte stream with a frame damaged, late or stale | No | `framed-serial`'s own `read_start_delimiter_missing` checks that the read throws, not that the next one recovers. Findings 1, 2 and 5 have no test that could see them. |
| A lost reply | No | `FakeDriver::Failure` has `NeverFinish` and `ResetDuringShot` only: no failure loses a reply, so neither the abort of [A Failed Query Ends the Goal](#a-failed-query-ends-the-goal) nor finding 3 is tested. |
| The firmware | No | By design ([Tests](ARCHITECTURE.md#tests)), checked by hand on the Nano. Its framing, `Buffer` and `DataBuffer` are plain C++ that could run on the host, but `SerialPort` calls the global `Serial`. |

The missing seams: a fake `framed_serial::Serial` that plays the controller byte by byte, under the real `DefaultFramedSerial` and `DefaultDriver`, which the probes of findings 1 and 2 are, about 150 lines; a failure of `FakeDriver` that loses the reply to a given command; and a `Serial`-like interface under the firmware's `SerialPort`, so that the same frames can be tested on both sides.

## Implementation Issues

Ordered by severity. **Safety** says whether the issue can leave an output on, cut a shot short or fire a table that was not checked.

1. **A late or stale reply leaves the host one reply behind, and nothing puts it back in step.** Verified. See [The Host's Side](#the-hosts-side). Two probes: a status reply that arrives after its query timed out, and an `Info` reply waiting in the controller's buffer when the port opens. In both, the status queries that follow all succeed, each reading the reply before; then 3 shots in a row each fire on the controller, which counts shot ids 1, 2 and 3, and each goal aborts, the first with `Shoot response is too short: 11 00 00 27 10.` (the reply to `LoadSequence`), the next with `Status response is too short: 11 00 02 00 00 27 10.` (the reply to `Shoot`). With the stale `Info`, the handshake succeeds and the first status fails with `Unknown controller state 02.` Nothing ends it: `watch()` closes the port after 3 failed queries in a row, and while idle its queries succeed, each one behind; only a restart of the node, or 3 failures in a row, closes the port. Meanwhile `~/shots` tells each fired shot as one of IN1, as in finding 3, and a refusal of `SetOutputs`, `LoadSequence` or `Stop` can be read as a success, from reading. The causes, from reading: a reply later than `timeout`, which needs the controller or the link to stall for 0.2 s, and a reply left in TinyUSB's FIFO by the previous session (see [The Firmware's Side](#the-firmwares-side)), e.g. when the node stops during one of its status queries, which take about 5 ms of every 200. The architecture's "a frame the host did not ask for can never be mistaken for the answer to its next request" ([The Protocol](ARCHITECTURE.md#the-protocol)) does not hold on the host. **Safety: no**: every shot runs to its end with all outputs off, and the firmware checks every table it loads; but a `LoadSequence` refused busy and read as a success makes the next `Shoot` fire the table loaded before, a checked table the goal did not ask for, from reading.
2. **A reply that loses its first byte puts the host's reader out of step for good.** Verified. See [The Host's Side](#the-hosts-side). The probe lost the first byte of one status reply from a controller that answers every request: 0 of the next 20 status queries were answered, and 355 bytes were left unread. During a shot the goal aborts at the first failure; while idle, `watch()` closes the port after 3, and the node connects again 2 s later, when the bytes TinyUSB had not sent yet can lead to finding 1 or 5. A misaligned read never passes the CRC, so no reply is taken for another. With one byte per USB packet at the start of a reply (point 1 of [The Firmware's Side](#the-firmwares-side)), a lost packet is likely to be that byte. The reader is shared with StepIt Motors. **Safety: no**: the shot runs to its end on the controller, all outputs off.
3. **A lost reply to `Shoot` aborts the goal of a shot that fired, and the node tells it as a shot of IN1.** Verified. `ShotRunner::run()` lets the exception of `driver_.shoot()` (line 80) abort the shot; `execute()` updates `known_shot_id_` only when the result has a shot id (line 622), so the next `watch()` takes the new id for the trigger's. The probe, with a driver that fires and then throws `timeout`: the goal ended `ABORTED` with `timeout`, and `~/shots` told shot 1 as `STARTED` and `ENDED` with `source` `SOURCE_TRIGGER`. StepIt Macro's `TakeShot` fails while the camera took its picture, and running it again takes a second one. A lost reply to `LoadSequence` is harmless, since the runner forgets its table first. **Safety: no**: the shot ran to its end.
4. **The rig cannot reach the board with `use_fake: false`.** From reading. See [Coupling and Duplication](#coupling-and-duplication): `rig.yaml` names a port of the Nano 3. **Safety: no.**
5. **A stale status reply refuses the Freezer at the handshake.** Verified on the host, from reading on the firmware. `get_info()` accepts any success frame of 13 bytes or more (line 187), so a status reply left in TinyUSB's FIFO reads as an `Info` with an empty name, and `connect()` refuses it as another device, without trying again (line 136). The probe: `connect()` returned `false` and logged `Unexpected device on serial port: expected "FREEZER", got "".` The node closes the port and tries again 2 s later, when the `Info` reply it did not read may be the stale one: finding 1. **Safety: no.**
6. **A shot of IN1 right after the node's shot fails the node's goal.** Verified. When IN1 fires between the end of the node's shot and the next poll, the status reads running with another id, and `ShotRunner::run()` reports its steps through `progress` as the node's, until the deadline of the node's shot, its duration plus `end_margin`, passes: the probe pressed the fake's trigger as a 10 ms shot ended, and the outcome was `Aborted` with `Shot 1 did not end within its 10000 us and the margin.`, after 16 progress reports. With a table shorter than `end_margin`, the trigger's shot ends first, and the goal aborts with `The controller lost shot 1: it may have reset.` (line 121), forgetting its table. IN1 is for testing, so this needs a press within one poll of a shot's end. **Safety: no.**
7. **`loop()` can stall in `Serial.write()`.** From reading; point 2 of [The Firmware's Side](#the-firmwares-side). While stalled, a running shot ends on the timer with all outputs off; `Stop`, `Status` and IN1 are not served. **Safety: no for a running shot**; a pattern set by `SetOutputs` stays until the host reads again or the port is reopened, as without a watchdog.
8. **A lost reply to `SetOutputs` leaves `~/outputs` wrong.** From reading. `handle_set_outputs()` publishes the pattern only on a success (line 646); on a timeout the controller may have latched it, and the page then shows a light, or a camera's shutter, off while it is on. A second click repairs it. **Safety: indirectly**: an output can be on while the page shows it off, until the next command.
9. **The reconnection thread can open the port that `watch()` is closing.** From reading. `disconnect()` sets `connected_` to false (line 330), publishes the status, then closes the port; the thread of `reconnect_loop()` may meanwhile see `connected_` false and connect, after which the close lands on the new connection while `connected_` reads true. Every call then fails until `watch()` lets it go again, about 0.6 s and 2 s later. The window is a publish wide, every 2 s. **Safety: no.**
10. **The started callback calls `stop()` under `start_mutex_`.** From reading. Line 519 sends `Stop` and waits for its reply while holding the mutex that `handle_cancel()` and `handle_stop()` take on the executor, which can then wait up to `timeout`. **Safety: no.**
11. **An `Echo` reply longer than the write buffer is cut without a sign.** From reading. `Buffer::add()` drops a byte when full, and the escaped reply of a long `Echo` can pass the 200 bytes of `SerialPort`'s write buffer: the frame loses its end delimiter, the host takes the next reply's start delimiter as its end and fails on the CRC, and the read after starts inside that reply: finding 2. The driver never sends `Echo`. **Safety: no.**
12. **Comments and documents that say another thing than the code.** From reading. [`Sequencer.h`](../src/freezer_mcu/include/Sequencer.h) speaks of `Timer1` twice and `runDueBoundaries()` of a wrap "32.8 ms later", both the Nano 3's: GPT1 wraps after about 89 s. [`ControllerStatus.msg`](../src/freezer_msgs/msg/ControllerStatus.msg) line 11 gives `/dev/ttyUSB0` as an example port. The module's `CLAUDE.md` asks for the MIT header; 38 of the 39 C++ source files and the 3 `package.xml` carry BSD-3-Clause, as the `LICENSE` does, and `Sequencer.h` carries none. `freezer.yaml` calls `timeout` "for one answer" (see [The Host's Side](#the-hosts-side)). [The Protocol](ARCHITECTURE.md#the-protocol) promises a pairing the host does not keep (finding 1). **Safety: no.**
13. **The fake differs from the firmware in small ways.** `FakeDriver::reset()` keeps the latches the shot had planned: after a reset 1 ms into a shot, `outputs()` at 7 ms read `0xF0`, the shot's second step, where the Nano switches everything off; verified. From reading: `get_info()` reports version 2.0.0 (line 58), the firmware 2.1.1; `worst_lateness_us_` is not cleared when a shot starts, so a status during a shot reports the previous shot's lateness; and no failure loses a reply. **Safety: no**, but a test of a reset on the fake does not see the outputs go off.

No security issue was found. The node opens only the configured port, and the board page reaches the node only through rosbridge, which is open to the network on port 9092, as the launch file intends.

## Recommendations

Ordered by value against effort; each fits one pull request.

1. **Keep each reply with its request on the host** (findings 1, 2, 5, 11). Before each request, drop whatever is waiting on the port: a `flush_input()` on `framed_serial::Serial`, over `serial::Serial::flushInput()`, called by `DefaultDriver`; and check the exact length of each reply, so that a reply of another command is an error, not an answer. In `DefaultFramedSerial::read()`, skip bytes up to a `0x7E` in `Waiting` instead of throwing, and treat a delimiter after fewer than 3 bytes as the start of a new frame, as the firmware does. Add the byte-level fake controller of the probes to the tests, with a late, a stale and a cut reply. Trade-off: it changes the shared submodule, so StepIt Motors moves to the same commit; a reply later than the next flush is still read once out of turn, which only a command id in each reply, a new major version of the protocol, would rule out.
2. **Drop TinyUSB's buffers when the port opens** (findings 1, 5). In `updateHost()`, call `tud_cdc_write_clear()` and `tud_cdc_read_flush()` when DTR rises, with the USB interrupt masked, so that the architecture's "drops the bytes it had not sent" holds for every buffer. Trade-off: two lines of firmware to check on the board, version 2.1.2; recommendation 1 alone covers the host.
3. **Tell a fired shot as the node's** (finding 3). In `ShotRunner::run()`, when `shoot()` throws, query the status: running, or idle with a last shot id past the one known before, means the shot started, and the runner reports it as started, or at least returns its id so that `execute()` records it. Trade-off: one more query when `Shoot` fails, and the runner needs the last id before the shot, from the node's `known_shot_id_` or a query of its own.
4. **Point `rig.yaml` at the R4** (finding 4). In StepIt Macro, the Freezer's `usb_port` as `/dev/serial/by-id/<the Nano R4's id>`, which `ls /dev/serial/by-id` shows on the rig's computer, and drop `baudrate`. Trade-off: none; it is the rig's file.
5. **Write a frame at once in the firmware** (point 1 of the firmware, finding 7). In `SerialPort::update()`, pass every byte of the write buffer that `Serial.availableForWrite()` allows in one `Serial.write(buffer, size)`, and never more, so `loop()` never waits on the FIFO. A reply then leaves in one packet, a lost packet loses a whole reply rather than its first byte, and a full FIFO cannot stall `loop()`. Trade-off: the copy in StepIt Motors diverges further, unless both move together; check on the board.
6. **Follow the trigger outside the node** (separation of concerns, finding 6). Move the logic of `watch()` into a `TriggerWatcher` of `freezer_driver`, with an injected clock as `ShotRunner` has, and let `ShotRunner` end a shot when it sees another id running. Trade-off: moves about 117 lines, and the node tests of the trigger move with them.
7. **Make the fake fail like the board** (finding 13, testability). Switch the outputs off in `FakeDriver::reset()`, report the firmware's version, clear the lateness at the start of a shot, and add a lost reply per command, at least for `Status`, `Shoot` and `SetOutputs`, so that [A Failed Query Ends the Goal](#a-failed-query-ends-the-goal) and finding 3 are tested. Trade-off: small; the node tests of a reset may need updating.
8. **Smaller fixes in one pass** (findings 8, 9, 10, 12). Publish `~/outputs` after a `SetOutputs` that timed out only once a status confirms it, or say in the reply that the outputs are unknown; close the port before clearing `connected_` in `disconnect()`, under the lock `reconnect_loop()` waits on; send the late `Stop` after releasing `start_mutex_`; correct the comments, the message's example port, the comment of `timeout`, the licence named in `CLAUDE.md` and the header of `Sequencer.h`, and the promise of [The Protocol](ARCHITECTURE.md#the-protocol) until recommendation 1 makes it true. Trade-off: none worth a design decision.
