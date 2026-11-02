# TinyUSB's Task in the Interrupt Only

Branch `usb-task-in-the-interrupt-only`.

## Goal

On the rig, the Nano R4 left about one status query in 2000 unanswered during a shot: no reply, no damaged frame, no USB error. One lost reply failed a whole focus stack until [#18](018-tolerate-a-lost-reply.md) asked again. The old Nano 3 of the board never lost one.

## The Cause

The Arduino core of the Nano R4 runs TinyUSB's `tud_task()` in the USB interrupt. `Serial`'s `operator bool` runs it too, and `updateHost()` called `if (Serial)` at every pass of `loop()`, to see DTR rise. `tud_task()` is not reentrant: a USB interrupt that came while `loop()` was inside it lost a request or a reply, now and then. The Nano 3 had an FT232 between USB and the microcontroller, and nothing of the kind.

## Decisions

**DTR is read from TinyUSB.** `updateHost()` calls `tud_cdc_connected()`, which only reads the line state. `tud_task()` runs in the interrupt only, as the core intends.

**Not `Serial.dtr()`.** It reads the same state, but sets `ignore_dtr`, after which `Serial.write()` sends even with the port closed.

**Version 2.1.1.** The protocol is the same: the driver accepts any 2.x.

## Tested

On the rig, with a throwaway program that uses `DefaultDriver`, status queries every 10 ms, the motors' power off:

| Test | 2.1.0 | 2.1.1 |
|---|---|---|
| 30000 status queries, no shot | 4 unanswered | 0 |
| 200 shots switching no output, about 22000 queries | 12 shots with a miss | 0 |

The misses came with nothing switching on the board, so they were not noise from the camera or the lights.

Then 150 `TakeShot` objectives with the camera and the lights, through the commander: all succeeded with their picture, and the 111 that the log still held missed no reply.

## Left Out

Found while reading the firmware, not changed here:

- `SerialPort::update()` sends one byte per pass of `loop()`, each in a USB packet of its own.
- `Serial.write()` waits while TinyUSB's buffer is full: a host that keeps the port open and stops reading would stall `loop()`, and with it the replies and IN1. A running shot is not affected: the timer runs it.
- A request with a bad CRC is dropped without a reply: the host learns it only at its timeout.
- `MIN_HOLD_US` is still the Nano 3's.
