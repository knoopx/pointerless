# AGENTS.md

Guidance for AI agents working in this repository.

## Project

`pointerless` — a utility to move the mouse pointer with the keyboard on
Wayland. It renders a layer-shell selector on top of the compositor and
drives a virtual pointer. Written in C with a C++ NCNN detector.

## Build

Build system is **Meson + Ninja**. There is no flake in-tree; the canonical
toolchain comes from a Nix devShell (see `flake.nix`).

```bash
# From a Nix devShell (nix develop) providing meson, ninja, gcc/g++,
# wayland, libxkbcommon, cairo, ncnn:
meson setup build --buildtype=release
meson compile -C build
```

Debug build (adds `-DDEBUG`, suffixes the version with `-debug`):

```bash
meson setup builddbg --buildtype=debug
meson compile -C builddbg
```

Install:

```bash
meson install -C build
```

### Dependencies

- `wayland-client`, `wayland-protocols` (layer-shell, virtual-pointer,
  screencopy unstable protocols)
- `xkbcommon`
- `cairo`
- `libm`
- `ncnn` — from the system: the Nix devShell provides the nixpkgs `ncnn`
  package, which meson requires as a system dependency (no vendored copy).
- A C++ compiler (for `src/guidetect.cpp`).

### GUI detection model

The Salesforce GPA-GUI-Detector YOLOv8 model runs via NCNN. The model files
live in `share/pointerless/` (`model.ncnn.param`,
`model.ncnn.bin`) and are installed to `<datadir>/pointerless/`. The detector
loads them at runtime from the compile-time `GUIDETECT_MODEL_DIR`
(`<datadir>/pointerless`). Keep the `install_data` source paths in `meson.build`
pointing at `share/pointerless/` to match.

## Tests

```bash
meson test -C build
```

The only registered test is `test_label` (`src/test_label.c` + `src/label.c`).

## Layout

- `src/` — C/C++ sources. Entry point `src/main.c`. Detector `src/guidetect.cpp`.
- `protocol/` — Wayland protocol files (NTP license) compiled into the binary.
- `share/pointerless/` — NCNN model files (`model.ncnn.param`, `model.ncnn.bin`).
- `share/pointerless.desktop` — desktop entry installed to `share/applications`.
- `config.example` — example configuration file.
- `helpers/` — helper scripts.
- `smoke_test.cpp` — manual smoke test (not wired into meson).

## Conventions

- C standard is C11 (`c_std=c11`); C++ is enabled natively for the detector.
- `-D_GNU_SOURCE=200809L` and `-DVERSION` are added project-wide.
- The system NCNN installs its headers under `<prefix>/include/ncnn/` while
  pkg-config only exposes `<prefix>/include`, so `meson.build` must add the
  `ncnn/` include subdirectory explicitly.
- The model `.bin` is ~80 MiB; it is a build artifact and is excluded from VCS
  snapshots (jj `snapshot.max-new-file-size`).

## Notes for agents

- Do not commit VCS state; leave commits to the user.
- Verify a build by running the compiled binary or `meson test`, not just by
  a clean compile.
- If a dependency is missing in the shell, prefer the Nix devShell over
  hand-assembled `PKG_CONFIG_PATH`.
