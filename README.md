# Fruit Company Renderer

This is a work-in-progress Apple Metal renderer for Unreal Engine 1-based games. The current plan is to include this renderer in OldUnreal's v469e patch for Unreal Tournament and v227k patch for Unreal.

## Project Status

This renderer builds and can be used in game. The following features are currently implemented:

* Metal initialization
* Texture support for P8 and all of the compressed formats in use in Unreal Tournament
* Support for Unreal Tournament's real-time textures
* Basic support for BSP, mesh, tile, and simple triangle rendering
* Light maps, fog maps, detail textures, macro textures
* Screenshots
* Multisample Anti-Aliasing (2x, 4x, and/or 8x, depending on what the device supports)

The following features are not implemented (yet):

* Editor support
* Line rendering
* LODBias
* Bump Mapping

## Build Instructions

### Prerequisites

You can only build this renderer if you have full source code access to an Unreal Engine 1 game. You will also need to a fairly recent version of metal-cpp.

### CMake

A top-level CMake file that builds an entire Unreal Engine 1 game will be included in OldUnreal's SDK for Unreal Tournament v469e.

## License

See LICENSE.md.