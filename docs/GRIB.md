# 🧩 GRIB integration contract

## 📂 Inputs and outputs

Read-only sources: user-selected GRIB1/GRIB2, generated fixtures in `testdata/grib/`,
optional `HDFSTUDIO_GRIB_SAMPLE` / `HDFSTUDIO_GRIB_CORPUS_DIR`.
External business acceptance uses `HDFSTUDIO_GRIB1_SAMPLE` / `HDFSTUDIO_GRIB2_SAMPLE`;
the files remain outside the repository and are never modified.
Development dependency: `ext/eccodes/`, overridden by CMake `HDFSTUDIO_ECCODES_ROOT`.
Application/CLI output: `build/output/{Debug,Release}/`; validation: `logs/S4-GribIntegration/`.
Session files use the application temporary directory, never the source directory.
Exports go only to a user-selected destination.

## 🔌 G0: data contract

`SourceRef` identifies one immutable file session; `ObjectRef` identifies an object in that
session. GRIB field locators retain physical offset, length and sub-field index. Display
names are not identities. `ArrayInfo` describes source axes and explicit capabilities.
`PlaneData` carries row-major decoded doubles, optional reconstructed quantized X,
coordinate state, source revision and a lifetime lease. Native HDF integer/string tables
retain their existing typed path.

Shape is `(leading axes..., y, x)` for rectangular source grids, `(point,)` for reduced
grids, and `(coefficient,)` for spectral data. A point table does not imply a raster.
Missing fields, source missing values and user Mask are distinct states.

## 🔬 Scientific rules

Keep centre/table/parameter provenance, both layer surfaces, reference cycle, interval,
step units and ensemble identity. Preserve duplicates and missing slots. Unknown product
templates remain individually browsable and are not automatically combined. Coordinates
are optional and verified; no automatic reprojection or geographical flips.
GRIB decoded values do not receive CF scale/offset a second time. Quantized X is a
validated reconstruction, not a packed-byte dump. Unknown units and finite sentinel
meanings are never guessed.

## 🪜 Delivery sequence

G1 backend and CLI → G2 tree/table/image → G3 shared analyses → G4 sections/vectors →
G5 mixed-format regression and portable deployment. The local 0.14.04 development version includes
G1–G3, the G4 CLI/worker section reader and independent GUI sections, plus offline help.
Verified geographic coordinates, vector integration and G5 remain separate gates.
The pre-merge local checkpoint is
`4cb279731971a2adbeff3cc47af87047f6420b26`.

Each stage records its actual checks and limitations in the workspace design ledger.
No GitHub publication is part of this integration task.

### 📈 GUI section display

Rectangular variables and fields expose **Space-Time Section (Hovmöller)** in View and
the context menu. Reads use the same isolated CLI worker with schema 2 / GribDecodedV1;
the application reserves output, coordinate and source-decoding working memory before
launching the worker. Any ordered axis pair is supported; fixed axes retain navigation,
latest-request cancellation and playback. Missing slots stay NaN.

Grid Y–Time index / Time index–Grid X presets use the catalog's reference-plus-step axis
and retain its complete original labels in probes, fixed-axis labels and export metadata.
These are index-space sections; they do not infer verified elapsed-time or geographical
coordinates. Fit stretches display axes independently with smooth image interpolation.
Numeric snapshots, probes, CSV and native-resolution PNG preserve source samples.

### 🧪 Small-step gate

Add one feature at a time, pass its Debug and Release unit tests and directly affected
regressions, then commit locally before starting the next feature. G2 starts with the
read controller, then tree/properties, table and image as separate gates.

`DataReadController` accepts a read-only `QString` source, object ID and zero-based
leading indices; it publishes an immutable catalog or decoded `PlaneData` plus a shared
application-budget lease. Each controller keeps one active and one latest pending
request, discards cancelled generations and drains before deferred deletion. Consumers
retain both the plane and budget while using a result. GRIB tasks and section processes
share two admission slots through `ReadScheduler`; ecCodes itself remains serialized.
Only the GRIB backend runs in these worker threads. NetCDF/HDF sections continue to use
the separate CLI process. This controller gate does not yet add a product browsing UI.

### 🌳 Tree and properties gate

Native GRIB sources expose virtual `Variables`, paged physical `Messages` and file
diagnostics. Expanding one page creates at most 256 physical messages, retaining every
sub-field locator. Pages are bound to a catalog session, including when several files
are open. Axes show source labels and zero-based indices; long label reports are bounded.
Variable properties identify representative-field details and varying keys explicitly.
GRIB keys may be encoded or computed ecCodes accessors; CF checks are not applicable.
Indexing is cancellable before any tree is published. Closing the source releases its
catalog session. This gate exposes Properties; tables and analyses follow their own tests.

### 📊 Data Grid gate

The GRIB grid uses the existing `AnalysisTableView` for bounded column sizing, selection
and TSV copy. Decoded, reconstructed Quantized X and side-by-side modes use the same
read-only plane revision. Unsupported integer reconstruction displays `unavailable`;
source missing values display `nan`. Leading indices navigate virtual arrays; a missing
field clears the table and disables export immediately. The table retains its application
budget lease. CSV is committed atomically, refuses source overwrite, and records source
session/object, indices, units, decoding profile, missing and quantized semantics. This
gate does not add GRIB image or analysis views.

### 🧪 Optional read-only GRIB1 corpus

`HDFSTUDIO_GRIB1_SAMPLE` selects an external GRIB1 sample for
`tests/26NOV23_v01_grib_grid_verify.py`. The sample stays outside the repository.
The verifier checks every message boundary and edition, creates reference cases in its
log directory, and provides `HDFSTUDIO_GRIB1_CASES` to the optional QtTest. Selected
`regular_ll` / `grid_simple` fields without bitmap are also decoded by a small NumPy
bitstream parser independent of ecCodes, then compared cell by cell with GUI CSV and
one CLI CSV. Unsupported reference packing/grid combinations fail explicitly; the
reference parser is a test oracle for this stated subset, not a product reader.
Catalog pagination, leading-axis navigation, source hash preservation and close are
included. Other test data is described in `testdata/grib/manifest.json`.

### 🖼️ Image gate

Rectangular GRIB fields and virtual arrays use the existing `RasterView` render path.
Asynchronous reads retain the decoded plane and shared budget; layer changes immediately
invalidate the previous snapshot. Palette and Mask changes reuse the decoded plane.
Images use source-grid indices unless a saved projection is restored. The visible **Map projection**
selector redraws immediately after separable regular_ll coordinates and the current source's earth model
are validated. The single-row selector redraws without OK; **Options…** opens live detailed settings with OK/Cancel. See [map display](PROJECTION.md).
For ecCodes 2.47 alternating scans, coordinate arrays only need j-consecutive transposition,
while decoded values retain the original source-index mapping. PNG export waits for the completed selection
and honors the existing horizontal/vertical flip contract. Batch preview accepts GRIB
object IDs, for example `--preview /field/0 --png <new-output.png> <source>`.
`tests/26NOV23_v01_grib_image_verify.py` compares sample raster doubles with the independent
GRIB1 references and GUI PNG with batch PNG, and runs the affected HDF/NC image regression.

### 🧪 Optional complex-packing GRIB2 corpus

With `HDFSTUDIO_GRIB2_SAMPLE`, the image verifier also creates
`HDFSTUDIO_GRIB2_CASES` and raw references under its evidence directory.
`tests/26NOV23_v01_grib2_complex_reference.py` independently parses message/section
boundaries, group references/widths/lengths, primary internal missing values and
second-order spatial differencing from the GRIB2 bitstream without ecCodes.
It explicitly accepts only regular latitude/longitude grid template 3.0, scan mode 0,
packing template 5.3/7.3, general group splitting and no bitmap. Unsupported cases fail.
All fields are compared through CLI CSV, GUI table CSV and raster doubles; selected
GUI/batch PNG pairs are compared pixel by pixel. Unknown metadata remains unknown.
The independent decimal rescaling can differ from ecCodes by double rounding:
missing/zero positions must match exactly and nonzero reference values allow only
eight double machine epsilons of relative error, with zero absolute tolerance.
The three product paths must still agree exactly; per-field errors are recorded.

### 📊 Histogram gate

`Histogram...` on a rectangular GRIB field or virtual variable opens the existing
histogram window. Reads use the shared background scheduler and retain the decoded
plane's budget lease; a new layer immediately clears the previous result. Missing
fields, changed sources and memory limits show an error rather than stale statistics.
Changing the bins or `Sample 1 in N` only resamples the cached decoded values. Unknown
units remain unknown; the independent histogram does not inherit an image's Mask.
`tests/26NOV23_v01_grib_histogram_verify.py` runs the focused dual-configuration gate
and compares business-field statistics with the independent bitstream references.

### 🖼️ Texture entropy gate

`Texture Entropy Image` reuses the 4x4 / 256-level algorithm. Independent GRIB
windows read decoded planes asynchronously and reuse the cache for Mask changes;
following windows consume the current Image View snapshot and Mask. Missing fields
and changed sources invalidate the result and disable export. Source units are
preserved separately from output entropy in bits. The focused verification script
is `tests/26NOV23_v01_grib_texture_verify.py`; evidence remains under
`logs/S4-GribIntegration/` and external samples are read-only environment inputs.

### 📈 Gray entropy and curve profiles

GRIB Image View uses the existing 256-bin whole-plane entropy and index-space
curve sampling. Curves follow decoded values, layers and the image Mask; missing
fields clear the curve and disable CSV export. A curve retains the plane's shared
memory lease until its data is replaced or released. Focused verification uses
`tests/26NOV23_v01_grib_profile_verify.py` and the desktop script's `-Profile` mode.

### 📊 Index comparisons and residuals

Compare & Metrics consumes the same immutable, masked GRIB image planes as the
existing HDF/NetCDF path. Pairing is by array index; grid/time identity conflicts
and unknown or different units are reported without conversion or resampling.
Missing fields invalidate metrics and the residual PNG immediately. Comparison
CSV and residual PNG exports reject source paths, including file aliases.
Verification uses `tests/26NOV23_v01_grib_compare_verify.py` and `-Compare` desktop mode.

### ▶️ Index synchronization and playback

GRIB images reuse the Anchor/Follower integer-index synchronization and layer
playback controls. Leading axes retain their source labels; lag counts index
steps, without converting times or matching physical coordinates. Playback waits
for current reads/renders, and missing or out-of-range fields clear prior data.
Data Grid playback also finishes when the last requested slot is missing; data
and CSV remain unavailable for that slot. Objects without a multi-frame leading
axis disable Play. Pausing holds the current indices; closing the source stops
playback. Focused checks use `tests/26NOV23_v01_grib_playback_verify.py` and the
desktop script's `-Playback` mode.

### 📈 Arbitrary-axis CLI and worker sections

`hdf_cli slice <source> <object-id> <row-axis> <column-axis> <new-csv>
<fixed-index-per-axis...>` reads any ordered pair of axes from a rectangular GRIB
field or virtual array. Indices are zero-based. The output is row-major decoded
float64; unavailable cube slots contribute NaN and an explicit `missing_fields`
count. Original leading-axis labels accompany index axes. This first section gate
uses generic source indices; geographic and valid-time presets require separate
coordinate verification.

The isolated `slice-worker` protocol accepts GRIB only with `schema=2`,
`source_kind="grib"` and `decode_profile="GribDecodedV1"`. Its `HDFSLICE` binary
header uses version 2; the JSON envelope, binary version, request ID, dimensions
and payload length must agree. Existing HDF/NetCDF schema 1 and `DecodeProfileV1`
remain supported. Each task scans its source once and records catalog-scan,
read/decode and total worker timings; large arrays stay in the binary transfer.

Sections decode one source plane at a time. Admission checks the output reservation
(64 bytes per output sample), largest source plane (128 bytes per source sample),
twice the source message bytes and 16 MiB overhead against a 512 MiB task limit.
The output retains its lease in the existing GRIB process budget. These estimates
are admission limits rather than an operating-system RSS guarantee. Source files
and aliases cannot be overwritten by CSV export. Focused dual-configuration and
independent-bitstream checks use `tests/26NOV23_v01_grib_slice_verify.py`.
