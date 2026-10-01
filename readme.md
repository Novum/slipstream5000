# Slipstream 5000

A native C port of Slipstream 5000, using SDL3 for windowing, input, and audio.
The original software renderer and game logic are preserved. Opal provides
OPL synthesis for music.

Full championship runs have been compared against the DOS game with zero
pixel divergence.

## Game data

You need the original game's data files; they are not included in this repository.
Steam installations are detected automatically. On Windows, GOG installations
are detected too. If no installation is found, a file picker lets you select
`SLIPSTRM.RES`. You can also pass its path directly:

```sh
./build/slipstream5000 /path/to/game/SLIPSTRM.RES
```

On Windows, run `build\slipstream5000.exe` instead. You can also set the
`SLIPSTREAM5000_RES` environment variable to the resource file's path.

## Building

Requirements: a C11 compiler, Meson 1.3 or newer, Ninja, and SDL3 3.2 or newer.
Opal is included in `src/opal`.

With SDL3 installed and discoverable through pkg-config or CMake:

```sh
meson setup build --buildtype=release
meson compile -C build
```

For Windows, use an x64 Visual Studio developer terminal. With the SDL3
development SDK extracted to a directory containing `include` and `lib64`:

```powershell
meson setup build --buildtype=release -Dsdl3_dir=C:/Libraries/SDL3
meson compile -C build
build\slipstream5000.exe C:/Games/Slipstream5000/SLIPSTRM.RES
```

The Windows SDK build copies `SDL3.dll` beside the executable.

## Controls and support

Configure controls through the game's Configuration menu. Keyboard and SDL3
game controllers are supported. The window can be resized; Alt+Enter toggles
borderless fullscreen.

Serial, modem, and IPX multiplayer are unavailable and their menu entries are
disabled. Local split-screen multiplayer is supported.

## Development

Format `src/port` with Clang-format 22 using the checked-in `.clang-format`.
GitHub Actions build debug and release configurations on Windows, Linux, and
macOS, and check formatting.

## License

The port is available under the [MIT license](LICENSE). Vendored Opal retains
its own [licenses](src/opal/LICENSE) and attribution. The original game and its
data remain the property of their respective owners.
