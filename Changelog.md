# Changelog

## 0.10.27 - 2026-11-20

- Fixed: NetCDF-3 files (CDF-1 classic, CDF-2 64-bit offset, CDF-5 64-bit data) could not be opened ("Cannot open as HDF5 / NetCDF-4"). They have no HDF5 container; HDFStudio now detects them from the file header and reads them through netCDF-C: dataset tree with global attributes, grid, image view, Properties and text export. NetCDF-4 and HDF5 files are read exactly as before.
- NetCDF-3 grids read only the displayed 2-D plane and re-read on layer change: a 697 MB 4-D variable opens in about 0.1 s with a 53 MB peak working set.
- Fixed: NetCDF files under a Chinese (non-ASCII) path could not be read by netCDF-C on Windows; paths are now passed in the local code page.
- Properties: dimension names, unlimited dimension and, for NetCDF-3, attribute values in the netCDF4-python style (numeric arrays expanded, shortest float32 form).
- New `hdf_cli` commands `probe`, `info`, `props`, `export` (plus the existing `csv`); they call the same code as the GUI and are shipped in the Windows zip.
- Validation: Debug/Release QtTest 31 passed + 1 optional real-sample test (passes when `HDFSTUDIO_CLASSIC_SAMPLE` is set), CTest 1/1; grid plane, decoded time column, image min/max and `hdf_cli info` match Python netCDF4; real-mouse desktop check on the 697 MB sample.

## 0.10.26 - 2026-11-20

- Added Close File / Close All Files (File menu, Ctrl+F4 and Ctrl+W) and a tree context-menu Close File; `Close` renamed `Close View`. Views of a closed file are destroyed first, then tree nodes and data; the info tabs return to `No file opened.` after the last file.
- Command-line start opens every listed file.
- Regression `closeFileRemovesItsViewsAndReleasesTheFile` (two files, grid and image views, `QFile::remove` after close, Close All). Real-mouse desktop check passed.
- README rewritten with screenshot, badges, comparison table and roadmap.
- Validation: Debug/Release QtTest 29/29, CTest 1/1. Reports: `logs/S4-UI-CloseFile/`.

## 0.10.25 - 2026-11-20

- Startup waiting box slogan changed to "HDFStudio / Unleash Your NetCDF Power."; the splash regression now compares the exact text.
- Lessons from 0.10.23-0.10.24 (QSplashScreen wait, time decoding and reconciliation, local build pitfalls) recorded in `AGENTS.md`.
- Validation: VS 2022 x64 Debug/Release, QtTest 28/28 and CTest 1/1 per configuration. Reports: `HDFStudio/logs/S4-UI-Slogan/`.
- Release preparation: full GPL-3.0 text in `LICENSE`, `THIRD_PARTY_NOTICES.md` (Qt LGPL-3.0, netCDF-C, HDF5, zlib, VC++ runtime), Windows download and build notes in `README.md`, and `tools/package-windows.ps1` producing the `-win64.zip` and `-src.zip` release assets.
- Public README rewritten in English (features, download, Python requirements, scope, credits to pedro-vicente/hdf_explorer_qt, search keywords).

## 0.10.24 - 2026-11-20

- Grid view of a one-dimensional `UNIT since DATE` variable adds `yyyymmdd` and `yyyymmdd hh:mm:ss` columns decoded by `netCDF4.num2date` with the declared calendar (`netcdf_report.py dates`, asynchronous); other variables keep a single numeric column.
- Grid doubles use `%.15g`, so epoch seconds such as `915170400` are exact instead of `9.1517e+08`.
- Regression `timeGridShowsDecodedDatesBesideRawValues` drives the real double-click path on the no_stand_nc tmp2m sample and dumps both tmp2m tables; `tests/reconcile_time_grid.py` matched all 480 + 180 rows against an independent netCDF4 decode.
- Validation: Debug/Release QtTest 28/28, CTest 1/1. Reports: `logs/S4-UI-TimeGrid/`.

## 0.10.23 - 2026-11-19

- Replaced the startup `QSplashScreen` with a plain frameless top-level widget. Qt 6 `QSplashScreen::event()` handles `QEvent::Show` by spinning in `waitForWidgetMapped()` for up to 1000 ms (10 ms `Sleep` per round). That mapped check never succeeded from inside the handler here, so `show()` always burned the full cap and the box was closed before painting. The plain widget is visible at 37 ms, and a plain top-level widget maps in 135 ms in the same process, so the lost second was the wait itself, not window creation.
- The box also carries `Qt::WindowStaysOnTopHint`, since `WA_ShowWithoutActivating` alone placed it below the active window, and it is held for a 600 ms minimum so the message is readable.
- Added `create_startup_splash()` and regression `startupSplashIsPlainVisibleWidgetNotQSplashScreen` (object name, no `QSplashScreen`, top-most flag, non-activating attribute, two label texts).
- Validation: Debug/Release QtTest 27/27 and CTest 1/1 per configuration; window enumeration probe confirmed the box is the top visible window of the process from 220 ms to 420 ms. Reports: `logs/S4-UI-SplashFix/`.

## 0.10.22 - 2026-11-19

- Added a single-line startup splash and on-demand Python netCDF4 and CF-1.8 reports in the resizable information panel. The time menu decodes the selected variable with its declared calendar.
- Regression coverage includes the business sample's axis order, noleap dates and CF checker result. Logs: `logs/S4-UI-InfoTabs/`.

## 0.10.21 - 2026-11-19

- Bottom file information panel is now the lower page of a vertical `QSplitter` (`mainSplitter`) whose 8 px handle resizes the panel by a normal drag; children are non-collapsible and the panel keeps a 150 px initial height.
- Panel content is a read-only `QPlainTextEdit` (`fileInfoText`) in a fixed-width font with wrapping off, showing Name, Path, Format, Size and Modified lines; the format detail stays available as a tooltip.
- Regression `bottomFileInfoPanelShowsCurrentFileMetadata` now drives the splitter handle with synthetic press/move/release events and requires the panel to grow and shrink by the dragged amount, instead of only comparing dock heights.
- Validation: VS 2022 x64 Debug/Release, QtTest 25/25 and CTest 1/1 in each configuration; desktop check with the read-only NetCDF sample, where real mouse drags on the handle moved the panel boundary by 140 px and 110 px. Reports and screenshots: `logs/S4-UI-FileInfo-TextPane/`.

## 0.10.20 - 2026-11-19

- Added a bottom file information dock with filename, full path, format, size, and modification time; updated on successful open.
- Validation: Debug/Release QtTest 25/25 and CTest 1/1 in each configuration; contents and vertical resizing verified with a read-only NetCDF sample (`logs/S4-UI-FileInfo-Resizable/`).
- Dock has no fixed height and remains vertically resizable from the main-window splitter.

## 0.10.18 - 2026-11-19

- Added a NetCDF format probe and documented the verified format/GUI capability boundary.
- Added a non-blocking CF-1.8 diagnostic report for a limited metadata rule subset.
- Debug/Release: QtTest 24/24, CTest 1/1 per configuration; diagnostic report dialog tested on a read-only sample. Independent valid/invalid CF fixtures checked with IOOS Compliance Checker 6.1.0.

## 0.10.16 - 2026-11-19

- Added a bounded UGRID v1.0 triangular face preview and face-value inspection.
- VS 2022 x64 Debug/Release: QtTest 21/21 and CTest 1/1 per configuration (`logs/S4-R08/`).

## 0.10.14 - 2026-11-19

- Added strict CF 1D latitude/longitude matching and geographic mouse coordinate lookup for supported grids.
- Unknown or projected coordinate systems remain index-only; no projection is guessed.
- Validation: Debug/Release builds passed; QtTest 14/14 in each configuration. Reports are in ignored `logs/0.10.14/`.

## 0.10.12 - 2026-11-19

- Added synchronized U/V reads and vector preview with dimension identity validation, missing-pair handling, and thinning.
- Validation: Debug/Release builds passed; QtTest 13/13 in each configuration. Reports are in ignored `logs/0.10.12/`.

## 0.10.10 - 2026-11-19

- Added chunked numeric dataset text export with row formatting, progress/cancel callback, and atomic output integration.
- Kept numeric attribute export with size guards; unsupported dataset types are rejected.
- Validation: Debug/Release builds passed; QtTest 12/12 in each configuration. Reports are in ignored `logs/0.10.10/`.

## 0.10.8 - 2026-11-19

- Expanded object Properties with storage/filter metadata and bounded attribute previews.
- Added HDF5 fixture checks for root/group and dataset inspection, dtype, shape, element/byte counts, chunking, deflate, units, and long-value truncation.
- Validation: Debug and Release builds passed; QtTest passed 11/11 in each configuration. Reports are in ignored `logs/0.10.8/`.

## 0.10.6 - 2026-11-19

- Added discrete raster mode, category labels from `flag_values`/`flag_meanings`, stable colors, and explicit missing/unknown category colors.
- Added a 12-category preview limit with visible overflow feedback.
- Validation: Debug/Release builds passed; QtTest passed 9/9 in each configuration. Reports are in ignored `logs/0.10.6/`.

## 0.10.4 - 2026-11-19

- Added automatic/fixed continuous raster range controls, range validation, and fixed-range legend indication.
- Added range tests for automatic, fixed, constant, all-missing, and invalid-bound cases.
- Validation: Debug and Release builds passed; QtTest 8/8 in each configuration. Reports are in ignored `logs/0.10.4/`.

## 0.10.2 - 2026-11-19

- Added continuous Viridis, Plasma, Inferno, Turbo, and Grayscale raster palettes with reversal control.
- Kept missing values gray and color mapping independent from the raster values and slice selection.
- Added palette interpolation, reversal symmetry, clamping, and missing-color tests.
- Added configuration-aware deployment of Qt platform plugins and QtTest runtime DLL.
- Validation: VS 2022 x64 Debug and Release application/test targets built; QtTest passed 7/7 in both configurations. Reports are in ignored `logs/0.10.2/`.
