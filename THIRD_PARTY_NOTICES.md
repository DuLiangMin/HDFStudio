# Third-party notices

HDFStudio is distributed under the GNU General Public License v3 (see `LICENSE`).
It is derived from HDF Explorer QT Edition by Pedro Vicente (2016, GPL-3.0),
https://github.com/pedro-vicente/hdf_explorer_qt .

The Windows binary package bundles or links the following components.

| Component | Version | How it is used | License | Text in the binary package |
|---|---|---|---|---|
| Qt (Core, Gui, Widgets and platform plugins) | 6.8.3 | Dynamically linked DLLs, unmodified | LGPL-3.0 | `licenses/Qt-LGPL-3.0.txt` |
| netCDF-C | 4.9.3 | `netcdf.dll`, unmodified | BSD-style (UCAR) | `licenses/netCDF-C-COPYRIGHT.txt` |
| HDF5 | 1.8.20 | Statically linked into `HDFStudio.exe` | BSD-style (The HDF Group) | `licenses/HDF5-COPYING.txt` |
| zlib | 1.3.1 | Compression support of the native libraries | zlib License | `licenses/zlib-LICENSE.txt` |
| Bitshuffle core | 0.5.2 | Statically linked read-only HDF5 filter 32008 | MIT | `licenses/hdf-filters/Bitshuffle-MIT.txt` |
| LZ4 | 1.10.0 | Statically linked Bitshuffle block decoding | BSD-2-Clause | `licenses/hdf-filters/LZ4-BSD-2-Clause.txt` |
| Zstandard | 1.5.7 | Statically linked Bitshuffle block decoding | BSD-3-Clause | `licenses/hdf-filters/Zstandard-BSD-3-Clause.txt` |
| ecCodes and embedded definitions/samples | 2.47.0 | Native GRIB1/GRIB2 decoding, pinned official wheel binaries | Apache-2.0 | `licenses/eccodes/libeccodes.txt` |
| OpenJPEG / libaec / libpng / zlib | Pinned ecCodes wheel dependencies | GRIB JPEG2000, CCSDS and PNG decoding | Upstream BSD-style / libpng / zlib terms | `licenses/eccodes/` |
| Microsoft Visual C++ runtime | 14.x | `msvcp140.dll`, `vcruntime140*.dll` | Visual Studio redistributable terms | - |

Qt is used under the LGPL-3.0. The Qt libraries are shipped as separate, unmodified DLLs, so
they can be replaced with another compatible Qt 6.8 build. Qt source code is available from
https://download.qt.io/archive/qt/6.8/6.8.3/single/ .

Optional runtime tools (not bundled; installed by the user with Python):
netCDF4-python (MIT), numpy (BSD-3-Clause), cftime (MIT), IOOS compliance-checker (Apache-2.0).

## 🎨 Color-map resources

HDFStudio embeds selected data exported with **cmap 0.8.0**, plus five NCL RGB
tables pinned to commit `8f9e9476281cc6f6d9d12eaa78729c7003ca24b7`. Python/cmap
and the NCL runtime are not needed to use these color maps.

| Data | Copyright / attribution | License |
|---|---|---|
| BIDS Viridis | Nathaniel J. Smith, Stefan van der Walt | CC0-1.0 |
| cmocean rain, thermal, balance, speed, curl, phase | 2015 Kristen M. Thyng | MIT |
| Crameri batlow | 2021 Fabio Crameri | MIT |
| NCL precip_11lev, precip_diff_12lev, temp_19lev, wind_17lev, BlueWhiteOrangeRed | 2019 UCAR; first four files also credit MeteoSwiss NCL library | Apache-2.0, per the locked NCL repository LICENSE |
| cmap development-time export tool | 2022 Talley Lambert | BSD-3-Clause |
| Jet / Rainbow, Matplotlib 3.10.7 | Matplotlib Development Team; Rainbow functions credit Thomas Williams and Colin Kelley | Matplotlib license, full text in `licenses/colormaps/Matplotlib-LICENSE.txt` |

Full license texts, upstream REUSE copyright assignments, NCL copyright and
modification/legacy-color notices are in `licenses/colormaps/`. Source inputs,
versions and SHA256 hashes are recorded in `resources/colormaps/manifest.json`
and per-palette JSON. Raw NCL files retain their original comments. Derived
tables retain all color nodes; RGB normalization and JSON conversion are
identified as HDFStudio changes. No source institution endorses this program.

User RGB imports remain in the user's application-data directory and are not
silently added to the distributed palette collection.

## 🧩 GRIB source and fixtures

The GRIB domain reader is adapted from GribStudio (GPL-3.0), with HDFStudio-specific
catalog, data-source and view adapters. ecCodes binaries/headers and their hashes are
listed in `licenses/eccodes/native-manifest.json`; source for the pinned version is
https://github.com/ecmwf/eccodes/tree/2.47.0 . The small `testdata/grib/` files are
generated test fixtures from GribStudio; no external business data are bundled.

## 🌊 Low-resolution reference coastlines

`resources/coastlines/low/` contains byte-for-byte copies of `gshhs_l.dat` and
`gshhsmeta_l.dat` from **Basemap v2.0.0**, commit
`6fed00b9b6146d19ffa98fd00a19cbb0ee448141`. Its upstream README identifies
**GSHHG 2.3.6** as the source and **LGPL-3.0-or-later** as the data license.
GSHHG is maintained by Paul Wessel, Walter H. F. Smith and upstream contributors.

Full license texts are in `licenses/coastlines/COPYING` and
`licenses/coastlines/COPYING.LESSER`. The original upstream README, fixed source
URLs, file identities and SHA256 hashes are retained alongside the data.
The upstream `LICENSE.mit` is also retained for its non-GSHHG supporting
material, including its Jeffrey Whitaker attribution; it does not relicense
the coastline data.
No Basemap Python implementation or high-resolution data package is bundled.
The development build loads normalized display polylines and deploys their
originals, converter, manifests and notices beside the program. No new release
archive has been published. Do not treat the data's license as the Basemap
code's MIT license; derived assets identify their changes and preserve source.

## 🌍 PROJ and its SQLite dependency

The display projection core uses PROJ **9.9.0**, built with vcpkg baseline
`277978ad2eddc3b05ecf288dcd33071968cd2f79`, `proj[core,tools]:x64-windows`.
Network and TIFF features are disabled. PROJ's original MIT-style notice is
preserved in `licenses/proj/COPYING`. Its SQLite **3.54.1** dependency's original
notice is in `licenses/proj/SQLITE-COPYRIGHT`. Matching Release/Debug DLLs and
`proj.db` identities are recorded in `resources/proj/dependency-lock.json`.
The application explicitly uses its bundled resource directory with network
disabled. No datum grid collection or external GIS/Python runtime is required
for the supported same-model display projections.

The normalized coastline asset and public converter are also retained in
`resources/coastlines/`; `low/derived_manifest.json` identifies the modifications
and output hash. Normalization removes only identified polygon closure edges;
the data retains the upstream LGPL-3.0-or-later designation.
