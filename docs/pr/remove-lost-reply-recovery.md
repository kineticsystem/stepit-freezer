# Remove the Recovery of Lost Replies

Branch `remove-lost-reply-recovery`, on top of `usb-task-in-the-interrupt-only`.

## Goal

Firmware 2.1.1 fixes the cause of the lost status replies. The recovery built around them in [#18](018-tolerate-a-lost-reply.md), asking again and dropping late replies, is no longer needed, and it only covered some of the ways a link fails. Keep the code simple: if replies go missing again, a new test will find why.

## Decisions

**#18 is reverted whole.** `ShotRunner`, `DefaultDriver`, the parameter `max_missed_status` and their tests are as before #18. Its note stays, as every note of a merged pull request does.
