# Drift Player

Drift Player is a cross-platform desktop media player that pairs a modern Qt 6 interface with the mpv playback engine. It focuses on an ambient viewing experience: blurred video backdrops, a curated playlist flow, and lightweight playback controls that stay out of the way while you watch or listen.

![Drift Player screenshot](docs/screenshot.png)


---

- [Features](#features)
- [Quick Start](#quick-start)
  - [Prerequisites](#prerequisites)
  - [Build](#build)
  - [Run](#run)
- [Usage Highlights](#usage-highlights)
- [Configuration & Persistence](#configuration--persistence)
- [Project Layout](#project-layout)
- [Contributing](#contributing)
- [License](#license)
- [Acknowledgements](#acknowledgements)

---

## Features

- **Playlist-first workflow** – add multiple audio or video tracks, shuffle or repeat, and jump seamlessly between entries.
- **Ambient visuals** – mpv video is rendered into a blurred OpenGL scene that keeps controls readable without obscuring the content.
- **Adaptive controls** – transport, progress, and playlist panels fade in on interaction, keeping the canvas uncluttered when idle.
- **Playback management** – seek with a scrubber, view elapsed/remaining time, toggle repeat modes, and adjust volume inline.
- **Background blur control** – fine-tune the visual intensity or let hardware decoding decide when blur should be disabled.
- **Stream-friendly** – paste a YouTube (or any yt-dlp supported) URL to play it instantly without a manual download.
- **Persistent settings** – volume, blur, shuffle, and repeat preferences are stored via `QSettings` so they survive restarts.

## Quick Start

### Prerequisites

Install the following dependencies before building Drift Player:

- **Qt 6** (modules: `Core`, `Gui`, `Widgets`, `OpenGLWidgets`)
- **mpv** with the `libmpv` development headers
- **CMake** 3.22 or newer
- A C++20-capable compiler (GCC 11+, Clang 12+, MSVC 19.3+)
- To stream online media (YouTube, Vimeo, etc.), ensure [`yt-dlp`](https://github.com/yt-dlp/yt-dlp) (or `youtube-dl`) is available on your PATH so mpv can resolve URLs.

On Debian/Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools libmpv-dev
```

On Arch Linux:

```bash
sudo pacman -S cmake qt6-base mpv
```

On macOS (Homebrew):

```bash
brew install cmake qt mpv
```

> Windows builds require the Qt and mpv development packages installed manually. Ensure `qmake` and `cmake` are available in your shell.

### Build

```bash
git clone https://github.com/hyperfield/drift-player.git
cd drift-player
cmake -S . -B build
cmake --build build
```

The configured target name is `drift_player`, and the generated executable is placed at `build/Drift Player` (or `build/Drift Player.exe` on Windows).

### Run

```bash
./build/Drift\ Player
```

The application creates a window titled _Drift Player_ and persists user settings under the organization `evoid`.

## Usage Highlights

- **Add Media** – click the `Add Media` button to select one or more files; Drift Player automatically normalises and deduplicates paths before queuing tracks.
- **Open URL** – choose `File → Open URL…` (or press <kbd>Ctrl</kbd>+<kbd>U</kbd>) to stream online media directly through mpv/yt-dlp without downloading the file first.
- **Playback controls** – play/pause, next, previous, repeat mode toggle, and shuffle are available in the central control bar.
- **Progress & seeking** – drag the position slider to scrub; the time label displays the current position and total duration.
- **Visual tweaks** – adjust the blur slider to change the strength of the background blur; the slider is disabled when hardware decoding already applies blur.
- **Fade interactions** – moving the cursor over playlists or controls temporarily raises their opacity; the panels fade after inactivity.

Keyboard shortcuts (default Qt behaviors) such as <kbd>Space</kbd> for play/pause also work.

## Configuration & Persistence

Drift Player stores preferences via `QSettings` (organization `evoid`, application `Drift Player`). Saved values include:

- `audio/volume`
- `playback/shuffle`
- `playback/repeat`
- `visual/blur`

Delete the relevant configuration file for a clean state (location is platform dependent; e.g. `~/.config/evoid/Drift Player.conf` on Linux).

## Project Layout

```
├── CMakeLists.txt          # Root CMake project definition
├── include/
│   ├── MainWindow.h        # Main window and UI controller
│   └── VideoBackgroundWidget.h # OpenGL-backed mpv renderer
├── src/
│   ├── CMakeLists.txt      # Target definition for drift_player
│   ├── main.cpp            # Application entry point
│   ├── MainWindow.cpp      # UI setup and application logic
│   └── VideoBackgroundWidget.cpp # Rendering & mpv integration
├── third_party/
│   └── spdlog/             # Header-only logging dependency
└── README.md               # You are here
```

## Contributing

1. Fork the repository and create a feature branch.
2. Follow the existing coding style (Qt/modern C++) and prefer concise logging via `spdlog`.
3. Add or update tests/scripts if you introduce new functionality that can be covered.
4. Submit a pull request describing the change and manual testing performed.

Bug reports and feature ideas are welcome—please include OS, reproduction steps, and any relevant logs.

## License

The project’s license has not yet been formally declared. Until a license is added, treat the sources as “all rights reserved.” If you plan to redistribute or build upon Drift Player, reach out to the project maintainers first.

## Acknowledgements

- [mpv](https://mpv.io/) for the playback engine and libmpv API.
- [Qt Project](https://www.qt.io/) for the cross-platform UI toolkit.
- [spdlog](https://github.com/gabime/spdlog) for high-performance logging.
- All early testers and contributors who helped shape the Drift Player experience.

---

_Enjoy the drift—feedback, issues, and pull requests are always appreciated._
