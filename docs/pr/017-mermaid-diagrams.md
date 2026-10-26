# Diagrams as SVG Images, from Mermaid Sources

Pull request [#17](https://github.com/kineticsystem/stepit-freezer/pull/17), branch `mermaid-diagrams`.

## Goal

Every diagram of the documents in one style, the one of the `beautiful-mermaid-diagrams` skill: colours by role, compact, readable in light and dark mode. The same change runs in every StepIt repository.

## Decisions

**An SVG image, not a Mermaid block.** GitHub stretches a Mermaid block to the width of the page, so a narrow diagram becomes very tall. An image shows at the width we give it. The source stays beside it, in `docs/diagrams/<name>.mmd`.

**A hash, not a render, in CI.** `bin/render-diagrams.sh` writes the SHA-256 of the source into the first line of the SVG. `bin/check-diagrams.sh` compares the two, so CI needs neither Node.js nor Chrome. Whoever changes a `.mmd` renders it and commits both.

**A shell script, not a test of colcon or vitest.** The same check runs in every StepIt repository, whatever its tests are written in.

**A final newline in each SVG.** The `end-of-file-fixer` hook of `ci-format.yml` would otherwise change every SVG.

**Short and narrow before faithful to the layout.** The shot from the host had 14 messages: it is two steps now. The big picture was 1,228 px tall: the board sits beside the host. Questions in a chain are rounded boxes, not diamonds, which grow with their text.

## Left Out

The notes of `docs/pr/` keep their Mermaid blocks: a note is not rewritten after its merge.
