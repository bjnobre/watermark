# whatermak

`whatermak` displays a transparent, click-through image watermark above normal
windows in Sway and other wlroots-based Wayland compositors. It uses
`wlr-layer-shell` rather than XWayland window-management tricks.

## Features

- Native Wayland overlay
- Transparent PNG and other GdkPixbuf-supported image formats
- Click-through: it does not intercept pointer or keyboard input
- One watermark per output by default
- Configurable output, position, width, margin, and opacity
- No reserved screen space

The overlay intentionally does not bypass secure lock-screen layers.

## Requirements

- GTK 3.24 or later
- gtk-layer-shell 0.7 or later
- Meson and Ninja
- A compositor implementing `wlr-layer-shell`, such as Sway

On Debian or Ubuntu, the build dependencies are typically:

```sh
sudo apt install build-essential meson libgtk-3-dev libgtk-layer-shell-dev
```

On Fedora:

```sh
sudo dnf install gcc meson gtk3-devel gtk-layer-shell-devel
```

On Arch Linux:

```sh
sudo pacman -S base-devel meson gtk3 gtk-layer-shell
```

## Build and install

```sh
meson setup build
meson compile -C build
sudo meson install -C build
```

For a user-local installation:

```sh
meson setup build --prefix="$HOME/.local"
meson compile -C build
meson install -C build
```

## Usage

```sh
whatermak [OPTIONS] IMAGE
```

For example:

```sh
whatermak --position=bottom-right --width=240 --opacity=0.22 logo.png
```

Run `whatermak --help` for every option.

To start it with Sway, add this to `~/.config/sway/config`:

```text
exec whatermak --position=bottom-right --opacity=0.22 /absolute/path/logo.png
```

Use an absolute image path because the process is started from Sway's working
directory. Reloading the Sway configuration repeatedly may start duplicate
instances; restart the existing process when changing its arguments.

## License

Copyright (C) 2026 whatermak contributors.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, version 3 of the License.

See [LICENSE](LICENSE) for the license notice and the Free Software Foundation's
authoritative license text.
