# Slipstream 5000 source port

<img width="2612" height="1556" alt="image" src="https://github.com/user-attachments/assets/c5f51e6a-1e89-42c9-89f0-beca4cacfb1d" />


A native C port of Slipstream 5000, using SDL3 for windowing, input, and audio.

Code is reverse engineered from the DOS binary and should produce bit identical results.
It also includes a high-res mode with GPU rendering. Pressing F12 at any time switches
old graphics and GPU mode.

## Game data

You need the original game's data files; they are not included in this repository.
Steam installations are detected automatically. On Windows, GOG installations
are detected too. If no installation is found, a file picker lets you select
`SLIPSTRM.RES`. The selection is remembered in the application's preferences
folder (AppData on Windows). You can also pass its path directly:

```sh
./build/slipstream5000 /path/to/game/SLIPSTRM.RES
```

On Windows, run `build\slipstream5000.exe` instead. You can also set the
`SLIPSTREAM5000_RES` environment variable to the resource file's path.

Saved games are stored in the same preferences folder. On first launch, an
existing `SLIPSTRM.SAV` beside the game data is copied there if no save file
already exists in the preferences folder. The original file is kept.

## Building

Requirements: a C11 compiler, Meson 1.3 or newer, Ninja, and SDL3 3.4 or newer.
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
game controllers are supported. The game starts in borderless fullscreen on
first launch; Alt+Enter toggles windowed mode. The display mode and windowed
size are remembered in the preferences folder. The window can be resized.

Serial, modem, and IPX multiplayer are unavailable and their menu entries are
disabled. Local split-screen multiplayer is supported.

High Res in Configuration > General (or F12 during a race) draws the race at
the native window resolution with SDL3 GPU. Textures use nearest filtering
and CPU-generated mipmaps from stb_image_resize2; menus retain the DOS art
and software renderer.

## Development

Format `src/port`, including GPU shaders, with Clang-format 22.1.3 using the
checked-in `.clang-format`. Format `meson.build` and `meson.options` with
`meson format` (Meson 1.11.0). Shader regeneration also runs Clang-format;
use `build_shaders.py --clang-format PATH` to select the executable.
GitHub Actions build debug and release configurations on Windows, Linux, and
macOS, and check formatting.

## License

The port is available under the [MIT license](LICENSE). Vendored Opal retains
its own [licenses](src/opal/LICENSE) and attribution. stb_image_resize2 retains
its [public-domain/MIT licensing](src/third_party/stb_image_resize2.h). The original game and its
data remain the property of their respective owners.
