# Caffeine8

## Overview

Caffeine8 is a Linux utility designed to prevent the screen saver from activating when you're away from your computer. It uses the X11 window system to display a simple UI and simulates user activity using the `qdbus` command.

## Features

- Prevents screen saver activation
- Displays a simple UI with version information, PID, and error logs
- Supports command-line arguments for starting, stopping, and attaching to existing instances

## Requirements

- Linux with X11 window system
- `qdbus` command-line utility
- Magick++ library

## Installation

To build and install Caffeine8, you'll need CMake and a C++ compiler. Follow these steps:

```bash
$ git clone https://github.com/miland3r/caffeine8.git
$ cd caffeine8
$ mkdir build
$ cd build
```

By default, the executable will be installed to `/usr/local/bin` and the assets to `/usr/local/share/caffeine8`. If you want to specify a custom installation path, you can set the `CMAKE_INSTALL_PREFIX` variable:

```bash
$ cmake -DCMAKE_INSTALL_PREFIX=/your/custom/path ..
```

Otherwise, simply run:

```bash
$ cmake ..
```

Then compile and install:

```bash
$ make
$ sudo make install
```

For example, if you set `/your/custom/path` to `/opt/caffeine8`, the executable will be installed to `/opt/caffeine8/bin` and the assets to `/opt/caffeine8/share/caffeine`.

## Usage

To start a new instance:

```bash
$ caffeine8 start
```

To stop a running instance:

```bash
$ caffeine8 stop
```

To attach to an existing instance and show the UI:

```bash
$ caffeine8 attach
```

## License

This project is licensed under the GNU General Public License v3.0. See the [LICENSE](LICENSE) file for details.

---

Feel free to modify this README to better suit your project's specific needs.
