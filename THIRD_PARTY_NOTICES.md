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

Full license texts, upstream REUSE copyright assignments, NCL copyright and
modification/legacy-color notices are in `licenses/colormaps/`. Source inputs,
versions and SHA256 hashes are recorded in `resources/colormaps/manifest.json`
and per-palette JSON. Raw NCL files retain their original comments. Derived
tables retain all color nodes; RGB normalization and JSON conversion are
identified as HDFStudio changes. No source institution endorses this program.

User RGB imports remain in the user's application-data directory and are not
silently added to the distributed palette collection.
