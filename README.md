# pointerless

`pointerless` is a utility to move the mouse pointer with the keyboard on
Wayland. It renders a full-screen overlay on top of the compositor, lets you
pick a target area by typing a short label, and then drives a virtual pointer
to that target.

See the [Supported compositors](#supported-compositors) section for
compatibility.

## How it works

`pointerless` connects to the compositor, creates a full-screen overlay
(a `wlr-layer-shell` surface in the overlay layer) with keyboard interactivity,
and then collects a set of *selectable areas*. Each area is rendered as a
labeled region, and you pick a target by typing:

1. **Areas** &mdash; the regions you can point at. The screen is captured
   with
   [`wlr-screencopy-unstable-v1`](https://wayland.app/protocols/wlr-screencopy-unstable-v1)
   (supported only by some compositors &mdash; see [Supported
   compositors](#supported-compositors)) and a YOLO GUI detector &mdash; the
   [Salesforce GPA-GUI-Detector](https://huggingface.co/salesforce/GPA-GUI-Detector)
   YOLOv8 model running via [NCNN](https://github.com/Tencent/ncnn) &mdash;
   finds UI elements (icons, buttons, input fields) which become the areas.
2. **Labels** &mdash; each area gets a label from a fixed alphabet
   (`general.label_symbols`, `a`&ndash;`z` by default). With 26 areas or
   fewer a single character is enough; with more areas the labels grow to two
   or more characters (base-26 counting, like a spreadsheet column).
3. **Selection** &mdash; type the label characters of the area you want. The
   overlay highlights the matching area as you type; `Backspace` removes the
   last character and widens the match again; `Escape` cancels and exits.
4. **Action** &mdash; once the full label is entered, `pointerless` moves a
   virtual pointer ([`wlr-virtual-pointer-unstable-v1`](https://wayland.app/protocols/wlr-virtual-pointer-unstable-v1))
   to the center of the selected area. The default action is a move only; use
   `--drag` to perform a click-and-drag between two areas (see below).

The selection is printed to standard output as `WxH+X+Y +OX+OY C` &mdash; the
selected area, the offset of its output in the global coordinate space, and
the click type (`l` left, `m` middle, `r` right, `d` drag, `n` no click).
With `--only-print` the pointer is not moved at all &mdash; useful on
compositors without virtual-pointer support.

The selection keys default to the home row of your keyboard layout
(`asdfjklmghb` on QWERTY) and can be overridden with `general.home_row_keys`.

### Detection performance

`detect` runs the YOLO detector once per trigger (when you enter detect
mode), not on every frame. On CPU via NCNN the bundled model loads in ~30 ms
(one-time) and each detection takes ~90 ms (p50) to ~95 ms (p95) on a
640&times;640 input, independent of screen resolution &mdash; so expect roughly a
tenth of a second of latency when a detect-mode run starts.

### Drag flag

The `--drag` flag (or `-d`) changes the action to a click-and-drag between two
selected positions. It requires **two selections**:

1. **Start**: the first selection's left-center is stored as the drag start
   point. A marker (shape, size and color configurable via the `mode_drag`
   section) is drawn at that position and the selection resets so you can pick
   again.
2. **End**: the second selection's right-center becomes the drag end point.
   The pointer then presses and holds the left button at the start, moves to
   the end, and releases.

This makes it useful for selecting text (start at word/line beginning, end at
word/line end) or dragging UI elements.

## Command line

```
pointerless [OPTION...]

 -h, --help          show this help
 --help-config       show help on configuration
 -v, --version       show version
 -c, --config=FILE   use given configuration file
 -r, --restrict=AREA restrict to given area (wxh+x+y)
 -o, --option        set configuration option
 -O, --output        specify display output to use
 -p, --only-print    only print, don't move the cursor or click
 -d, --drag          perform a click-and-drag between two selections
```

## Supported compositors

For `pointerless` to work, it requires the following protocols:
 - [`wlr-layer-shell-unstable-v1`](https://wayland.app/protocols/wlr-layer-shell-unstable-v1) for the program to display on top,
 - [`wlr-virtual-pointer-unstable-v1`](https://wayland.app/protocols/wlr-virtual-pointer-unstable-v1) to control the mouse pointer,
 - [`wlr-screencopy-unstable-v1`](https://wayland.app/protocols/wlr-screencopy-unstable-v1) to capture the screen for target detection.

Here are the compositors with which it has been tested:

| Compositor |     | Notes |
| ---------- | --- | ----- |
| [Sway](https://swaywm.org) | ✅ | - |
| [Hyprland](https://hyprland.org) | ✅ | - |
| [niri](https://github.com/YaLTeR/niri) | ✅ | - |
| [dwl](https://codeberg.org/dwl/dwl) | ✅ | - |
| [labwc](https://labwc.github.io) | ✅ | - |
| [Wayfire](https://wayfire.org) | ✅ | The pointer doesn't move to the right location with multiple display outputs. See [#56](https://github.com/moverest/pointerless/issues/56#issuecomment-3087922040). |
| [KWin](https://github.com/KDE/kwin) | ❗ | The compositor doesn't support the [`wlr-virtual-pointer-unstable-v1`](https://wayland.app/protocols/wlr-virtual-pointer-unstable-v1) and [`wlr-screencopy-unstable-v1`](https://wayland.app/protocols/wlr-screencopy-unstable-v1) protocols. It can still work with the `--only-print` option and the mouse pointer can then be moved with `ydotool` or similar. |
| [Mutter](https://mutter.gnome.org) | ❌ | The compositor doesn't support any of the required protocols. |

## Installation

### Arch Linux

If you are using Arch Linux, you can install the [`pointerless` AUR package](https://aur.archlinux.org/packages/pointerless).

Recommended way to build and install the package directly from the AUR (gets all required files):
```bash
git clone https://aur.archlinux.org/pointerless.git
cd pointerless
makepkg -si
```

Alternatively, if you only want the `PKGBUILD`:
```bash
curl -L 'https://aur.archlinux.org/cgit/aur.git/plain/PKGBUILD?h=pointerless' -o PKGBUILD
makepkg -si
```

### Nix

`pointerless` is not in nixpkgs. Build and install it from the project's
Nix flake (works on NixOS and any other distribution with Nix):

```bash
nix profile install github:knoopx/pointerless#pointerless
```

The flake also provides a development shell with all build dependencies
(wayland, cairo, xkbcommon, NCNN):

```bash
nix develop github:knoopx/pointerless
```

### Chimera Linux

If you are using Chimera Linux, you can install the [`pointerless` package](https://pkgs.chimera-linux.org/package/current/contrib/x86_64/pointerless) which is available in the [contrib repository](https://chimera-linux.org/docs/apk#repositories).

```bash
apk add chimera-repo-contrib
apk add pointerless
```
### Fedora

If you are using Fedora, you can install the [`pointerless` package](https://src.fedoraproject.org/rpms/pointerless) which is available in the official repository.

```bash
dnf in pointerless
```

### From sources

You can build from sources with:

```bash
meson setup build --buildtype=release
meson compile -C build
```

The [NCNN](https://github.com/Tencent/ncnn) library used by the GUI detector
is a required system dependency; the Nix devShell provided by `flake.nix`
ships the nixpkgs `ncnn` package, and on other systems install it from your
distribution's package manager. The model files are installed with the
package.

Then install with:

```bash
meson install -C build
```

## Setting the bindings

### Sway

```
mode Mouse {
    bindsym a mode default, exec 'pointerless-sway-active-win; swaymsg mode Mouse'
    bindsym Shift+a mode default, exec 'pointerless; swaymsg mode Mouse'

    # Mouse move
    bindsym h seat seat0 cursor move -15 0
    bindsym j seat seat0 cursor move 0 15
    bindsym k seat seat0 cursor move 0 -15
    bindsym l seat seat0 cursor move 15 0

    # Left button
    bindsym s seat seat0 cursor press button1
    bindsym --release s seat seat0 cursor release button1

    # Middle button
    bindsym d seat seat0 cursor press button2
    bindsym --release d seat seat0 cursor release button2

    # Right button
    bindsym f seat seat0 cursor press button3
    bindsym --release f seat seat0 cursor release button3

    bindsym Escape mode default
}

bindsym $mod+g exec pointerless-sway-active-win
bindsym $mod+Shift+g mode Mouse
```

### Hyprland

```
# Cursor submap (similar to the Mouse mode in Sway)
submap=cursor

# Jump cursor to a position
bind=,a,exec,hyprctl dispatch submap reset && pointerless && hyprctl dispatch submap cursor

# Cursor movement
binde=,j,exec,wlrctl pointer move 0 10
binde=,k,exec,wlrctl pointer move 0 -10
binde=,l,exec,wlrctl pointer move 10 0
binde=,h,exec,wlrctl pointer move -10 0

# Left button
bind=,s,exec,wlrctl pointer click left
# Middle button
bind=,d,exec,wlrctl pointer click middle
# Right button
bind=,f,exec,wlrctl pointer click right

# Scroll up and down
binde=,e,exec,wlrctl pointer scroll 10 0
binde=,r,exec,wlrctl pointer scroll -10 0

# Scroll left and right
binde=,t,exec,wlrctl pointer scroll 0 -10
binde=,g,exec,wlrctl pointer scroll 0 10

# Exit cursor submap
# If you do not use cursor timeout or cursor:hide_on_key_press, you can delete its respective calls.
bind=,escape,exec,hyprctl keyword cursor:inactive_timeout 3; hyprctl keyword cursor:hide_on_key_press true; hyprctl dispatch submap reset 

submap = reset

# Entrypoint
# If you do not use cursor timeout or cursor:hide_on_key_press, you can delete its respective calls.
bind=$mainMod,g,exec,hyprctl keyword cursor:inactive_timeout 0; hyprctl keyword cursor:hide_on_key_press false; hyprctl dispatch submap cursor
```

## Configuration

`pointerless` can be configured with a configuration file. The file is loaded
from `$XDG_CONFIG_HOME/pointerless/config` (or `$HOME/.config/pointerless/config`
when `XDG_CONFIG_HOME` is not set) unless a file is passed with `-c`. See
[`config.example`](./config.example) for an example and run
`pointerless --help-config` for help. Individual options can also be set on
the command line with `-o section.option=value`.

## Dependencies

- [`xkbcommon`](https://xkbcommon.org)
- [`cairo`](https://cairographics.org)
- [`wayland`](https://wayland.freedesktop.org)
- [`wayland-protocols`](https://gitlab.freedesktop.org/wayland/wayland-protocols)
- C++ compiler
- [`NCNN`](https://github.com/Tencent/ncnn) (GUI detector)


## License

[GPL-3.0-only](./LICENSE)

The sources also include Wayland protocol files under the [NTP license](./LICENSE-NTP).
