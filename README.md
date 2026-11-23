# HDFStudio — NetCDF, HDF5 & GRIB Viewer for Windows

**Unleash the Power of Your NetCDF and GRIB Data**

**English** | [简体中文](README_CN.md) · **Version 0.14.02**

## ⬇️ Download

**[Download](https://github.com/DuLiangMin/HDFStudio/releases/download/v0.14.02/HDFStudio-0.14.02-win64.zip)** — **0.14.02 Windows x64 portable binary ZIP** · [All releases](https://github.com/DuLiangMin/HDFStudio/releases)

Includes the application, CLI, GRIB decoder and definitions, runtime libraries and offline help. Unzip and run; no separate dependency download is needed for native browsing.

HDFStudio is a free, open-source Windows tool for inspecting meteorological, climate and scientific data. Browse NetCDF-3/4, HDF5 and native GRIB1/GRIB2, inspect metadata, view values and images, and compare layers in one workspace. Source files are read-only.

**🤖 Friendly to modern LLMs and AI agents:** HDFStudio extracts NetCDF, GRIB and other supported data information into copyable Properties reports, CLI output and numeric CSV. These give models useful reference material to quickly understand dimensions, variables, units, packing and missing values. See the [help guide](doc/help.html#en-ai) for workflows and examples.

![HDFStudio layer playback and synchronized texture entropy](docs/demo-analysis.gif)

*NetCDF temperature playback beside its synchronized 4×4 texture entropy image.*

## 🚀 Start here

1. Click **Download** above, unzip and run `HDFStudio.exe`; no installer or administrator rights are needed.
2. Choose **File → Open** or drop files onto the window. Expand the tree and right-click a variable for **Properties**, **Data Grid** or **Image View**.
3. Choose **Help → Help** (before About), press **F1**, or open [doc/help.html](doc/help.html) directly for the offline Chinese/English guide.

Keep the executable, matching `hdf_cli.exe`, dependency folders and `doc` together. Basic browsing and native analysis need no Python. Optional NetCDF reports, CF checks and calendar labels use Python with the corresponding `netCDF4`, `numpy` and `compliance-checker` modules; `HDFSTUDIO_PYTHON` selects the interpreter.

## 🌦️ Native GRIB support

- **Browse GRIB1 and GRIB2 directly:** virtual **Variables** group compatible fields; paged **Messages** preserve physical messages and sub-fields, metadata and provenance.
- **Inspect and visualize:** decoded, reconstructed **Quantized X** and paired grids; rectangular-field images, scientific palettes, decoded-value Mask and CSV/PNG export. Quantized X is available only for supported packing and is not a raw-byte dump.
- **Analyze and play:** histograms, curve profiles, whole-plane and 2D texture entropy, comparison metrics, residual maps, integer-index synchronization and layer playback.
- **CLI sections:** arbitrary ordered axis pairs for rectangular GRIB arrays, with missing slots recorded explicitly. GRIB GUI sections, verified geographic-coordinate integration and U/V vectors remain pending.

Unknown names/units remain unknown; missing or out-of-range fields clear prior results. GRIB images currently use source-grid indices. Comparisons require identical plane shapes and align by integer index, without automatic unit conversion, time matching, reprojection or resampling. CF checks do not apply to GRIB.

## 🧰 Everyday tools

| Task | Feature |
|---|---|
| Inspect values | Copyable Properties; decoded/stored/paired NetCDF/HDF5 grids; automatic column widths, cell selection and Ctrl+C |
| View images | Scientific color maps, zoom/pan, flips, decoded-value Mask and full-resolution PNG |
| Inspect distributions | Sampled or full-plane histograms, curve CSV and texture entropy images |
| Compare fields | Pearson r, RMSE, MAE, Bias, `Target − Anchor` residuals and per-target integer lag |
| Browse sequences | Layer controls and Play/Pause; linked views wait for the current data |
| Slice NetCDF/HDF5 | Independent **Space-Time Section (Hovmöller)** with any two axes, fixed-axis navigation and PNG/CSV |
| Arrange the workspace | **Window → Tile / Cascade / Hide Infobar** |

NetCDF/HDF5 sections retain source-index cell spacing and verified coordinate labels. Generic HDF5 stays in index space. CF diagnostics are advisory; unsupported encodings and memory limits are reported explicitly. Details are in the [offline help](doc/help.html).

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

Windows development uses Visual Studio 2022 x64, CMake, Qt 6.8, HDF5, netCDF-C and ecCodes. Dependency locations and targets are defined in `CMakeLists.txt` and `cmake/Grib.cmake`; ecCodes defaults to `ext/eccodes` and can be set with `HDFSTUDIO_ECCODES_ROOT`. Code lives in `src/app`, `src/core`, `src/views` and `src/cli`; tests are in `tests/`. Builds copy the offline guide to `build/output/<configuration>/doc/help.html`.

The public source omits third-party code copies. Restore pinned HDF filter sources with `python tools/26NOV23_v01_restore_hdf_filters.py`; see [Dependencies](docs/DEPENDENCIES.md) and the [project structure](docs/PROJECT_STRUCTURE.md). Runtime libraries and license texts remain in the binary ZIP.

Report reproducible problems in [Issues](https://github.com/DuLiangMin/HDFStudio/issues), with the format, object ID, shape and relevant `probe`/`info`/`props` output. Release history is in [Changelog.md](Changelog.md).

## 📜 License and credits

GNU GPL v3 — see [LICENSE](LICENSE). Derived from Pedro Vicente's [HDF Explorer QT Edition](https://github.com/pedro-vicente/hdf_explorer_qt). Dependency and palette attribution is retained in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `licenses/`.
