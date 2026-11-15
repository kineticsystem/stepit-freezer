# The READMEs, Checked against the Code

Branch `readme-fixes`.

## Goal

Make the four README files say what the code does: `README.md`, `docs/hardware/README.md`, `docs/pr/README.md` and `docker/README.md`. Every statement was read against the repository; no code changes.

## Decisions

- **The README says what happens today.** The development server of the board page serves a page that does not connect, and `reconnect_period` 0 loses a controller that stops answering until the node restarts. The README says so in one sentence each, and [Review.md](../Review.md#implementation-issues) keeps the findings.
- **The lists are whole.** The fourth workflow, `ci-web.yml`, the `ControllerStatus` message, the topic `status`, the service `fake/press_trigger`, `SynchronizedDriver`, and the two ament linters that were missing from the commands to run without `act`.
- **The notes of #19 and #21 take their numbers.** They were merged under their branch names. `Review.md` and the note of #24 follow the new names.
- **The notes start at #16.** `docs/pr/README.md` says so; no note is written after the fact for the older pull requests, nor for #22.
- **`<container-name>`, not `[container-name]`.** `dock.sh` refuses to run without a name, and square brackets read as optional.

## Left Out

- **The fake controller answers as firmware 2.0.0**, the firmware is 2.1.1. No README gives the version of the fake, and the driver checks the major only.
- **`baudrate`.** The README says it is the speed of the firmware, which is true. `Review.md` says the Nano R4 ignores it; the README does not repeat it.
- **The default of `sequence_names`.** The table gives the one of `freezer.yaml`, two sequences; the node alone declares `flash_shot` only.

## Open Items

- The README quotes the error of `dfu-util` in two ways, with and without `found on devnum 2`. One of them is not word for word: copy it from a real upload that fails.
- `docs/hardware/README.md` says the Gerber files were exported on 31 July 2011; their names start with `20110704A`. Which date is right is for who made the board.
- The module's `CLAUDE.md` asks for the MIT header; the sources and `LICENSE` are BSD-3-Clause.
- The two ament linter commands added to the README are those of `ci-ros-lint.yml`; they were not run in the container for this pull request.
