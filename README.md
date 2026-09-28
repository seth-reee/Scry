<h1><img src="Scry.png" width="56" alt="Scry icon" align="absmiddle"> Scry</h1>

A lightweight STL and 3MF model previewer for [Omarchy](https://omarchy.org/).

## Features

- Preview binary and ASCII STL files and 3MF models
- Rotate, zoom, and reset the view with the mouse and keyboard
- Open models from the file picker, drag and drop, or the command line
- Follow the active Omarchy colors for the mesh and application styling
- Read-only viewing: models are never modified

## Installation

Download the package for your architecture from [Releases](https://github.com/seth-reee/Scry/releases), then install it with:

```bash
sudo pacman -U scry-0.1.2-1-*.pkg.tar.zst
```

The package recipe supports Omarchy on x86_64 and ARM64, using Arch Linux and Arch Linux ARM respectively. Choose the package whose filename matches your machine's architecture. ARM64 packages are built under QEMU and need a real ARM64 desktop check.

After installation, launch **Scry** from the application menu or run:

```bash
scry path/to/model.stl
```

## Usage

Drag with the left mouse button to rotate, use the wheel to zoom, and press **Home** to reset the view. Use **Open** or **Ctrl+O** to choose a file, or drag an STL or 3MF file into the window.

Scry displays geometry only. It does not show textures, materials, or printer settings. Files above 300 MB, 3MF model XML above 128 MB, and models above 3 million triangles are rejected.

## Build from source

Install `base-devel`, `cmake`, `ninja`, `pkgconf`, `qt6-base`, and `libzip`, then run:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/scry path/to/model.stl
```

To build an Arch package from the release source archive, place `scry-0.1.2.tar.gz` in `packaging/` and run `makepkg -s` there.

## Graphics troubleshooting

Scry needs desktop OpenGL 3.3 or newer and a working GPU driver. The Arch packages include Qt's Wayland plugin. Install the driver recommended by your distribution for your GPU, including NVIDIA's userspace OpenGL libraries when using its proprietary driver.

If the graphics driver cannot create a context, Scry displays an error. On Wayland, **Try XWayland** restarts Scry through the X11 backend when XWayland is available. You can also launch it from a terminal:

```bash
QT_QPA_PLATFORM=xcb scry path/to/model.stl
```

Collect platform and GPU information, or check whether a particular model renders:

```bash
scry --diagnostics
scry --diagnostics path/to/model.stl
```

When reporting a graphics issue, include this output, your GPU driver version, and whether it occurs on Wayland, X11, or both. A model diagnostic exits with status 0 after a successful render, 2 for unavailable OpenGL, or 3 for a file or rendering error.

## License

Scry is made by [seth-reee](https://github.com/seth-reee) and is available under the [MIT License](LICENSE).
