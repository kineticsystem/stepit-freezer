# Tolerate a Lost Reply

Pull request [#18](https://github.com/kineticsystem/stepit-freezer/pull/18), branch `tolerate-a-lost-reply`.

## Goal

On the rig of StepIt Macro, a focus stack failed at its 46th shot with `Shot failed: SerialException: timeout`. The shot itself fired and the camera took its picture. One status query, about 1.0 s into the shot, got no reply within the 0.2 s of `timeout`; the controller stayed connected and answered the next ones. One lost reply out of about 5000 failed a whole stack.

## Decisions

**A shot survives lost status replies.** `ShotRunner` asks again when a status query throws, and gives up after `max_missed_status`, 3, in a row. The shot runs on the controller whatever the host hears, so a lost reply is no reason to fail it. The deadline still applies, at the next reply.

**A late reply is dropped.** A reply carries no id of its request. A reply that comes after its request timed out would be read as the answer to the next one, and every reply after it would be one behind. After a failed read, `DefaultDriver` reads and drops what is left before its next request, until a read times out. It costs one `timeout` after a lost reply, never otherwise.

**Not on connect.** Each attempt of the handshake starts clean, so a booting controller does not slow it down.

**A warning for every shot that missed a reply.** The node logs how many status queries went unanswered, so that a rig losing replies often is seen before it fails.

**Why a reply is lost is still open.** The Pi saw no USB event and no CRC error, and the firmware drops a damaged request without a reply. On the rig, the lost query came as the board released the camera, 0.2 s after it switched the lights off: noise from the lights is the first suspect.

## Tested

- `ShotRunner`: two replies lost in a row, then one more, and the shot succeeds with 3 missed; the last reply lost; three in a row fail the shot.
- `DefaultDriver`: a late reply is dropped and the next request reads its own; a lost reply costs the next request one read.
- All 61 tests of `freezer_driver` and 22 of `freezer_node` pass.
