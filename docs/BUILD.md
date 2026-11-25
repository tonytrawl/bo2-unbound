# Build Unbound HQ

This is the Wii U / Aroma app. The public source and build assets are in `tonytrawl/bo2-unbound`.

## Docker build

The Dockerfile specifies the devkitPro image version and pinned libmocha/librpxloader artifacts.

```sh
git clone https://github.com/tonytrawl/bo2-unbound.git
cd bo2-unbound
docker build -t unbound-hq-builder .
docker run --rm -v "${PWD}:/project" unbound-hq-builder make -j6
```

The build produces `Unbound-HQ.wuhb`. Install it at `sd:/wiiu/apps/Unbound-HQ/Unbound-HQ.wuhb`.

For a local toolchain, install devkitPro's `wiiu-dev` and `wiiu-portlibs`, libmocha and librpxloader, then run `make`. Python 3 prepares the runtime content. The committed `content/` and `data/` directories contain build assets.

## Host verification

With the devkitPro build environment available, native checks also require a C++20 compiler, Python 3, pkg-config, SDL2/SDL2_image/SDL2_ttf, libzip and libcurl development libraries.

```sh
make test
make test-zip
make test-curl
python3 tests/install_flow_source_tests.py
python3 tests/sd_package_flow_tests.py
python3 tests/map_publisher_tests.py
```

`tools/render_v1.cpp` captures the real SDL UI with fixture data and verifies touch controls. Use a Wii U for final hardware testing.

The application source is MIT licensed. Preserve bundled fonts, icons and other third-party license/attribution files.
