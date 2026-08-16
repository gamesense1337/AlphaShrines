# AlphaShrines

![Visitors](https://visitor-badge.laobi.icu/badge?page_id=AlphaShrines) ![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white) ![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D4?logo=windows&logoColor=white) ![IDE](https://img.shields.io/badge/IDE-Visual%20Studio%202022-5C2D91?logo=visualstudio&logoColor=white) ![Target](https://img.shields.io/badge/Target-Cube%20World%20Steam-1B2838?logo=steam&logoColor=white)

AlphaShrines is a lightweight C++ mod for the Steam release of **Cube World** designed to recreate the classic Alpha respawn experience as accurately as possible.

In the Steam release, Shrine of Life activation primarily supports fast travel and death can return the player to a previously activated shrine. AlphaShrines changes the respawn selection path so a death is resolved against nearby Shrine of Life spawn records, recreating Cube World's Alpha behavior.

This project intentionally changes only death respawn selection. It does not remove, replace, or alter the base game's Shrine of Life activation and fast travel system.

## Features

- Restores proximity based Shrine of Life respawn selection.
- Preserves the Steam release's existing Shrine of Life activation and fast travel behavior.
- Tracks native Shrine of Life spawn records as the world loads instead of relying on fixed map coordinates.
- Uses the player's death location to select from shrines in the surrounding 3 x 3 world cell area.
- Validates the expected Steam executable function prologues before installing any hook.

## How It Works

This project hooks two internal Steam runtime routines:

1. The spawn record insertion path, where it identifies native `0x0C` Shrine of Life records emitted by the shrine generation caller and caches their world positions.

2. The respawn selection path, where it replaces the result with the locally selected Shrine of Life position.

The selection logic follows the Alpha reversal's logic: it examines shrine records in the player’s current 256 block cell and its eight neighboring cells, sorts candidates by horizontal distance from the death position, and uses the Alpha ordering rule when multiple candidates are available.

## Requirements

- Cube World Steam release, **64-bit**
- Cube World Mod Loader
- Visual Studio 2022 with the **Desktop development with C++** workload (only required to build from source)

The project is configured for **Release | x64**. It uses MSVC directly; CMake and Clang are not required.

## Installation

1. Install Cube World Mod Loader.
2. Copy `AlphaShrines.dll` into the loader's `Mods` directory.
3. Start Cube World.
4. Confirm the in game chat message: `[AlphaShrines] Alpha logic restored.`

If the game build does not match the validated runtime signatures, AlphaShrines reports that the build was not recognized and does not install its hooks.

## Building From Source

1. Open `AlphaShrines.sln` in Visual Studio 2022.
2. Select **Release** and **x64**.
3. Build the `AlphaShrines` project.
4. Copy `bin\\x64\\Release\\AlphaShrines.dll` to the mod loader's `Mods` directory.

The repository includes CWSDK alongside the project. If you move it, update the `CWSDKRoot` property in `AlphaShrines.vcxproj`.

## Acknowledgements

- [CWSDK](https://github.com/coremaze/CWSDK) for the Cube World modding SDK.
- [CubeWorld-Reversal](https://github.com/qad3n/CubeWorld-Reversal) for the Alpha behavior reference used during the respawn selection reconstruction.

## License

AlphaShrines original source code is licensed under the [MIT License](LICENSE).

CWSDK is a third party dependency by coremaze. It is included solely for build convenience and is not covered, relicensed, or otherwise affected by AlphaShrines' MIT License. CWSDK remains subject to its original authorship and any applicable terms.

## Disclaimer

AlphaShrines is an unofficial community mod and is not affiliated with or endorsed by Wolfram von Funck or Picroma.
