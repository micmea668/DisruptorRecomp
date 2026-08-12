# Third-party notices

`psxrecomp-overlay/` contains modified files based on PSXRecomp revision
`854a658372b304c222a0d0cbbae16c66264b785e`. PSXRecomp is copyright Matthew
Stan and is provided under the PolyForm Noncommercial License 1.0.0. The
upstream license is reproduced in
[`licenses/PSXRecomp-LICENSE.txt`](licenses/PSXRecomp-LICENSE.txt).

The build downloads the pinned upstream framework and its dependencies. Their
own notices remain in that checkout and resulting local build. No third-party
binaries are stored in this repository.

The optional in-game settings and diagnostics menu builds against Dear ImGui
1.92.6, copyright Omar Cornut and Dear ImGui contributors, under the MIT
License. The source archive is integrity-pinned in `CMakeLists.txt`; its license
is reproduced in [`licenses/Dear-ImGui-LICENSE.txt`](licenses/Dear-ImGui-LICENSE.txt).
