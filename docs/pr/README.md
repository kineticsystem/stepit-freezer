# Pull Request Notes

Each pull request has a notes file here, from #16 on; #20, closed without a merge, and #22 have none. It keeps what the pull request does not: what we discussed, what we chose, and why.

## The Notes

| Note | Pull request |
|---|---|
| [016-nano-r4.md](016-nano-r4.md) | [#16](https://github.com/kineticsystem/stepit-freezer/pull/16) The Arduino Nano R4 |
| [017-mermaid-diagrams.md](017-mermaid-diagrams.md) | [#17](https://github.com/kineticsystem/stepit-freezer/pull/17) Diagrams as SVG images, from Mermaid sources |
| [018-tolerate-a-lost-reply.md](018-tolerate-a-lost-reply.md) | [#18](https://github.com/kineticsystem/stepit-freezer/pull/18) Tolerate a lost reply |
| [019-usb-task-in-the-interrupt-only.md](019-usb-task-in-the-interrupt-only.md) | [#19](https://github.com/kineticsystem/stepit-freezer/pull/19) TinyUSB's task in the interrupt only |
| [021-remove-lost-reply-recovery.md](021-remove-lost-reply-recovery.md) | [#21](https://github.com/kineticsystem/stepit-freezer/pull/21) Remove the recovery of lost replies |
| [023-ros2-workspace.md](023-ros2-workspace.md) | [#23](https://github.com/kineticsystem/stepit-freezer/pull/23) Libraries from `freezer.repos`, not submodules |
| [024-architecture-review.md](024-architecture-review.md) | [#24](https://github.com/kineticsystem/stepit-freezer/pull/24) A second check of the architecture, and the review again |
| [025-readme-fixes.md](025-readme-fixes.md) | [#25](https://github.com/kineticsystem/stepit-freezer/pull/25) The READMEs, checked against the code |

## How to Write One

- One file per pull request: `<number>-<branch>.md`, e.g. `016-nano-r4.md`. Before it has a number, use the branch name.
- Sections: **Goal**, **Decisions**, **Left Out**, **Open Items**. Skip the ones that do not apply, and add one a note needs, e.g. **Tested**.
- Short sentences. One idea per paragraph. A diagram when it explains faster.
- Write it while the branch is open. Do not rewrite it after the merge.
