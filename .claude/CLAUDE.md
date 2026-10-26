# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Repository Is

StepIt Freezer fires cameras and flashes from ROS 2 (Jazzy) through the Freezer board: an Arduino Nano R4 driving two 74HC595 shift registers and 16 optocouplers, wired to 8 output jacks and 1 input jack. The host loads a table of steps and fires it; the Nano runs it on a hardware timer. It runs with a fake controller by default. How it is built, and why: [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md); the ideas weighed on the way: [docs/design/Brainstorming.md](../docs/design/Brainstorming.md).

## Working Environment

Everything is built, tested and run **inside the Docker container**, never on the host. The repo is mounted at `~/ws`; the scripts of `bin/` are on the `PATH` and aliased:

```bash
./docker/dock.sh stepit-freezer build   # the image and the container
./docker/dock.sh stepit-freezer start   # a shell in it
update      # rosdep install, and pnpm install in web/
build       # colcon build, then the board page into web/dist
test        # the page's typecheck + vitest, then colcon test + test-result
ros2 launch freezer_node freezer.launch.py                                  # fake controller, board page, rosbridge
ros2 launch freezer_node freezer.launch.py use_fake:=false usb_port:=/dev/ttyACM0
```

One test package: `colcon test --packages-select freezer_driver --return-code-on-test-failure`. The firmware is a PlatformIO project for the Nano R4, `src/freezer_mcu`, not built by colcon: `pio run --target upload --upload-port /dev/ttyACM0` flashes it over DFU, which needs the udev rule `src/freezer_mcu/udev/60-arduino-nano-r4.rules` on a host without a desktop. The Nano 3 the board had before is no longer supported; its firmware is at the tag `nano3-2.0.0`. The submodules `serial` and `framed-serial` must be present: `git submodule update --init --recursive`.

## Architecture

It follows one rule: **once a shot starts, its timing belongs to the controller, and only a stop interrupts it**.

| Package | Rule |
|---|---|
| `freezer_msgs` | The `Shoot` action, the messages and the `SetOutputs` service. No code. |
| `freezer_driver` | The host side of the protocol, with **no ROS interface**: `Driver` and its implementations (`DefaultDriver`, `FakeDriver`, `SynchronizedDriver`), the sequence and its rules, the recipes, `ShotRunner`. Its tests are in its `tests` folder, with gmock. |
| `freezer_node` | The node `freezer`, its parameters, its launch file (which also starts the board page and its rosbridge), and its test. |
| `freezer_mcu` | The firmware of the Nano R4. |
| `web` | The board page, React and TypeScript with Vite and pnpm, not a ROS package (`web/COLCON_IGNORE`). |
| `modules/framed-serial`, `modules/serial` | The framed, CRC-checked protocol and the serial port. `framed-serial` is shared with StepIt Motors; a rig that builds both needs them at the same commit. |

Easy to break, see **Safety Nets** in the architecture document:

- **A shot ends with every output off**: the controller and the host both refuse a table whose last step is not `0x0000`.
- **A table is checked whole before anything changes.**
- **The shot owns the outputs**: nothing changes them while it runs. A shot runs to its end; the host's cancel works only before it starts, and `Stop` is the one deliberate exception.
- **The handshake** refuses the wrong device and a different major version of the firmware: a change to the protocol changes the version on both sides together.
- **No watchdog, by design**: the outputs stay as set when the host goes silent.
- **Opening the port starts the controller again**: every output off, no table, no shot ids. The Nano R4 does it in `updateHost()` when DTR rises, without a reset, which would drop its USB port.

## Pull Request Notes

Each pull request has a notes file in [docs/pr/](../docs/pr/README.md): what we discussed, what we chose, and why. One file per pull request, written while the branch is open. Keep them simple: short sentences, one idea per paragraph, and a Mermaid diagram when it explains faster than words.

## Architecture and Review Documents

For the `architect` and `reviewer` skills: what they cannot find out alone.

- `docs/ARCHITECTURE.md` follows the rule above and describes firmware 2.1.0 and protocol version 2. Its sections, in order: Introduction, The Big Picture, The Packages, The Board, The Protocol, The Controller, The Remote Trigger, The Host, A Shot from the Host, Stopping a Shot, Losing the Controller, Seeing the Board, Safety Nets, Tests, Design Decisions and Trade-offs.
- **The review is a separate document, `docs/Review.md`**, opened with the date and the commit reviewed; `docs/ARCHITECTURE.md` describes the design and holds no review. Say whether an issue is a safety issue for a camera or a light: an output left on, a shot cut short, a table fired that was not checked.
- The safety nets, and what is deliberately not there, are design: judge them, do not report them as bugs. Do not review `serial`, which is third-party.
- **Probes run in the container, never on the host.** A throwaway test is a gmock test in `src/freezer_driver/tests/`, added with `ament_add_gmock()` to its `CMakeLists.txt`, and run with `colcon test --packages-select freezer_driver --return-code-on-test-failure`; for the page, a vitest test in `web/tests/`. The firmware has no automated test: what depends on it is checked on a Nano, or marked "from reading".

## CI

`.github/workflows`: `industrial_ci.yml` builds and tests, `ci-format.yml` runs pre-commit without the ament hooks, `ci-ros-lint.yml` runs those per package, `ci-web.yml` typechecks, tests and builds the board page.

## Conventions

- Every source file carries the MIT copyright header (`ament_copyright` enforces it).
- C++23, compiled with `-Werror -Wall -Wextra -Wpedantic -Wshadow -Wconversion`; `cpplint` with `--linelength=121`; `clang-format` and `clang-tidy` with the repo's files.
- Update the README and `docs/ARCHITECTURE.md` when the interface, the protocol or a safety net changes.
- Diagrams follow the `beautiful-mermaid-diagrams` skill. Each diagram of a document is an image: its source is `docs/diagrams/<name>.mmd`, rendered by `bin/render-diagrams.sh` into an SVG beside it, outside the container (it needs Node.js and Chrome); commit both. `bin/check-diagrams.sh`, run by `bin/test.sh` and CI, fails when an SVG is older than its source. A note of `docs/pr/` may keep its diagram as a Mermaid block, in the same style.
