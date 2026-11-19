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
