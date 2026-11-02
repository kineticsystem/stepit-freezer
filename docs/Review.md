# StepIt Freezer Review

## Table of Contents <!-- omit in toc -->

- [Scope and Method](#scope-and-method)
- [Verdict](#verdict)
- [The Packages and Their Boundaries](#the-packages-and-their-boundaries)
- [Interfaces and Abstractions](#interfaces-and-abstractions)
- [Separation of Concerns](#separation-of-concerns)
- [SOLID](#solid)
- [The Safety Nets](#the-safety-nets)
- [The Changes Not Yet on Main](#the-changes-not-yet-on-main)
  - [Firmware 2.1.1: TinyUSB's Task in the Interrupt Only](#firmware-211-tinyusbs-task-in-the-interrupt-only)
  - [Asking Again and Dropping Late Replies](#asking-again-and-dropping-late-replies)
  - [Reporting the Missed Replies](#reporting-the-missed-replies)
  - [The Four Firmware Points Left Out](#the-four-firmware-points-left-out)
- [Coupling and Duplication](#coupling-and-duplication)
- [Testability and Tests](#testability-and-tests)
- [Implementation Issues](#implementation-issues)
- [Recommendations](#recommendations)

Reviewed on 2026-11-02, at commit `583e60f` of the branch `report-missed-replies`. It judges the design that [docs/ARCHITECTURE.md](ARCHITECTURE.md) describes.

## Scope and Method

This review covers the whole module: the firmware of the Nano R4 in [`src/freezer_mcu`](../src/freezer_mcu), 10 files and 1859 lines; the host library [`freezer_driver`](../src/freezer_driver), its tests and its fake; the node [`freezer_node`](../src/freezer_node), its launch file and its configuration; the messages of [`freezer_msgs`](../src/freezer_msgs); and the board page in [`web`](../web), read for its interface to the node. The host side of `framed-serial`, shared with StepIt Motors, is judged where the Freezer depends on it. `serial`, third-party, is not reviewed.

The review was asked for at `1965f77`. While it ran, `583e60f` landed on the branch: `uint32` for every count of missed replies, the fake losing the first reply of a shot of IN1 too, and a note of the pull request. Every file was read, every probe built and the suite run at `583e60f`, so that is the commit reviewed.

We read every file of the parts above, the Arduino Renesas core 1.6.0 that the firmware runs on (`SerialUSB.cpp`, `USB.cpp`, and TinyUSB's `cdc_device.c`, `usbd.c`, `tusb.c` and `dcd_rusb2.c`), the neighbours named by the rig's `CLAUDE.md` (`rig.yaml` of StepIt Macro, and the copies of the protocol in StepIt Motors), and the notes of the three pull requests under review in [docs/pr](pr/README.md). The safety nets and what is deliberately not there are judged as design, not reported as bugs.

Each suspected bug of the host was reproduced with a throwaway gmock probe, built with `freezer_driver` and `freezer_node` in a scratch workspace inside the `stepit-macro` container, on ROS domain 77, then deleted. The probes of the node went in `src/freezer_node/tests`, the only place that can start a node. A finding marked **verified** failed its probe; the others are **from reading**. The firmware has no automated test, and no board was touched: every finding of the firmware is from reading. No probe passed, so no suspicion was dropped by a probe; one was dropped by reading, see [The Changes Not Yet on Main](#firmware-211-tinyusbs-task-in-the-interrupt-only).

The whole suite, at `583e60f`: `colcon test` of `freezer_driver` and `freezer_node` passes 84 gmock tests in 6 programs, with no failure and no skip, in about 7.6 s; `vitest run` of the board page passes 29 tests in 4 files in 0.2 s.

## Verdict

The design is sound and its one rule holds: once a shot starts, the hardware timer owns it, every table is checked whole by the host and again by the firmware, and every shot ends with all outputs off. The packages keep to what they own: `freezer_driver` has no ROS interface, `ShotRunner` runs a shot on an injected clock, and the firmware is small enough to read in an hour. No finding of this review is a safety issue for a camera or a light in the sense of the project: no path was found that leaves an output on after a shot, cuts a shot short other than by `Stop`, or fires a table the firmware did not check.

The weakness is **recovery on the serial link**, the area of the three changes under review. Firmware 2.1.1 removes the cause of the lost replies, and from reading it removes the only one: no other call of `loop()` runs TinyUSB's task. But the host's recovery from a lost reply works only when what is lost is a whole reply, or its end. A reply that loses its first byte puts the host's reader out of step for good: every following request fails until the port is closed (finding 1, verified). A lost reply to `Shoot` aborts the goal of a shot that fired, which the node then tells as a shot of IN1 (finding 2, verified). Neither touches the outputs; both cost a stack its shot, which is what pull request #18 set out to prevent.

## The Packages and Their Boundaries

| Rule | Verdict | Evidence |
|---|---|---|
| `freezer_msgs` holds no code | Kept | One action, four messages, one service. |
| `freezer_driver` has no ROS interface | Kept | Its only ROS include is `rclcpp/logging.hpp`, in [`default_driver.cpp`](../src/freezer_driver/src/default_driver.cpp), for its logger, as [The Packages](ARCHITECTURE.md#the-packages) says. It links `framed_serial` and `rclcpp`. |
| `freezer_node` reaches the controller only through `Driver` | Kept | Every call goes through `SynchronizedDriver`. Three `dynamic_cast`s to `FakeDriver` in [`freezer_node.cpp`](../src/freezer_node/src/freezer_node.cpp) name the device `fake` and offer `~/fake/press_trigger`. |
| `web` knows the node only through rosbridge | Kept | [`freezer.ts`](../web/src/freezer/freezer.ts) uses the topics, the services, the action and `get_parameters`; nothing else. |
| No cycle | Kept | `freezer_node` → `freezer_driver` → `framed_serial` → `serial`; `freezer_node` → `freezer_msgs`. In the firmware, `main.cpp` → `Sequencer`, `SerialPort` → `DataBuffer` → `Buffer`, `CrcUtils`: one direction. |

`FakeDriver` ships in the production library, under `include/freezer_driver/fake`. That is a choice, not a leak: the node runs with it by default, and the launch file selects it with `use_fake`.

## Interfaces and Abstractions

| Interface | Where | Judgement |
|---|---|---|
| `Driver` | [`driver.hpp`](../src/freezer_driver/include/freezer_driver/driver.hpp) | One method per command, one request and one response each: the protocol, seen from the host. Three implementations substitute for each other. `connect()` returns a `bool` and drops the `InfoResponse` it read, so [`FreezerNode::connect()`](../src/freezer_node/src/freezer_node.cpp) asks `get_info()` a second time for the limits: two `Info` round trips per connection. |
| `ShotRunner` with `Clock` and `Sleep` | [`shot_runner.hpp`](../src/freezer_driver/include/freezer_driver/shot_runner.hpp) | The best seam of the module: a shot runs in a test on the fake's clock, instantly and exactly, and 13 tests use it. |
| `ShotRunner::Callbacks` | same | `start`, `started` and `progress` keep the cancel and stop rules in the node and the polling in the runner. Clean. |
| `SynchronizedDriver` and `with_driver()` | [`synchronized_driver.hpp`](../src/freezer_driver/include/freezer_driver/synchronized_driver.hpp) | A decorator that serialises the calls, so a stop goes between two status queries. `driver()` hands out the wrapped driver for a `dynamic_cast`, a small test seam in the production interface. |
| `framed_serial::FramedSerial` | the submodule | Mocked by [`mock_framed_serial.hpp`](../src/freezer_driver/tests/mock/mock_framed_serial.hpp): the tests of `DefaultDriver` see whole frames, never bytes, which is why finding 1 had no test. |
| The firmware's `SerialPort` | [`SerialPort.cpp`](../src/freezer_mcu/src/SerialPort.cpp) | Calls the global `Serial` directly: the framing of the firmware cannot be compiled and tested on the host. |
| `sequencer::` | [`Sequencer.h`](../src/freezer_mcu/include/Sequencer.h) | A free-function module with a small interface, `load`, `start`, `stop`, `reset`, `setOutputs`, `status`: the timer and the shift registers stay behind it, and `main.cpp` never touches a register. |

## Separation of Concerns

- **The trigger's shots are followed in the node.** [`FreezerNode::watch()`](../src/freezer_node/src/freezer_node.cpp), lines 761 to 877, tells a new shot id from a reset, estimates the start of a shot from its elapsed time, follows it to its end and tells a stop: a state machine of about 120 lines in the ROS class, testable only through a node and a fake on the steady clock. `ShotRunner` shows how the same logic would sit in `freezer_driver`, on an injected clock.
- **The recipes are known twice in the node.** `declare_sequence_parameters()` and `build_sequence()` each hold an `if` chain over `flash` and `timed_light`, with the parameter names spelled in both. A third recipe changes both.
- **The node rebuilds the table after accepting it.** `handle_goal()` builds and validates the table; `execute()` builds it again from the parameters (line 507) and does not validate it. A parameter changed between the two goes to the controller unchecked by the host; the firmware still checks it, so it is not a table fired unchecked. Building once and handing the table to the thread would remove the gap and one of the two builds.
- **The firmware keeps its layers.** `main.cpp` holds the commands and their checks, `Sequencer.cpp` the timer and the registers, `SerialPort.cpp` the framing. Nothing of the protocol reaches the interrupt.

## SOLID

| Principle | Verdict | Where |
|---|---|---|
| Single responsibility | Partly violated | [`freezer_node.cpp`](../src/freezer_node/src/freezer_node.cpp), 878 lines, holds the action, the services, the watch of the trigger, the reconnection thread and the status. The driver classes and `ShotRunner` each do one thing. |
| Open/closed | Partly violated | A new recipe changes the two `if` chains of the node, see above. A new command changes `Driver` and its 3 implementations, which is expected of a protocol. |
| Liskov substitution | No violation found | The node and `ShotRunner` run the same against `DefaultDriver` and `FakeDriver`. The fake differs in details, see finding 12, not in contract. |
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
| The handshake refuses the wrong device and version | Kept, but see finding 4 | `DefaultDriver::connect()` checks the name and the major version; a stale reply in the way is refused as the wrong device. |
| A shot that never ends is abandoned | Kept | The deadline of `ShotRunner::run()`, line 99, checked after every running status. |
| IN1 is debounced | Kept | `updateTrigger()`, 20 ms, fires on the press only. |
| No watchdog, by design | Judged sound | Outputs left on outside a shot are a choice of the host, cleared by a restart of the node. Finding 6 is the one case where that choice is made by a stalled `loop()` rather than the host, and it still needs a misbehaving host. |

## The Changes Not Yet on Main

### Firmware 2.1.1: TinyUSB's Task in the Interrupt Only

**The fix is right, and complete for `tud_task()`.** In core 1.6.0, `tud_task()` runs from `_usbfs_interrupt_handler()` of `USB.cpp`, and in thread mode only from `_SerialUSB::operator bool()`; `updateHost()` no longer calls it, and nothing else in `loop()` does. `tud_cdc_connected()` reads `tud_ready()` and the line state, nothing more.

**The endpoint claims, suspected and dropped.** `Serial.read()` and `Serial.write()` still reach `usbd_edpt_claim()` from `loop()`, and with no RTOS its lock is empty (`OSAL_MUTEX_REQUIRED` is 0), so a check and a set of `claimed` could interleave with the interrupt. We traced both sides: the interrupt only claims the IN endpoint after a transfer on it completes, which `loop()` cannot see half-way, since `loop()`'s pre-check fails while that transfer is busy; the OUT endpoint is re-armed by the interrupt itself while the 256-byte FIFO has room for 64; and `dcd_edpt_xfer()` masks the USB interrupt around the registers. With our traffic the two sides do not meet. This agrees with the note [Report the Missed Replies](pr/report-missed-replies.md) and with 0 misses in about 52000 queries.

### Asking Again and Dropping Late Replies

**Asking again is the right policy**: the controller owns the shot, so a lost status says nothing about it. The deadline still holds at the next reply, and a miss does not reset it.

**Dropping a late reply is right, and incomplete.** `DefaultDriver::request()` drops frames until a read fails, and `DefaultFramedSerial::read()` fails at once on a first byte that is not `0x7E`, consuming one byte and no more. So after a reply that lost its start delimiter, every drain stops after one byte, every read fails after one more, and the controller adds a whole reply per request: the host never catches up. Finding 1 measures it. The firmware's reader, by contrast, resynchronises: in `Waiting` it skips everything up to a delimiter, and a delimiter after fewer than 3 bytes starts a new frame.

**Every recovery costs at least one `timeout`.** The drain reads until a read fails; with nothing late, that is 0.2 s; with a late reply, the reply and then 0.2 s. A missed status therefore costs 0.4 s and a poll before the next answer, as [#18](pr/018-tolerate-a-lost-reply.md) says. `timeout` is per byte, not per answer: [`DefaultSerial::set_timeout()`](../modules/framed-serial/src/default_serial.cpp) sets the constant of `serial::Timeout::simpleTimeout()`, and the reader calls `read()` one byte at a time. The comment `# s, for one answer` of [`freezer.yaml`](../src/freezer_node/config/freezer.yaml) says otherwise.

### Reporting the Missed Replies

The counting is consistent: `ShotRunner::Result::missed_status`, the result of `Shoot`, `Shot.missed_status` and `ControllerStatus.missed_status` are all `uint32`; a shot that fails after 3 misses still adds them, since `++result.missed_status` comes before the throw; `missed_status_` is an atomic shared by the shot's thread and the executor; and `publish_status()` from the shot's thread is safe, the publisher being thread-safe. Misses of `watch()` while no goal runs are not counted; `ControllerStatus.msg` says "during the shots", and that is what the code does. The node test `a_lost_status_reply_is_reported` covers the result, `~/shots` and `~/status`, and the count over two shots.

What the count cannot see: a lost reply to `Shoot`, `LoadSequence`, `SetOutputs` or `Stop`. Those throw at once, and finding 2 shows what one costs.

### The Four Firmware Points Left Out

The note [TinyUSB's Task in the Interrupt Only](pr/usb-task-in-the-interrupt-only.md) leaves four points out. From reading:

1. **`SerialPort::update()` sends one byte per pass of `loop()`: confirmed; one USB packet per byte: partly.** `update()` passes one byte to `Serial.write()` (line 211), and `_SerialUSB::write()` flushes after each. When the IN endpoint is idle, that byte leaves in a transfer of its own; while it is busy, the next bytes wait in the FIFO and the interrupt sends them together when the transfer ends. So a reply goes in at least 2 packets, and in one per byte only if the host collects each packet before `loop()` passes again. It costs latency, not correctness: the 5 ms of a status round trip measured in [Design Decisions](ARCHITECTURE.md#design-decisions-and-trade-offs) has room to shrink. Not a safety issue.
2. **`Serial.write()` spins while the FIFO is full: confirmed.** In `_SerialUSB::write()`, a full FIFO loops on `tud_cdc_write_flush()` and `continue`, and `connected()` is checked once, before the loop: a host that closes the port then does not free it. It takes a host that keeps the port open, sends requests and stops reading until 256 bytes of FIFO and the host's tty buffer are full; the node always reads its reply, so the node alone never causes it. While stalled, a running shot still ends on the timer with all outputs off; `Stop`, `Status` and IN1 are not served, and outputs set by `SetOutputs` stay as they are, as without a watchdog. Not a safety issue for a shot. Finding 6.
3. **A request with a bad CRC gets no reply: confirmed.** `update()` drops it (lines 179 to 185) and waits for the next delimiter. That is ordinary for a framed protocol; an error reply would itself need a request to answer. Its price is that a lost `Shoot` request, which fired nothing, and a lost `Shoot` reply, which fired, look the same to the host: finding 2.
4. **`MIN_HOLD_US` was never measured on the R4: confirmed, and not a bug.** It is 40 µs, the Nano 3's limit, kept on purpose ([Running a Shot on a Hardware Timer](ARCHITECTURE.md#running-a-shot-on-a-hardware-timer)). On the R4 it errs on the safe side. A step shorter than the shift takes is not lost either: `runDueBoundaries()` checks the time again after arming the match, latches late, and reports the lateness in `Status`. Measuring it needs a logic analyser on `ST_CP`.

## Coupling and Duplication

- **The firmware's protocol layer is copied in StepIt Motors.** 8 files: `Buffer.h`, `CrcUtils.h`, `CrcUtils.cpp`, `DataBuffer.h`, `DataBuffer.cpp`, `Guard.h`, `SerialPort.h`, `SerialPort.cpp`, against `modules/stepit-motors/src/stepit_mcu`. They differ in their licence header (BSD-3-Clause here, MIT there), in `DataBuffer::addInt()` and `removeInt()` (`int16_t` here, for the R4's 4-byte `int`; `int` there), and in `SerialPort::reset()`, which only the Freezer has. A fix to the framing of the firmware, e.g. writing a frame at once (recommendation 4), is made twice. Weight: medium.
- **The host's `framed-serial` and `serial` are shared, not copied**, at the same commits in the Freezer and in StepIt Motors (`d386c91` and `d8d1606`), as the rig's build requires. Finding 1 is in that shared reader, so it is StepIt Motors' too.
- **Dead code in the firmware copy**: `Guard.h` is included nowhere; `SerialPort::flush()`, `isBusyWriting()`, `crc_utils::crc_ccitt()`, `DataBuffer::removeLong()`, `addFloat()`, `removeFloat()` and `getCapacity()` are called nowhere. Weight: low; it is the price of the copy.
- **The limits are kept in three places**: the firmware's `Sequencer.h`, `FakeDriver::kLimits`, and the host reads the real ones at the handshake. The fake's must follow the firmware by hand. Weight: low.
- **`rig.yaml` names the Nano 3's port.** StepIt Macro's `rig.yaml` sets the Freezer's `usb_port` to `/dev/ttyUSB0` at its last commit, and to an FTDI FT232R `by-id` path in the working tree, both ports of the Nano 3's FT232; the Nano R4 is a `ttyACM` device, and the Freezer supports only the R4. With `use_fake: false`, the node never finds the board. It also keeps `baudrate: 9600`, which the R4 ignores. Weight: high for the rig, one line to fix there. Not a safety issue.

## Testability and Tests

| What | Tested | How |
|---|---|---|
| The frames of each command and their decoding | Yes, 18 tests | `test_default_driver`, against a mock of `FramedSerial`: whole frames only. |
| The fake controller | Yes, 17 tests | `test_fake_driver`. |
| The sequence and its rules, the recipes | Yes, 8 and 5 tests | `test_sequence`, `test_recipes`. |
| A shot from load to end, its failures, lost status replies | Yes, 13 tests | `test_shot_runner`, on the fake's clock. |
| The node end to end | Yes, 23 tests | `test_freezer_node`, on the steady clock, with a fake. |
| The board page | Yes, 29 tests | vitest: the bits, the timeline, the shots, rosbridge. |
| A byte stream with a frame damaged | No | `framed-serial`'s own `read_start_delimiter_missing` checks that the read throws, not that the next one recovers. Finding 1 had no test that could see it. |
| A lost reply to anything but `Status` | No | `FakeDriver::Failure` has `LoseStatusReply` only. Finding 2. |
| The firmware | No | By design ([Tests](ARCHITECTURE.md#tests)), checked by hand on the Nano. Its framing, `Buffer` and `DataBuffer` are plain C++ that could run on the host, but `SerialPort` calls the global `Serial`. |

The missing seams: a fake `framed_serial::Serial` that plays a controller byte by byte, which the probe of finding 1 is, about 80 lines; a failure of the fake per command, not only for `Status`; and a `Serial`-like interface under the firmware's `SerialPort`, so that the same frames can be tested on both sides.

## Implementation Issues

Ordered by severity. **Safety** says whether the issue can leave an output on, cut a shot short or fire a table that was not checked.

1. **A reply that loses its start delimiter puts the host's reader out of step for good.** Verified. `DefaultFramedSerial::read()` throws `start delimiter missing` on the first byte that is not `0x7E` ([`default_framed_serial.cpp`](../modules/framed-serial/src/default_framed_serial.cpp) line 94) after consuming only that byte, and the drain of `DefaultDriver::request()` stops at the first exception ([`default_driver.cpp`](../src/freezer_driver/src/default_driver.cpp) line 182). The probe fed `DefaultDriver` a controller that answers every request and lost the first byte of its first reply: 0 of the next 20 status queries were answered, and 298 bytes were left unread. During a shot, `ShotRunner` gives up after 3 misses and the goal aborts; while idle, `watch()` closes the port after 3, and the node connects again 2 s later. A misaligned read never passes the CRC, so no reply is taken for another. On the Nano R4 the first byte of a reply tends to leave in a USB packet of its own (point 1 above), so a lost packet is likely to be that one. The reader is shared with StepIt Motors. **Safety: no**: the shot runs to its end on the controller, all outputs off.
2. **A lost reply to `Shoot` aborts the goal of a shot that fired, and the node tells it as a shot of IN1.** Verified. `ShotRunner::run()` lets the exception of `driver_.shoot()` (line 80) abort the shot; `execute()` updates `known_shot_id_` only when the result has a shot id (line 639), so the next `watch()` takes the new id for the trigger's. The probe, with a fake that fires and then throws: the goal ended `ABORTED` with `timeout`, and `~/shots` told shot 1 as `STARTED` and `ENDED` with `source` `SOURCE_TRIGGER`. StepIt Macro's `TakeShot` fails while the camera took its picture, and running it again takes a second one. A lost reply to `LoadSequence` is harmless, since the runner forgets its table first. **Safety: no**: the shot ran to its end.
3. **The rig cannot reach the board with `use_fake: false`.** From reading. See [Coupling and Duplication](#coupling-and-duplication): `rig.yaml` names a port of the Nano 3. **Safety: no.**
4. **A reply left from the session before refuses the Freezer at the handshake.** Verified on the host, from reading on the firmware. `updateHost()` clears the firmware's own buffers when DTR rises, but TinyUSB's 256-byte TX FIFO is cleared only at a USB bus reset (`cdcd_reset()`): a reply the previous host did not collect, e.g. when the node closed the port after timeouts, comes first after the next open. `get_info()` accepts any success frame of 13 bytes or more (line 223), so a status reply reads as an `Info` with an empty name, and `connect()` refuses it as another device, without trying again (line 136). The probe: `connect()` returned `false` and logged `Unexpected device on serial port: expected "FREEZER", got ""`. The node connects at its next try, 2 s later. The architecture's "drops the bytes it had not sent" holds for the firmware's buffer, not for TinyUSB's. **Safety: no.**
5. **A shot of IN1 right after the node's shot fails the node's goal.** Verified. When IN1 fires between the end of the node's shot and the next poll, the status reads running with another id, then idle with that id, and `ShotRunner::run()` aborts with `The controller lost shot 1: it may have reset.` (line 139), and forgets its table. The probe pressed the fake's trigger as the shot ended: the outcome was `Aborted` with that message. Meanwhile, `progress` reported the steps of the trigger's shot as the node's. IN1 is for testing, so this needs a press within one poll of a shot's end. **Safety: no.**
6. **`loop()` can stall in `Serial.write()`.** From reading; point 2 of [The Four Firmware Points Left Out](#the-four-firmware-points-left-out). **Safety: no for a running shot**, which the timer ends with all outputs off; a pattern set by `SetOutputs` stays until the host reads again or the port is reopened, as without a watchdog.
7. **A lost reply to `SetOutputs` leaves `~/outputs` wrong.** From reading. `handle_set_outputs()` publishes the pattern only on a success (line 663); on a timeout the controller may have latched it, and the page then shows a light, or a camera's shutter, off while it is on. A second click repairs it. **Safety: indirectly**: an output can be on while the page shows it off, until the next command.
8. **The reconnection thread can open the port that `watch()` is closing.** From reading. `disconnect()` sets `connected_` to false (line 332), publishes the status, then closes the port; the thread of `reconnect_loop()` may meanwhile see `connected_` false and connect, after which the close lands on the new connection while `connected_` reads true. Every call then fails until `watch()` lets it go again, about 0.6 s and 2 s later. The window is a publish wide, every 2 s. **Safety: no.**
9. **The started callback calls `stop()` under `start_mutex_`.** From reading. Line 522 sends `Stop` and waits for its reply while holding the mutex that `handle_cancel()` and `handle_stop()` take on the executor, which can then wait up to `timeout`. **Safety: no.**
10. **An `Echo` reply longer than the write buffer is cut without a sign.** From reading. `Buffer::add()` drops a byte when full, and the escaped reply of a long `Echo` can pass the 200 bytes of `SerialPort`'s write buffer: the frame loses its end delimiter and joins the next reply, which then trips finding 1. The driver never sends `Echo`. **Safety: no.**
11. **Comments and documents that say another thing than the code.** From reading. [`Sequencer.h`](../src/freezer_mcu/include/Sequencer.h) speaks of `Timer1` twice and `runDueBoundaries()` of a wrap "32.8 ms later", both the Nano 3's: GPT1 wraps after about 89 s. [`ControllerStatus.msg`](../src/freezer_msgs/msg/ControllerStatus.msg) line 11 gives `/dev/ttyUSB0` as an example port. The module's `CLAUDE.md` asks for the MIT header; all 39 C++ source files and the 3 `package.xml` carry BSD-3-Clause, as the `LICENSE` does. `freezer.yaml` calls `timeout` "for one answer" (see [Asking Again](#asking-again-and-dropping-late-replies)). **Safety: no.**
12. **The fake differs from the firmware in small ways.** `FakeDriver::reset()` keeps the latches the shot had planned: after a reset 1 ms into a shot, `outputs()` at 7 ms still read `0xF0`, the shot's second step, where the Nano switches everything off; verified. `get_info()` reports version 2.0.0 (line 58); `worst_lateness_us_` is not cleared when a shot starts, so a status during a shot reports the previous shot's lateness; and no failure loses a reply other than a status one. From reading, except the first. **Safety: no**, but a test of a reset on the fake does not see the outputs go off.

No security issue was found. The node opens only the configured port, and the board page reaches the node only through rosbridge, which is open to the network on port 9092, as the launch file intends.

## Recommendations

Ordered by value against effort; each fits one pull request.

1. **Resynchronise the host's reader** (findings 1, 10). In `DefaultFramedSerial::read()`, skip bytes up to a `0x7E` in `Waiting` instead of throwing, and treat a delimiter after fewer than 3 bytes as the start of a new frame, as the firmware does. Add the probe of finding 1 to the tests of `framed-serial`. Trade-off: it changes the shared submodule, so StepIt Motors moves to the same commit; a reader that skips garbage needs a bound, e.g. the timeout, to still fail on a silent port.
2. **Ask the controller after a `Shoot` that got no reply** (finding 2). In `ShotRunner::run()`, when `shoot()` throws, query the status: running, or idle with a last shot id that moved past the one known before, means the shot started, and the runner follows it as usual. The runner needs the last id before the shot, from one status query or from the node's `known_shot_id_`. Trade-off: one more query before every shot, or a dependency of the runner on the node's id; a test with a fake that loses the `Shoot` reply, as the probe did.
3. **Point `rig.yaml` at the R4** (finding 3). In StepIt Macro, the Freezer's `usb_port` as `/dev/serial/by-id/<the Nano R4's id>`, which `ls /dev/serial/by-id` shows on the rig's computer, and drop `baudrate`. Trade-off: none; it is the rig's file.
4. **Write a frame at once in the firmware** (point 1, finding 6). In `SerialPort::update()`, pass every byte of the write buffer that `Serial.availableForWrite()` allows in one `Serial.write(buffer, size)`, and never more than that, so `loop()` never waits on the FIFO. A reply then leaves in one packet, and a full FIFO no longer stalls `loop()`. Trade-off: the copy in StepIt Motors diverges further, unless both move together; check on the board, firmware 2.1.2.
5. **Drop stale bytes when the port opens** (finding 4). In `updateHost()`, call `tud_cdc_write_clear()` and `tud_cdc_read_flush()` when DTR rises, with the USB interrupt masked; and in `DefaultDriver::connect()`, check the length and shape of an `Info` reply before reading its name, retrying on a frame that is not one. Trade-off: two lines of firmware to check on the board; the host half alone already removes the false refusal.
6. **Follow the trigger outside the node** (separation of concerns, finding 5). Move the logic of `watch()` into a `TriggerWatcher` of `freezer_driver`, with an injected clock as `ShotRunner` has, and let `ShotRunner` end a shot when it sees another id running. Trade-off: moves about 120 lines, and the node tests of the trigger move with them.
7. **Make the fake fail like the board** (finding 12, testability). Switch the outputs off in `FakeDriver::reset()`, report the firmware's version, clear the lateness at the start of a shot, and add a lost reply per command, at least for `Shoot` and `SetOutputs`. Trade-off: small; the node tests of a reset may need updating.
8. **Smaller fixes in one pass** (findings 7, 8, 9, 11). Publish `~/outputs` after a `SetOutputs` that timed out only once a status confirms it, or say in the reply that the outputs are unknown; close the port before clearing `connected_` in `disconnect()`, under the lock `reconnect_loop()` waits on; send the late `Stop` after releasing `start_mutex_`; correct the comments, the message's example port and the licence named in `CLAUDE.md`. Trade-off: none worth a design decision.
