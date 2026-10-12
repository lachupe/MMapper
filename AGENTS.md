# AGENTS.md

Shared instructions for Codex, Claude and other coding agents. `CLAUDE.md` imports
this file; tools without automatic instruction loading must read it explicitly.

## This fork and the mume3d boundary

- This Qt/C++ MMapper fork publishes the frontend protocol consumed by the sibling
  mume3d repository. Check the actual branch and dirty paths before work; the main
  development branch has been `db/frontend-protocol-impl`.
- MMapper owns the integer grid and room graph. Float layouts, local spaces and all
  3D behavior belong in mume3d; publish derived hints rather than changing that split.
- MUME parsing belongs here and reaches the client as `MMapper.*` packages. New
  packages need a client consumer, with a labelled stub when its use comes later.
- The protocol contract is tracked in the mume3d repository at
  `docs/specs/mmapper_frontend_protocol_spec.md`. Locate that sibling checkout before
  editing the contract; the old parent-directory spec path is only a local symlink.
- Read `BUILD.md`; use Ninja/Debug in `build/` and the Linux/ccache rules below.
  Run Qt unit tests with `QT_QPA_PLATFORM=offscreen`; never launch the GUI from a
  session. Diagnose failures against current inputs; old machine-specific failure
  notes are not test waivers.
- Keep upstream map/storage/display behavior intact unless the task explicitly
  changes it. The unmerged integer reshaper and abandoned float-layout experiment
  are historical work, not instructions to merge them.

## Work, handoff and integration

- Keep instructions provider independent. For substantial cross-repository work,
  use the task/acceptance/handoff format in mume3d's `docs/agent-workflow.md` and
  `docs/tasks/TEMPLATE.md`. Preserve task IDs and record both repositories' SHAs.
- A provider switch resumes from Git and a compact task record, not private memory
  or the previous chat. Record scope, owned paths, next action and validation/logs.
- Check `git status`, branch and worktrees. Use an isolated branch/worktree alongside
  active work; never stash/reset/switch a shared checkout or remove another worker's
  worktree/lock. Stage named files only, never `git add -A`, and do not finish another
  session's merge. Check the commit email: `lachupe@gmail.com`.
- Only the designated coordinator/integrator lands or pushes the development branch
  (the user's 2026-10-09 rule covering both repositories). Workers commit/push their
  own branches and hand off branch/SHA plus build/test evidence. The integrator role
  may use either provider; no permanent coordinator conversation is required.
- Changes here also need an outcome/reason in mume3d's integrated changelog. Workers
  put proposed text and unverified parts in their commit/task; only the integrator
  edits `CHANGELOG.md`. Do not apply mume3d's Godot quick gate as a C++ build check.

## Mandatory Build Guidelines

1.  **Environment:** All builds **must** be performed on a **Linux** system.
2.  **Code Style:** All commits **must** strictly adhere to the project's `clang-format` configuration.

---

## Speed Optimization: ccache (Mandatory)

To enable incremental, cached builds and reduce compilation time, `ccache` is **required**.

1.  **Install:** Ensure `ccache` is installed on Linux.
    * *Example:* `sudo apt-get install -y ccache`
2.  **CMake Configuration:** The initial `cmake` command **must** use the Debug build type and include the ccache launcher flags:

    ```bash
    cmake -B build/ \
          -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
          -DCMAKE_C_COMPILER_LAUNCHER=ccache \
          ... [Other flags from BUILD.md]
    ```

---

## Code Style Application (Mandatory)

We use `clang-format-18` via Docker to ensure style consistency. Use the following command to automatically format your code.

1. **Directory:** Navigate to the Git root directory.

2. **Execution:** Run this command to perform an in-place edit to all source files with the required style:

    ```bash
    docker run --rm --platform linux/amd64 \
         -v "$(pwd):$(pwd)" -w "$(pwd)" \
         ghcr.io/jidicula/clang-format:18 \
         -i --Werror --style=file \
         $(find src tests -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' -o -iname '*.hpp')
    ```
