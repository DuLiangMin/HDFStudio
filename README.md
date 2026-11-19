# HDFStudio — NetCDF & HDF5 Viewer for Windows

**Unleash Your NetCDF Power.**

HDFStudio is a free, open-source desktop **NetCDF viewer** and **HDF5 browser** for Windows, built for
meteorology, climate and earth-science work. Open a `.nc`, `.nc4`, `.h5` or `.hdf5` file, browse
groups, variables and attributes, read values in a table, view 2-D slices as images, decode time
coordinates into real dates, and check CF metadata conventions, all without writing code.

Version: 0.10.25 · License: GPL-3.0 · Platform: Windows 10/11 x64

## ✨ Features

- 📂 **Open NetCDF and HDF5 files**: NetCDF-3 (CDF-1, CDF-2, CDF-5), NetCDF-4 / NetCDF-4 classic, and HDF5 containers; drag and drop supported.
- 🌳 **Dataset tree**: groups, dimensions, variables and attributes with type and shape details.
- 🔢 **Data grid**: exact values in a table, with layer navigation for 3-D, 4-D and 5-D arrays.
- 🗺️ **Image view**: 2-D slices with continuous and discrete color palettes, and latitude/longitude readout on hover for CF lat/lon grids.
- ⏱️ **Time decoding**: `time` variables such as `seconds since 1970-01-01` or `days since 2024-10-01` are shown as `yyyymmdd` and `yyyymmdd hh:mm:ss`, using the file's own calendar (`standard`, `noleap`, …) via `netCDF4.num2date`.
- 🧭 **Coordinate orientation**: the By NetCDF4 tab reports whether latitude runs south→north or north→south and longitude west→east or east→west, a common source of flipped maps.
- ✅ **CF Conventions check**: a CF-1.8 report from the IOOS Compliance Checker, plus built-in advisory CF metadata diagnostics.
- 🌬️ **Vector view** for U/V wind or current components, and a preview of **UGRID** triangular meshes.
- 💾 **Export** values to text and slices to PNG; batch preview from the command line.

## 📥 Download and run

1. Download `HDFStudio-<version>-win64.zip` from the [Releases](../../releases) page.
2. Unzip it anywhere and run `HDFStudio.exe`. No installer or admin rights needed. The Qt and Visual C++ runtimes are included.

The tree, grid, image view and File Info tab work out of the box. The **By NetCDF4** tab, the **CF Check**
tab and time decoding use Python 3 on `PATH` (or set `HDFSTUDIO_PYTHON`):

```
pip install netCDF4 numpy compliance-checker
```

Command-line preview (no window):

```
HDFStudio.exe --preview PRECT --layer 0 --png out.png data.nc
```

## 🛠️ Build from source (Windows)

Visual Studio 2022 x64, CMake, Qt 6.8 (`CMAKE_PREFIX_PATH`), HDF5 1.8.20 built into `build/hdf5-1.8.20`,
and netCDF-C 4.9.3 headers in `ext/netcdf-c-4.9.3/include` with `netcdf.lib`/`netcdf.dll` in
`build/native-nc/Release`. See `CMakeLists.txt`. `tools/package-windows.ps1` builds the release zips.
`build.cmake.sh` is the inherited Linux script and has not been tested for this release.

## 📌 Scope

HDFStudio is a viewer and checker; it never modifies input files. CF diagnostics are advisory and not a
conformance certification. Map projections and arbitrary unstructured meshes are not supported yet.
See `FORMAT_CAPABILITY.md` for the verified format boundary.

## 🙏 Credits and license

Author: DuLiangMin. HDFStudio is derived from [HDF Explorer QT Edition](https://github.com/pedro-vicente/hdf_explorer_qt)
by Pedro Vicente (2016) and is released under the GNU GPL v3 (`LICENSE`). Bundled third-party components
(Qt 6.8 LGPL-3.0, netCDF-C 4.9.3, HDF5 1.8.20, zlib) are listed in `THIRD_PARTY_NOTICES.md`.

## 🔎 Keywords

NetCDF viewer, NetCDF browser, NetCDF4 viewer Windows, nc file viewer, open .nc file, HDF5 viewer,
HDF viewer, HDF5 browser, scientific data viewer, climate data viewer, weather data, meteorology,
atmospheric science, oceanography, earth science, climate model output, CMIP, reanalysis, GRIB2 to NetCDF,
CF Conventions, CF-1.8 compliance checker, netCDF4-python, num2date, time decoding, noleap calendar,
latitude longitude grid, Qt6 desktop application, free open source, alternative to Panoply, ncview, HDFView.
