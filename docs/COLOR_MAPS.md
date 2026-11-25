# 🎨 Color maps in HDFStudio

The image toolbar contains `CMaps [palette] … Reverse`. A separate **Map projection**
single-row bar above it redraws the image as soon as a projection is selected, without OK; its **Options…**
button opens the detailed controls described in [Projection display](PROJECTION.md).

## 🌈 Palettes and mapping

The original five palettes remain at the start of the selector as **legacy**
compatibility options. **Jet** is the default spectrum palette for new raster
images, Space-Time Sections and texture entropy images. **Rainbow** is also
available in CMaps. Select **Viridis** for the full 256-node scientific palette.
Residual maps retain a zero-centered diverging palette.

| Group | Additional maps |
|---|---|
| General | Viridis, Batlow |
| Spectrum | Jet (default), Rainbow |
| Precipitation | Rain, precip_11lev (NCL), precip_diff_12lev (NCL) |
| Temperature | Thermal, Balance, temp_19lev (NCL) |
| Circulation | Speed, Curl, Phase, wind_17lev (NCL), BlueWhiteOrangeRed (NCL); Balance/Batlow can also be useful |

Use the small **…** button for searchable cards, attribution and mapping
settings. All values are in the dataset's **decoded units**. Names do not
imply accumulation periods, temperature conversion or official rainfall levels.

- **Continuous** interpolates all original nodes; balance/curl retain 512 nodes.
- **Stepped** uses original colors without interpolating. Empty boundaries mean
  equal intervals of the current display range, not meteorological thresholds.
  For K colors, enter K+1 finite, strictly increasing custom boundaries.
  Intervals include the lower endpoint; the final interval includes its upper
  endpoint. Out-of-range values clamp to endpoint colors unless custom
  under/over colors are enabled.
- **Centered** requires a center strictly inside the displayed range. To make a
  symmetric zero-centered view, use a fixed range `[-a, a]` and center `0`.
- **Cyclic** requires an explicit origin and positive period in the data's units;
  values separated by one period share a color. It does not derive wind direction
  from U/V or guess radians/degrees.
- **Discrete** on the existing toolbar continues to map category values. It is
  separate from numerical intervals and overrides numerical normalization.

The image and colorbar use the same mapping. File-missing values remain grey;
user Mask Range values remain white. Some precipitation palettes also contain
white as a legitimate color: white never automatically means missing data.

## 📂 Local RGB import

Choose **… → Import RGB…** and explicitly select the file's 0..1 or 0..255
encoding. The parser accepts `ncolor`/`ncolors`, blank lines and comments.
All RGB rows are retained; foreground/background rows are not silently removed.
Invalid counts, nonfinite components and oversized files are rejected before
changing the active palette. Imports are stored under the application's local
data directory in `colormaps/`, separately from the shipped collection.

## ⚡ Cache and diagnostics

Changing colors, reversing, changing display ranges, or editing Mask Range
reuses the decoded current layer. Mask edits derive from the original decoded
cache so clearing a mask restores data. Larger images recolor on a bounded
two-worker pool; each view retains at most one pending request and ignores
outdated results. Raster pixels update without rebuilding the curve overlay.
Layer changes use the shared plane reader and update the open profile.
The existing `--preview` batch PNG export completes rendering synchronously
before saving and exiting, including larger planes.
The process-wide raster allowance defaults to 1024 MiB, conservatively charged
at 64 bytes per pixel for decoded/masked buffers and in-flight image replacement.
Set `HDFSTUDIO_RASTER_BUDGET_MIB` (1..16384) before launch to change it. This limits
the raster allocations, not the entire process working set; other views and
library caches are outside this allowance. A plane still has the existing
16-Mi-pixel limit. Closing a view releases its allowance after pending work exits.

Hover over the raster for the decoded value and units, or a distinct source
missing / user Mask state. All-missing and all-masked planes retain their state
colors and show no invented numerical range. Category labels preserve the
file's `flag_values` / `flag_meanings` pairing even if values are not sorted.

```text
hdf_cli cmap
hdf_cli cmap bids:viridis 0.5
hdf_cli rgb my-palette.rgb byte
hdf_cli rgb my-palette.rgb unit
```

These commands use the same palette parser and sampling code as the GUI.
The `rgb` command validates without installing or copying the input.

## 📜 Reproducibility and attribution

`resources/colormaps/manifest.json` lists the input/output hashes and license
files. Each palette records source, authors, version, transformation and shape.
Upstream cmap Python copies are omitted from public source; their provenance
and hashes remain in the manifest. Full license texts and attributions are in
`licenses/colormaps/`, included in both source and binary packages. Local export
and maintenance scripts are not published.
Runtime does not require Python, cmap or NCL.

Jet and Rainbow are pinned to Matplotlib 3.10.7. Jet retains every channel
breakpoint; Rainbow samples its functions at 256 positions. Derived JSON and
the complete Matplotlib license are shipped; upstream implementation copies
remain local development inputs. Spectrum palettes do not imply physical
thresholds or perceptually uniform distances between values.
