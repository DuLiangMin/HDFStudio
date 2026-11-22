# Changelog

## 📈 0.12.10 Independent space-time sections - 2026-11-22

- Independent **View → Space-Time Section (Hovmöller)** and variable context-menu window; arbitrary ordered axis pairs, trusted Lat–Time/Lon–Time presets, fixed index/nearest coordinate selection, flip/zoom/pan, Mask and shared color maps/colorbar.
- Shared native numeric hyperslab decoder, scalar storage-domain missing tests before packing, stable diagnostics and strict worker JSON/48-byte binary protocol. GUI reading is isolated in matching CLI processes, with latest-request cancellation and shared byte-budget leases including chunk working memory.
- Immutable section snapshots drive probes and PNG/CSV exports. Source order, coordinate units and calendar remain explicit; optional bounded Python labels degrade to numeric time. Fixed-axis playback waits for current Ready, including its final frame; sections do not join image comparison/synchronization.
- New unit, independent numeric/RGB, semantics, GUI heartbeat/cancellation and real-desktop checks are maintained under `tests/26NOV22_v01_hovmoller_*`; evidence remains local in `logs/S4-Hovmoller/`.
- Clean Debug/Release builds pass 20 section and 83 existing QtTest checks per configuration, with zero failures/skips and CTest 2/2 each. Independent tests cover every ordered 4D axis pair, source packing/coordinate boundaries and unchanged legacy image planes; PNG pixels and section CSV values/metadata are reconciled separately.
- Keep calendar-label status text and its accessibility name synchronized, including numeric fallback when Python is unavailable.
- Synchronize the application/About version, English and Chinese READMEs, changelog and portable/source packages at 0.12.10. The portable ZIP includes the matching slice-worker CLI, dependency licenses and README-linked demo images for offline reading.
- Replace legacy test fixture filenames with explicit read-only sample environment variables, retaining the numerical and GUI assertions.

## 🖼️ 0.12.08 2D texture entropy image - 2026-11-21

- Add independent **View → Texture Entropy Image** and variable context-menu output, with a real 2D canvas, a fixed 0–4 bits colorbar, zoom/pan, layer navigation, Mask, full-size PNG and numeric CSV exports.
- Compute Shannon entropy in non-overlapping 4×4 blocks using 256 gray levels shared across each decoded plane, then bilinearly interpolate entropy values at actual block centers to the original dimensions. Partial edge blocks use actual samples; source gaps and Mask remain invalid.
- Follow an existing Image View's immutable current masked plane without rereading it; recompute on layer/Mask changes and clear when the source closes. Independent windows read through the shared decoder and reserve the shared raster budget.
- Remove single-image entropy from comparison reports/CSV; explicitly label the earlier scalar as whole-plane histogram entropy. Document the limits of histogram entropy as a texture/possible-information descriptor.
- T1 core acceptance: Debug/Release targeted tests each 5/0/0; 20 planes and 327014 full-resolution entropy values, plus block values, reconciled with NumPy. GUI and final checks are recorded under `logs/S4-TextureEntropy/`.
- Clean Debug/Release QtTest each 83 passed, zero failed/skipped; CTest each 1/1. Full entropy CLI reconciliation covers 22 planes and 3692216 values, including packed data. Desktop tests cover independent images, source following, Mask, native exports, playback/pause and normal shutdown.
- Add a recorded temperature/texture playback GIF in both English and Chinese README files, using cmocean Thermal for temperature and the fixed Viridis entropy scale.
- Fix derived-image shutdown callbacks so closing the main window with source and texture views open exits normally in Debug and Release.

## ▶️ 0.12.07 complete local build (unreleased) - 2026-11-21

- Green bitmap Play/Pause button in the main toolbar and Layer menu, generated offline by `tools/26NOV21_v01_playback_icons.py`; frame interval 100–10,000 ms, default 500 ms. Single-view playback and joint Anchor/Target playback update existing metrics and residual views. Last frame stops; Play at the last frame restarts from the first.
- Disabled for fewer than two frames on the selected playback axis, including empty and singleton axes. In comparison mode the Anchor determines availability. Pause holds position; changing source, axis or binding, closing the playback view or exiting stops the timer safely.
- Fixed Next/Previous/First/Last, Go to Layer and main layer selection on an active synced Target: navigation now advances the Anchor while preserving each Target's lag. Unbound views and Targets with layer synchronization disabled retain independent navigation.
- Visible **Compare & Metrics** entry in the main toolbar as well as the Window menu. Empty axes show **No frames on this axis** with disabled layer selection; comparison controls do not create a negative index range. Layer-signal connection handles preserve safe disconnection while a view is being destroyed.
- Stage evidence and final acceptance are tracked under `logs/S4-Playback/` and `tests/26NOV21_v01_playback_acceptance.md`; 0.12.07 is local and has not been published.
- Clean Debug/Release builds passed the full QtTest suite with 79 passed, zero failed/skipped per configuration, and CTest 1/1 each. Six business-sample hashes and all six final binaries were checked; no layer-signal destruction warnings remain. Real-mouse and portable-program checks are recorded in the local acceptance report.
- Real-mouse playback acceptance passed 21/21 checks in each configuration, including the green bitmap, visible comparison entry, single and joint playback/pause, Next/Previous from an active Target, live entropy/residuals, out-of-range recovery, About version and normal exit. The actual paused forecast pair's metrics were independently reconciled with NumPy; source hashes remained unchanged.

## 🔬 Local analysis additions (unreleased) - 2026-11-21

- 📊 Full-plane 256-bin gray-level Shannon entropy and normalized entropy in image/histogram summaries; immutable decoded/Mask plane snapshots without extra reads on color or histogram display changes.
- 🔗 Independent workspace controller with one Anchor, multiple Targets, checked positive/negative integer leading-axis offsets, layer slider, synchronized zoom/pan/flips and optional colors/resolved range. Out-of-range targets clear and recover; no coordinate-time matching or unit conversion is inferred.
- 📏 Compare & Metrics dock displays Pearson r, RMSE, MAE, Bias (Target−Anchor), finite pair counts and both entropies; copies reports and atomically exports CSV identity/index/revision/unit records. New CLI entropy/corr commands share the core algorithms.
- 🖼️ Live residual maps use Target−Anchor on the finite, currently masked intersection with matching shapes, a zero-centered symmetric diverging scale, zoom/pan and full-resolution PNG export. Derived rasters share the process raster budget.
- 🧪 Implemented sequentially with Debug/Release stage builds, numerical unit tests, NumPy reconciliation and GUI assertions. Final clean builds passed: 75 QtTest checks with zero failures/skips per configuration, CTest 1/1 and three fresh-process timing repetitions per configuration. Two real temperature forecasts passed paired-metric/entropy checks and 437,760 residual values/RGB pixels matched independent calculations. Evidence is recorded under `logs/S4-Metrics-Sync/`; the downloadable release remains unchanged.
- 🖱️ Real-mouse desktop acceptance passed 16/16 checks in both Debug and Release, including lead/lag, live residuals, out-of-range recovery, clipboard, native CSV saving, unchanged source hashes and normal exit. Saved CSV metrics were independently checked against NumPy.

## 0.12.06 - 2026-11-21

- 🧪 Full local regression follow-up: added fixed-seed sampling of nonstandard NetCDF files, CLI error/overwrite tests, 5-D slices, mixed asynchronous layer/Mask/color switching, budget release/recovery and real-mouse desktop checks. Logs remain local under `logs/S4-FullRegression-20261121/`.
- 🛠️ Corrected CLI histogram usage/read error exit codes and unreadable/unknown `probe` exit codes; batch PNG now reserves new PNG/JSON outputs and preserves pre-existing files.
- 🖼️ GUI PNG export now saves the current dataset's full-resolution layer with its mapping, Mask and flips instead of a newly created first-layer widget screenshot. It completes the current cached raster revision before saving atomically.
- 🔎 Cleared stale hover/colorbar tooltips when display input becomes invalid. Product version remains 0.12.06; no remote publication is part of this regression.
- ✅ Local regression checks: clean Debug/Release builds; QtTest 68 passed, 0 failed, 0 skipped per configuration; CTest 1/1 per configuration; three fresh-process timing repetitions per configuration. Twelve real samples per configuration matched all 13,652,673 CSV values and PNG pixels, all histogram bins, and 348 metadata items; 17 additional CLI edge cases passed per configuration. Detailed local evidence and remaining scope limits are recorded under the regression log directory.
- 🔢 Raised the product version from 0.12.03 to 0.12.06 in CMake, application metadata and the About dialog; synchronized both READMEs.
- 🎨 Includes the scientific color maps, shared colorbar and cached recoloring implemented since 0.12.03, together with the visible image zoom controls and completed regression cycles documented below.
- ✅ Rebuilt Debug/Release at 0.12.06: each full QtTest suite passed 47 tests with zero failures and two optional external-sample skips; CTest passed 1/1 in both configurations. Both executables report `HDFStudio 0.12.06` with `--version`.

## 0.12.03 visible image zoom - 2026-11-21

- 🔍 Added visible magnifying-glass **Zoom in / Zoom out** buttons at the start of the second image toolbar, beside the existing **Fit** action. Removed the duplicate `+`/`-` actions from the crowded first toolbar.
- 🖱️ Buttons scale around the view center by 1.25× / 0.8×; the wheel scales around the pointer using its vertical delta. Zero/horizontal wheel events do not accidentally shrink the image. Scale is clamped to the existing 0.002..2048 limits, preserving flips, curve objects, profile values, cached data and raster pixels.
- 🧪 Added zoom tests for real Qt button clicks at three window widths, four flip combinations, wheel/button anchoring, 100 inverse cycles, both scale limits, Fit/Reset and unchanged plane-read/render counts. `tests/run-zoom-regression.ps1` builds/runs full Debug/Release QtTest/CTest, then five full-suite cycles per configuration; `tests/desktop_zoom.ps1` measures colored-image bounds after real Windows mouse clicks. Reports: `logs/S4-UI-Zoom/`.
- ✅ Debug/Release builds passed; each complete QtTest run passed 47 tests with zero failures and two optional external-sample skips, and CTest passed 1/1. Five additional full-suite cycles per configuration passed with the same totals in all ten runs.
- 🖥️ Real desktop acceptance passed 9/9 in each configuration: image bounds grew from 207×162 to 259×202 and returned to 207×162; five zoom pairs after a horizontal flip also restored the size. The input sample SHA256 stayed unchanged. Captured screenshots were visually reviewed.

## 0.12.03 CMaps implementation - 2026-11-21

- 🎨 Added 13 offline scientific palettes: standard BIDS Viridis, six cmocean maps, Crameri Batlow and five NCL tables. The five original sparse palettes remain as legacy choices. `Projs` keeps geographic lookup; `CMaps` has strip previews and a compact options button.
- 📐 Added searchable palette details, continuous/stepped/centered/cyclic mapping, explicit boundaries and period, reverse, custom under/over colors and local RGB import. Image and colorbar share the numerical mapping; palette names do not define units or meteorological thresholds.
- ⚡ Cached immutable decoded planes; display changes do not reread data, masks derive from the unmasked cache, and profiles share the plane. Background recoloring is bounded and discards obsolete requests. Existing curve items, zoom and flips survive recoloring; image/colorbar apply together. Raster memory reservations are configurable and survive until cancelled work exits.
- 🔎 All-missing/all-masked planes retain their state colors; source missing, user Mask and real values have separate hover text. Non-sorted flag values retain their matching meanings. Added `hdf_cli cmap` and `rgb` using the same core mapping/parser.
- 🖼️ Existing `--preview` batch PNG export waits for a complete raster, including planes above the interactive asynchronous-render threshold; an integration test checks the exported size and endpoint pixels.
- 📜 Preserved source data, pinned versions, SHA256, license texts and attribution; extended packaging to include them. Exact-byte Git attributes keep resource hashes stable across Windows checkouts. Runtime requires neither Python nor NCL.
- ✅ Debug and Release: 46 passed, 0 failed, 2 optional external-sample tests skipped; CTest 1/1 each. Real-mouse desktop checks: 7/7 in each configuration. Resource export `--check` reproduces all 13 palettes byte for byte. Tests cover native nodes, boundary/center/cycle behavior, missing/Mask, cached IO counts, overlay preservation, latest-request wins, colorbar pixels and budget refusal.

## 0.12.03 toolbar labels - 2026-11-21

- 🎨 Image toolbar: shortened `Geographic coordinates` to `Projs` and added a `CMaps` label immediately before the existing palette selector, with a tooltip explaining its color-map purpose. Existing coordinate lookup and the five palettes retain their behavior.
- 📝 New palette resources, including standard Viridis, remain planned; this change introduces the requested toolbar labels only.

## 0.12.03 - 2026-11-20

- 🖼️ **Image View Flip V / Flip H / Reset**: added three buttons on the left of the second toolbar (`maskBar`) with custom 24×24 icons (`flip_vertical.png`, `flip_horizontal.png`, `reset_home.png`). **Flip V** and **Flip H** keep their pressed state (click to flip, click again to restore); **Reset** (home icon) unconditionally clears both flips and restores original zoom and position (`fitInView`). Zooming remains valid when horizontally flipped (`|m11|`), and `Fit` preserves the active flip state.
- 📈 **Curve Profile**: added `Profile` (checkable point-picking mode), `Show Profile`, and `Clear` buttons on `maskBar`. Left-click adds control points (compatible with zoom, pan, and flips); right-press on a point drags/moves it; double-click on a point deletes it; toggling `Profile` off hides the curve while keeping points, and toggling it back on restores the curve; `Clear` erases all points and the curve. Catmull-Rom spline / polyline arc-length sampling (`src/core/curve_profile.*`) extracts bilinear or nearest-neighbor values from the decoded/masked plane into `ProfileView` (`src/views/profile_view.*`), with `NaN` gaps and CSV export.
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
