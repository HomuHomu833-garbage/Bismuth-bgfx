# Bismuth
Rendering optimization mod with the goal of fully
rewriting the rendering side of Geometry Dash
for maximum performance.

Currently experimental

<img src="logo.png" alt="Bismuth logo" width="100"/>

Bismuth renders through [bgfx](https://github.com/bkaradzic/bgfx) on Windows,
macOS, iOS and Android: Vulkan / Metal by default, with an OpenGL fallback that
runs inside Geometry Dash's own context. Linux is supported through the Windows
build under Wine / Proton. See [docs/RENDERING.md](docs/RENDERING.md).

## License

Bismuth - Optimization mod for Geometry Dash

Copyright (C) 2026 IliasHDZ

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

THIS PROJECT CANNOT BE USED UNDER ANY CIRCUMSTANCES TO FACILITATE OR
ADVANCE GEOMETRY DASH LEVEL GENERATION OR MODIFICATION USING
GENERATIVE AI.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.