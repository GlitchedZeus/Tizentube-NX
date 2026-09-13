# Third-party components

Borealis is pinned as a Git submodule at
`20e2d33b6c4ffce139ce304c503c04f5b94da920` from
https://github.com/natinusala/borealis (Apache-2.0). Its bundled dependencies
retain their original licenses in `third_party/borealis/library/lib/extern`.
The Switch makefile is adapted from its Apache-2.0 demo build. Bundled UI hint
translations and Material Icons are copied from its resources; their licenses
are retained. The app icon SVG/JPEG is original project artwork.

Initialize dependencies with `git submodule update --init --recursive`.
Shaders compile from the pinned source and are packaged inside NRO RomFS.
