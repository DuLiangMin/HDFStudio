# Changelog

## 0.12.03 - 2026-11-20

- 🖼️ **Image View Flip V / Flip H / Reset**: added three buttons on the left of the second toolbar (`maskBar`) with custom 24×24 icons (`flip_vertical.png`, `flip_horizontal.png`, `reset_home.png`). **Flip V** and **Flip H** keep their pressed state (click to flip, click again to restore); **Reset** (home icon) unconditionally clears both flips and restores original zoom and position (`fitInView`). Zooming remains valid when horizontally flipped (`|m11|`), and `Fit` preserves the active flip state.
- 📈 **Curve Profile**: added `Profile` (checkable point-picking mode), `Show Profile`, and `Clear` buttons on `maskBar`. Left-click adds control points (compatible with zoom, pan, and flips); right-press on a point drags/moves it; double-click on a point deletes it; clicking `Profile` a second time (toggling off) clears the curve. Catmull-Rom spline / polyline arc-length sampling (`src/core/curve_profile.*`) extracts bilinear or nearest-neighbor values from the decoded/masked plane into `ProfileView` (`src/views/profile_view.*`), with `NaN` gaps and CSV export.
- 📸 Updated `docs/screenshot-main.png` from the active window.
- ✅ Debug and Release QtTest 41 passed + 2 optional, CTest 1/1 (`S4-UI-CurveProfile`).
- 🔢 Version 0.12.03 (CMake project version, `setApplicationVersion`, About dialog).

## 0.12.02 - 2026-11-20

- 📊 **Histogram of one layer**: right-click a variable with at least two dimensions → **Histogram...**. The window reads the selected layer once, then shows a bar chart and one summary line (values sampled, bins, min, max, mean, standard deviation, NaN cells). **Sample 1 in N** defaults to 20 and draws a **uniformly random** subset of exactly `round(finite / N)` values — not every Nth cell — so a 1201 × 1401 plane answers in about 70 ms; `1` uses every value and gives the exact extremes. Changing the step or the bins re-samples values that are already in memory (the file is not read again), and **Draw a new sample** takes another subset of the same size. A seed keeps the bars reproducible. Layer spin boxes implement `LayerNavigation`, so the main Layer toolbar drives the histogram too. `hdf_cli hist` prints the same lines.
- 🧭 New source layout: `src/app` (main window, layer navigation, iterate, visit), `src/core` (readers, properties, text export, `plane_reader`), `src/views` (raster, vector, mesh, histogram), `src/cli`, `resources/`, `docs/`. `include_directories` keeps plain `#include "nc_reader.hpp"`, so files moved between these folders need no edit at the call sites.
- 🧪 Race to the truth: an intermediate version read the plane with a stride (`nc_get_vars_double` / `H5Sselect_hyperslab`) to touch only 1/20 of the data. Measured cold-cache 63 ms against 80 ms, but a regular grid is not a random sample and its products aliased with the 4 × 5 split (1/400 of the values). The strided path was removed: the plane is read once, then sampled in memory at random, which is both faster and statistically sound.
- 🧪 Validation: `nc_histogram` unit cases (exact sample size `round(finite / N)`, same seed → identical counts, another seed → same size / different counts, NaN counted as missing, max in the last bin, constant plane → one bin, fractions 0 and > 1 refused, packed fixture decoded 0..18). `tests/reconcile_histogram.py` against Python netCDF4 on the CMPA FAST 0.05° product: full plane 1,682,601 finite, 32 bins, **0 mismatched**; 1 in 20 → 84,130 = round(1,682,601 / 20), bars sum to the sample, mean off by 1.5 against an allowed 5 s.e. of 57.2; two runs of the same seed byte-identical; the sample file's size and mtime unchanged.
- ⏱️ Timing (`hdf_cli hist`, Release, warm file cache, 1,682,601 cells): 1 in 20 → 69.4 ms, 1 in 1 → 91.7 ms.
- 📦 Fix found by the move: `resources/images/*.png` had never been tracked because `.gitignore` ignores `*.png`, so earlier source zips shipped without icons. The rule now has `!/resources/images/*.png`.
- ✅ Debug and Release QtTest 39 passed + 2 optional (the sample-driven ones are skipped without `HDFSTUDIO_PACKED_SAMPLE`), CTest 1/1; report in `HDFStudio/logs/S4-UI-Histogram/`.
- 🔢 Version 0.12.02 (CMake project version, `setApplicationVersion`, About dialog).

## 0.10.30 - 2026-11-20

- Image View **Mask Range**: a second toolbar row with start and end fields. Decoded values in [start, end] (inclusive) become NaN, are drawn white and are left out of the color range and min/max; an empty start is −∞, an empty end +∞. The file's own `_FillValue`/`missing_value` stay grey. Bounds are decoded values (`stored × scale_factor + add_offset`), never stored integers.
- Properties and `hdf_cli props`: new lines stating the decoding and giving `valid_range`/`valid_min`/`valid_max`/`_FillValue`/`missing_value` in decoded units next to the stored values, e.g. `valid_range decoded: [0, 9999] (stored [0, 99990000])`.
- New `hdf_cli mask <file> <path> <start|-> <end|-> [index ...]`: same masking as the Image View; prints counts and min/max of the remaining values.
- Drag and drop: a local file dropped anywhere in the window now opens, including on text fields that used to take the path as text. When HDFStudio runs as administrator, the status bar explains that Windows blocks drag and drop from Explorer.
- **Three grids instead of one** (right-click → the menu entries are now separate): **Data Grid (decoded to real)...** applies `scale_factor`/`add_offset`, **Stored Grid (raw value)...** shows the numbers as written in the file, and **Both Grids (decoded and raw)...** puts the decoded column next to its raw column in one table, so a value and the stored number it came from are read in the same row. Headers say which number they hold (`Value (decoded to real)`, `Value (raw)`, `Value 1 (decoded)` / `Value 1 (raw)`), and the window title repeats it. The previous single **Grid...** always showed stored values, which is why the grid of a packed product disagreed with its image view.
- Decoding now starts from the stored number, not from its formatted text: `float32` 123456789 used to be shown as `1.23457e+08` in the text (6 significant digits) and, if such text were decoded, would give 308642400 instead of 308641880.
- Validation: three grids reconciled cell by cell with Python netCDF4 on the CMPA FAST 0.05° product (1201 × 1401): decoded, raw and both grids 1,682,601 cells each, 0 mismatched (`tests/reconcile_decoded_grid.py`); open 35 ms / 8 ms / 9 ms, full-table read 933 ms / 510 ms / 1499 ms.
- Validation: Debug/Release QtTest 38 passed + 2 optional; new `maskRangeUsesDecodedValuesWithOpenEnds`, `decodedAndStoredGridsShowScaleFactorAndOffset`, `bothGridsShowDecodedAndRawSideBySide` and `fileDropOpensFromEveryAreaOfTheMainWindow`; real-mouse `tests/desktop_mask_range.ps1` 5/5 on a CMPA FAST 0.05° product (1,470,269 values masked, remaining max 6.4329, equal to Python netCDF4).

## 0.10.29 - 2026-11-20

- Properties dialog improved for extracting metadata: right-click → **Properties...** now opens a non-modal window with selectable plain text (`Ctrl+C`, right-click Copy/Select All) and a **Copy All** button. The report starts with `HDFStudio <version> Properties`, `File:` (full path) and `Object:` (path in the file), then shape, type, size, dimensions, storage layout and all attributes: the same lines as `hdf_cli props`. It can be pasted as-is into a search, an issue or an LLM prompt.
- Window menu: **Cascade** arranges the views in creation order, each 2/3 of the area, offset by one title bar, with the active view kept in front; minimized or maximized views are restored first. Below Cascade, Tile and Close All Views is a numbered list of open views (current one checked); choosing an entry activates that view.
- Fixed: closing the main window could call a slot on an already-destroyed `MainWindow` (Qt debug assertion) because the MDI area still emitted `subWindowActivated` while its views closed.
- README (English and Chinese): the Properties report and the Window menu; new screenshot showing the layer toolbar.
- Validation: Debug/Release QtTest 34 passed + 1 optional, CTest 1/1; new `propertiesDialogCopiesReportAndWindowMenuListsViews`; 5 consecutive full Debug runs passed. Real-mouse script `tests/desktop_properties_copy.ps1` 8/8 on the 697 MB NetCDF-3 sample (clipboard contains every `hdf_cli props` line).

## 0.10.28 - 2026-11-20

- Added the old HDF Explorer layer buttons to the main toolbar, right after Open: Next (up), Previous (down), Last (double up), First (double down), then a dimension picker (`time (60)`, `lev (100)`, …), a layer picker and a `Layer n of N` readout. A new **Layer** menu has the same commands plus **Go to Layer...** (`Ctrl+G`); shortcuts `Alt+Right`, `Alt+Left`, `Alt+End`, `Alt+Home`.
- They act on the active grid or image view and follow it when another view is activated or the view's own selectors are used; disabled when the view has no layers. Dimension names come from netCDF-C.
- Grid: one plane read per step (the old Next/Previous handlers read it twice).
- `README_CN.md` (Chinese); `README.md` stays English with a language switch at the top. Both are in the Windows zip.
- Validation: Debug/Release QtTest 33 passed + 1 optional, CTest 1/1; new `layerStepTargetMatchesOldHdfExplorerButtons` and `mainToolbarStepsLayersOfTheCurrentView` (toolbar clicks, combos, Go to Layer dialog, view switching); real-mouse desktop script `tests/desktop_layer_toolbar.ps1` 13/13 on a 697 MB NetCDF-3 sample, values equal to Python netCDF4.

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
