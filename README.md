# HDFStudio — NetCDF & HDF5 Viewer for Windows

**Unleash Your NetCDF Power.**

**English** | [简体中文](README_CN.md)

[![Release](https://img.shields.io/github/v/release/DuLiangMin/HDFStudio?label=release&color=3d99a4)](https://github.com/DuLiangMin/HDFStudio/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/DuLiangMin/HDFStudio/total?color=3d99a4)](https://github.com/DuLiangMin/HDFStudio/releases)
[![License: GPL v3](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-lightgrey)](#-download-and-run)
[![Qt](https://img.shields.io/badge/Qt-6.8-41cd52)](https://www.qt.io/)

HDFStudio is a free, open-source desktop **NetCDF viewer** and **HDF5 browser** for Windows, built for
meteorology, climate and earth-science work. Open a `.nc`, `.nc4`, `.h5` or `.hdf5` file, browse
groups, variables and attributes, read values in a table, view 2-D slices as images, decode time
coordinates into real dates, and check CF metadata conventions, all without writing code.

![HDFStudio showing two NetCDF files, two image views with different color palettes, and the file information panel](docs/screenshot-main.png)

*Two NetCDF files open side by side: 2-metre temperature rendered with the Viridis and Turbo palettes,
the dataset tree on the left, and the File Info / By NetCDF4 / CF Check panel at the bottom.*

## 🤔 Why HDFStudio?

You have a `.nc` file from a model, a satellite product or a colleague, and you need to know **what is
really inside** before you trust it. Typical questions:

- Is `time` in seconds or days, and since when? Which calendar?
- Does latitude run south→north or north→south? Is longitude 0–360 or −180–180?
- Is this file CF compliant, or will my plotting library flip the map?

HDFStudio answers these in seconds, with no Python session, no notebook and no installation of a
scientific stack. It is a small, portable program (about 15 MB) that opens a file and shows you the facts.

| | HDFStudio | Panoply | ncview | HDFView |
|---|---|---|---|---|
| Windows native, portable zip | ✅ | Java required | ❌ | Java required |
| NetCDF **and** HDF5 | ✅ | ✅ | NetCDF | ✅ |
| Time decoded with the file's own calendar | ✅ (`noleap`, `standard`, …) | ✅ | ❌ | ❌ |
| Latitude / longitude orientation report | ✅ | ❌ | ❌ | ❌ |
| CF-1.8 compliance check built in | ✅ | ❌ | ❌ | ❌ |
| Map projections and coastlines | ❌ not yet | ✅ | ❌ | ❌ |

*Comparison reflects the author's understanding; check each tool's documentation for details.*

## ✨ Features

- 📂 **Open NetCDF and HDF5 files**: NetCDF-3 (CDF-1, CDF-2, CDF-5; since 0.10.27, read through netCDF-C), NetCDF-4 / NetCDF-4 classic, and HDF5 containers; open several files at once, drag and drop supported. The format is detected from the file header, not the `.nc` extension.
- 🗂️ **Close files cleanly**: **File → Close File** (`Ctrl+F4` or `Ctrl+W`), **Close All Files**, or right-click any node → **Close File**. Views of a closed file close with it.
- 🌳 **Dataset tree**: groups, dimensions, variables and attributes with type and shape details.
- 🔢 **Data grid**: exact values in a table, with layer navigation for 3-D, 4-D and 5-D arrays. For NetCDF-3 files only the displayed 2-D plane is read, so a 700 MB variable opens in under 0.1 s.
- ⬆️⬇️ **Layer toolbar**: next, previous, last and first layer buttons right after Open, with a dimension picker and a layer picker, and a `Layer n of N` readout. The **Layer** menu has the same commands (`Alt+←`/`Alt+→`/`Alt+Home`/`Alt+End`, `Ctrl+G` to type a layer number). Works on the grid and the image view of the active window, for any leading axis such as `time` or `lev`.
- 🔍 **Properties** like `print(netCDF4.Dataset(...))`: dimension names, unlimited dimension, and attribute values with arrays expanded.
- 🗺️ **Image view**: 2-D slices with continuous and discrete color palettes (Viridis, Inferno, Turbo, …), and latitude/longitude readout on hover for CF lat/lon grids.
- ⏱️ **Time decoding**: `time` variables such as `seconds since 1970-01-01` or `days since 2024-10-01` are shown as `yyyymmdd` and `yyyymmdd hh:mm:ss`, using the file's own calendar via `netCDF4.num2date`.
- 🧭 **Coordinate orientation**: the By NetCDF4 tab reports whether latitude runs south→north or north→south and longitude west→east or east→west, a common source of flipped maps.
- ✅ **CF Conventions check**: a CF-1.8 report from the IOOS Compliance Checker, plus built-in advisory CF metadata diagnostics.
- 🌬️ **Vector view** for U/V wind or current components, and a preview of **UGRID** triangular meshes.
- 💾 **Export** values to text and slices to PNG; batch preview from the command line.

## 📥 Download and run

1. Download `HDFStudio-<version>-win64.zip` from the [Releases](https://github.com/DuLiangMin/HDFStudio/releases/latest) page.
2. Unzip it anywhere and run `HDFStudio.exe`. No installer or admin rights needed. The Qt and Visual C++ runtimes are included.

The tree, grid, image view and File Info tab work out of the box. The **By NetCDF4** tab, the **CF Check**
tab and time decoding use Python 3 on `PATH` (or set `HDFSTUDIO_PYTHON`):

```
pip install netCDF4 numpy compliance-checker
```

Open files from the command line, or run a window-less batch preview:

```
HDFStudio.exe a.nc b.nc
HDFStudio.exe --preview PRECT --layer 0 --png out.png data.nc
```

`hdf_cli.exe` (in the same zip) runs the same reading code without a window, which helps when a file
does not open or a value looks wrong:

```
hdf_cli probe  data.nc              format detected from the file header
hdf_cli info   data.nc              dimensions, variables and attributes, laid out like netCDF4-python
hdf_cli props  data.nc /ttr         the lines of the Properties dialog
hdf_cli export data.nc /lat         values as text (add a file name to write a file)
hdf_cli csv    data.nc /ttr plane.csv 7 42    one 2-D plane as the image view reads it
```

## 🛠️ Build from source (Windows)

Visual Studio 2022 x64, CMake, Qt 6.8 (`CMAKE_PREFIX_PATH`), HDF5 1.8.20 built into `build/hdf5-1.8.20`,
and netCDF-C 4.9.3 headers in `ext/netcdf-c-4.9.3/include` with `netcdf.lib`/`netcdf.dll` in
`build/native-nc/Release`. See `CMakeLists.txt`. `tools/package-windows.ps1` builds the release zips.
`build.cmake.sh` is the inherited Linux script and has not been tested for this release.

## 📌 Scope

HDFStudio is a viewer and checker; it never modifies input files. CF diagnostics are advisory and not a
conformance certification. Map projections and arbitrary unstructured meshes are not supported yet.
NetCDF-3 grids read one 2-D plane at a time; for NetCDF-4/HDF5 the grid still reads the whole variable,
and chunked reading for those is on the roadmap.
See `FORMAT_CAPABILITY.md` for the verified format boundary.

## 🗺️ Roadmap

- Sliced and chunked reading for multi-gigabyte NetCDF-4 / HDF5 variables
- Map projections and coastline overlay
- More time-axis and calendar checks (bounds, irregular steps)
- Linux and macOS builds

Ideas and bug reports are welcome in [Issues](https://github.com/DuLiangMin/HDFStudio/issues). If a file
opens wrongly, please attach the output of `hdf_cli probe file.nc` and `hdf_cli info file.nc` (or
`ncdump -h file.nc`); these are header only, no data.

## 🙏 Credits and license

Author: DuLiangMin. HDFStudio is derived from [HDF Explorer QT Edition](https://github.com/pedro-vicente/hdf_explorer_qt)
by Pedro Vicente (2016) and is released under the GNU GPL v3 (`LICENSE`). Bundled third-party components
(Qt 6.8 LGPL-3.0, netCDF-C 4.9.3, HDF5 1.8.20, zlib) are listed in `THIRD_PARTY_NOTICES.md`.

If HDFStudio saves you time, a ⭐ on the repository helps other researchers find it.

## 🔎 Keywords

NetCDF viewer, NetCDF browser, NetCDF4 viewer Windows, nc file viewer, open .nc file, HDF5 viewer,
HDF viewer, HDF5 browser, scientific data viewer, climate data viewer, weather data, meteorology,
atmospheric science, oceanography, earth science, climate model output, CMIP, reanalysis, GRIB2 to NetCDF,
CF Conventions, CF-1.8 compliance checker, netCDF4-python, num2date, time decoding, noleap calendar,
latitude longitude grid, Qt6 desktop application, free open source, alternative to Panoply, ncview, HDFView.
