# Report the Missed Replies

Branch `report-missed-replies`, on top of `usb-task-in-the-interrupt-only`.

## Goal

[#18](018-tolerate-a-lost-reply.md) asks again when a status query goes unanswered, so one lost reply no longer fails a stack. But it turned a loud failure into a line in the log: the fault of firmware 2.1.0 was found only because of that line. A healthy controller misses no reply, so a miss must stay visible, not only in the log.

## Decisions

**The count travels with the shot.** `missed_status` is in the result of `Shoot` and on `~/shots` when the shot is over, ended, stopped or failed. A client sees which shot missed replies.

**And a count since the node started, on `~/status`.** `ControllerStatus` carries `missed_status`, the sum over every shot of the node. Every page that shows the Freezer's status sees the same, as StepIt Macro wants: the count lives on the node, not in a browser.

**Every miss counts.** A shot that failed after `max_missed_status` misses in a row adds its misses too: whether asking again saved the shot or not, the controller missed them.

**`uint32` everywhere.** A shot polls every 10 ms, so a long shot on a bad link could pass 65535 misses; one type for the shot and the total avoids a count that wraps.

**The status is published at once.** The thread of the shot publishes `~/status` when the count grows, as the other changes of the status are published when they happen, besides every second.

**A fake that loses a reply.** `FakeDriver::Failure::LoseStatusReply` throws at the first status query of each shot, of a goal or of IN1, as a timeout does. The node test checks the result, `~/shots` and `~/status`.

## Left Out

**A limit on the misses of a stack.** A stack could fail once its misses add up. The count is reported instead: the client decides.

**Masking the USB interrupt around every call of `loop()` into TinyUSB.** The review found that `Serial.read()` and `Serial.write()` still reach TinyUSB's endpoint claims, which the USB interrupt runs too, with no lock without an RTOS. With the buffers of the Nano R4, 256 bytes of FIFO against 64 of endpoint, the OUT endpoint stays armed and the interrupt re-arms it itself, and the IN endpoint is only flushed by the interrupt when a transfer ends, so the two sides do not meet with our traffic. 0 misses in about 52000 queries with 2.1.1 agree. The misses on `~/status` would show it if they did.
