# HDFStudio — NetCDF, HDF5 & GRIB Viewer for Windows

**Unleash the Power of Your NetCDF and GRIB Data**

**English** | [简体中文](README_CN.md) · **Version 0.14.08**

## ⬇️ Download

Download **0.14.08** below. Older binary archives have been removed to reduce storage; their release notes, tags and source archives remain available.

[![Release](https://img.shields.io/github/v/release/DuLiangMin/HDFStudio?label=release&color=3d99a4)](https://github.com/DuLiangMin/HDFStudio/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/DuLiangMin/HDFStudio/total?color=3d99a4)](https://github.com/DuLiangMin/HDFStudio/releases)
[![License: GPL v3](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-lightgrey)](https://github.com/DuLiangMin/HDFStudio/releases/download/v0.14.08/HDFStudio-0.14.08-win64.zip)
[![Qt](https://img.shields.io/badge/Qt-6.8-41cd52)](https://www.qt.io/)

**[Download for Windows](https://github.com/DuLiangMin/HDFStudio/releases/download/v0.14.08/HDFStudio-0.14.08-win64.zip)** — **0.14.08 Windows x64 portable binary ZIP** · [Source code ZIP](https://github.com/DuLiangMin/HDFStudio/releases/download/v0.14.08/HDFStudio-0.14.08-src.zip) · [Release notes](https://github.com/DuLiangMin/HDFStudio/releases/tag/v0.14.08) · [All releases](https://github.com/DuLiangMin/HDFStudio/releases)

Includes the application, CLI, GRIB decoder and definitions, runtime libraries and offline help. Unzip and run; no separate dependency download is needed for native browsing.

HDFStudio is a free, open-source Windows tool for inspecting meteorological, climate and scientific data. Browse NetCDF-3/4, HDF5 and native GRIB1/GRIB2, inspect metadata, view values and images, and compare layers in one workspace. Source files are read-only.

**🤖 Friendly to modern LLMs and AI agents:** HDFStudio extracts NetCDF, GRIB and other supported data information into copyable Properties reports, CLI output and numeric CSV. These give models useful reference material to quickly understand dimensions, variables, units, packing and missing values. See the [online help guide](https://duliangmin.github.io/HDFStudio/doc/help.html#en-ai) for workflows and examples.

![HDFStudio layer playback and synchronized texture entropy](docs/demo-analysis.gif)

*NetCDF temperature playback beside its synchronized 4×4 texture entropy image.*

## 🚀 Start here

1. Click **Download** above, unzip and run `HDFStudio.exe`; no installer or administrator rights are needed.
2. Choose **File → Open** or drop files onto the window. Expand the tree and right-click a variable for **Properties**, **Data Grid** or **Image View**.
3. Read the [online Chinese/English help](https://duliangmin.github.io/HDFStudio/doc/help.html#en). In the downloaded application, choose **Help → Help** (before About), press **F1**, or double-click the local `doc/help.html` for offline help.

Keep the executable, matching `hdf_cli.exe`, dependency folders and `doc` together. Basic browsing and native analysis need no Python. Optional NetCDF reports, CF checks and calendar labels use Python with the corresponding `netCDF4`, `numpy` and `compliance-checker` modules; `HDFSTUDIO_PYTHON` selects the interpreter.

**🔎 New in 0.14.08:** **File → Find files...** searches a directory by name (including extensionless files); **Find variables...** locates names, paths and descriptions in the current open file. [Usage and limits](docs/SEARCH.md).

**🌍 Map display in 0.14.08:** The single-row **Map projection** bar above Image redraws and remembers longitude/latitude, North/South Polar Stereographic, East Asia Lambert or Mercator as soon as it is selected; no OK click is needed. **Options…** opens live detailed parameters and offline reference-coastline styling. Verified separable NetCDF axes, GRIB `regular_ll` and rectangular Gaussian `regular_gg` grids are supported, preserving actual nonuniform Gaussian latitudes. Map labels use solid text without boxes or outlines; projected playback retains the completed frame until its replacement is ready. Sources without a declared Earth model use explicitly labelled unit-sphere display, with no physical distance or area claim; declared valid models are retained. Source data, analysis and source-resolution PNG remain in the original grid. [Use and limits](docs/PROJECTION.md).

## 🌦️ Native GRIB support

- **Browse GRIB1 and GRIB2 directly:** virtual **Variables** group compatible fields; paged **Messages** preserve physical messages and sub-fields, metadata and provenance.
- **Inspect and visualize:** double-click rectangular variables/fields for **Image View**; decoded, reconstructed **Quantized X** and paired grids, scientific palettes, decoded-value Mask and CSV/PNG export. Quantized X is available only for supported packing and is not a raw-byte dump.
- **Analyze and play:** histograms, curve profiles, whole-plane and 2D texture entropy, comparison metrics, residual maps, integer-index synchronization and layer playback.
- **Space-Time Section (Hovmöller):** independent GUI and CLI sections for rectangular GRIB arrays, arbitrary ordered axes, original reference/step labels, fixed-axis navigation, Mask and PNG/CSV export. Missing slots remain explicit. Geographic-coordinate section axes and U/V vectors remain pending.

Unknown names/units remain unknown; missing or out-of-range fields clear prior results. GRIB images open in source-grid mode, or restore a saved map preference after source validation; eligible regular latitude–longitude and rectangular Gaussian grids can use map display. Comparisons require identical plane shapes and align by integer index, without automatic unit conversion, time matching, reprojection or resampling. CF checks do not apply to GRIB.

## 🧰 Everyday tools

| Task | Feature |
|---|---|
| Inspect values | Copyable Properties; decoded/stored/paired NetCDF/HDF5 grids; automatic column widths, cell selection and Ctrl+C |
| View images | Jet spectrum colors by default, selectable Rainbow and scientific palettes, zoom/pan, flips, decoded-value Mask and full-resolution PNG |
| Inspect distributions | Sampled or full-plane histograms, curve CSV and texture entropy images |
| Compare fields | Pearson r, RMSE, MAE, Bias, `Target − Anchor` residuals and per-target integer lag |
| Browse sequences | Layer controls and Play/Pause; linked views wait for the current data |
| Slice NetCDF/HDF5/GRIB | Independent **Space-Time Section (Hovmöller)** with any two axes, fixed-axis navigation and PNG/CSV |
| Arrange the workspace | **Window → Tile / Cascade / Hide Infobar** |

NetCDF/HDF5 sections retain source-index cell spacing and verified coordinate labels. Generic HDF5 stays in index space. CF diagnostics are advisory; unsupported encodings and memory limits are reported explicitly. Details are in the [help guide](https://duliangmin.github.io/HDFStudio/doc/help.html#en).

**Fit** fills the section plot width and height with smooth display interpolation, including short or single-column sections. Probes, CSV and native-resolution PNG retain the original samples; this display stretch does not interpolate physical coordinates.

## ⌨️ Command-line inspection

```text
hdf_cli probe data.grib2
hdf_cli info data.grib2
hdf_cli props data.grib2 /field/0
hdf_cli csv data.grib2 /field/0 plane.csv
hdf_cli props data.nc /temperature
```

Use the variable/object ID reported for your file. `hdf_cli` shares the application's readers and also provides histogram, entropy, comparison and arbitrary-axis slice commands. See the [GRIB contract](docs/GRIB.md) for tested behavior and current boundaries.

## 🛠️ Build and contribute

Windows development uses Visual Studio 2022 x64, CMake, Qt 6.8, HDF5, netCDF-C, ecCodes and PROJ. Dependency locations and targets are defined in `CMakeLists.txt`, `cmake/Grib.cmake` and `cmake/Projection.cmake`; ecCodes defaults to `ext/eccodes` and can be set with `HDFSTUDIO_ECCODES_ROOT`; PROJ uses an isolated `PROJ_DIR` with matching database and SQLite DLL. Code lives in `src/app`, `src/core`, `src/views` and `src/cli`; tests are in `tests/`. Builds copy the offline guide to `build/output/<configuration>/doc/help.html`.

The public source omits `tools/` and third-party code copies. Use the [source ZIP](https://github.com/DuLiangMin/HDFStudio/releases/download/v0.14.08/HDFStudio-0.14.08-src.zip), [Dependencies](docs/DEPENDENCIES.md) and the [project structure](docs/PROJECT_STRUCTURE.md); exact dependency URLs and hashes remain in the manifests. Runtime libraries and license texts remain in the binary ZIP.

Report reproducible problems in [Issues](https://github.com/DuLiangMin/HDFStudio/issues), with the format, object ID, shape and relevant `probe`/`info`/`props` output. Release history is in [Changelog.md](Changelog.md).

## 📜 License and credits

GNU GPL v3 — see [LICENSE](LICENSE). Derived from Pedro Vicente's [HDF Explorer QT Edition](https://github.com/pedro-vicente/hdf_explorer_qt). Dependency and palette attribution is retained in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `licenses/`.

## 🔎 Keywords

NetCDF viewer, NetCDF browser, NetCDF4 viewer Windows, nc file viewer, open .nc file, HDF5 viewer,
HDF viewer, HDF5 browser, scientific data viewer, climate data viewer, weather data, meteorology,
atmospheric science, oceanography, earth science, climate model output, CMIP, reanalysis, GRIB2 to NetCDF,
CF Conventions, CF-1.8 compliance checker, netCDF4-python, num2date, time decoding, noleap calendar,
latitude longitude grid, Qt6 desktop application, free open source, alternative to Panoply, ncview, HDFView.

GRIB viewer, GRIB1 viewer, GRIB2 viewer Windows, weather forecast data, LLM data inspection, AI agent data tools,
gray-level entropy, Space-Time Section (Hovmöller).
