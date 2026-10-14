# Third-party notices

The add-on's authored C++ and profiles are MIT licensed. The JSON header vendored in third_party/nlohmann is nlohmann/json (MIT), copyright Niels Lohmann. Its exact LICENSE.MIT is included in every runtime bundle.

The companion follows MyWallpaper's process-v2 wire v5. It uses the existing host supervision and native-only manifest contract. It performs external Win32 operations; it does not load the engine's injection agent, MinHook, Windhawk or a hook DLL. The sibling engine SDK and taskbar implementation were read as references. No competitor source is incorporated into the build.

Competitor mechanisms and licences are recorded in docs/source-study.md with pinned source links. GPL-marked AltDrag, Hyprland Windows, Slick Window Arrangement and Three Finger Drag are study inputs only, with no code reuse.
