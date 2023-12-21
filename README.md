# Fruit Company Renderer

This is a work-in-progress Apple Metal renderer for Unreal Engine 1-based games. The current plan is to include this renderer in OldUnreal's v469e patch for Unreal Tournament.

## Project Status

This renderer builds and can be used in game. The following features are currently implemented:

* Metal initialization
* Texture support for P8 and all of the compressed formats in use in Unreal Tournament
* Support for Unreal Tournament's real-time textures
* Basic support for BSP, mesh, tile, and simple triangle rendering
* Screenshots

The following features are not implemented (yet):

* Resolution switching
* Editor support
* Line rendering

Known issues:

* Certain meshes (e.g., UT's health vials) do not render correctly. I suspect this is because some of the blending modes are not implemented correctly
* 3D projection for tiles has some issues. This issue is particularly visible when shooting a distant shock rifle ball. Although I have only briefly looked into this problem, it seems like the renderer still writes depth values for discarded fragments 

## Build Instructions

### Prerequisites

You can only build this renderer if you have full source code access to an Unreal Engine 1 game. You will also need to a fairly recent version of metal-cpp.

### CMake

A top-level CMake file that builds an entire Unreal Engine 1 game will be included in OldUnreal's SDK for Unreal Tournament v469e.

## License

See LICENSE.md.