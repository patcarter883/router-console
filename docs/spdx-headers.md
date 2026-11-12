# SPDX headers

Every first-party source file (`*.cpp`, `*.h`, `*.qml`) starts with these two
lines and nothing else above them:

    // SPDX-FileCopyrightText: 2026 the router-console authors
    // SPDX-License-Identifier: AGPL-3.0-or-later

Rules:

- Same order, same wording, same year, everywhere. `//` is a comment in every
  file type the project ships, so C++, headers and QML all carry the identical
  text.
- Applied to first-party files only. A vendored or generated file keeps its own
  licence header (none are vendored today) and is listed in this file when one
  is added.
- `LICENSE` is the full AGPL-3.0 text; the SPDX tag is the machine-readable
  form of the same statement.

Files covered: everything under `src/` and `tests/` (see
`git ls-files 'src/*.cpp' 'src/*.h' 'src/*.qml' 'tests/*.cpp'`).
