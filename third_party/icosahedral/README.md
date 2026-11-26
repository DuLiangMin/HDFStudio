# 🌐 Icosahedral native kernels

The nine `26NOV21_*` files are byte-for-byte copies of the user's existing
`P20261120_icosahedral_mesh/06_SCRIP_C_Version/01_src` implementation.
`source-manifest.json` records their SHA256. This is the local C17 derived/new
backend, **not a complete upstream SCRIP port**. Preserve the
[original SCRIP Notice](../../licenses/icosahedral/LICENSE_SCRIP).

📂 Inputs/outputs, units, shapes and ownership are declared by `26NOV21_scrip.h`;
the HDFStudio adapter contract is [ICOSAHEDRAL](../../docs/ICOSAHEDRAL.md).
The standalone NetCDF/SCRIP and conservative C APIs remain available to native
callers. The interactive viewer uses point bilinear sampling and barycentric
reconstruction, without interpreting source points as conservative cell means.

🧮 `src/core/icosahedral.cpp` adapts axes/NaN handling and ports the radial
barycentric formula from the same project's `26NOV21_ico_regrid.py`.
Hierarchical face search rejects failed containment instead of silently choosing
a nearest face. No Python runtime is required by HDFStudio.

🖥️ Local development keeps ten original Qt GL/mesh/atlas/render/view source files from
the already compiled `HDFStudio_to_Isosahedral` project in `qt-reference/`.
Public snapshots retain its SHA256 manifest; unused reference `.cpp/.hpp` copies are excluded.
`src/core/ico_3d_mesh.*` and `src/views/ico_3d_canvas.*` copy/adapt its GL 3.3
shader/VAO/VBO, depth drawing, arcball/turntable, morph, camera and ray-pick logic.
The adapter consumes the existing immutable Result; it adds no source reader or
second remapper. `ico_atlas.*` and `ico_atlas_render.*` additionally reuse the
five-strip/rectangular atlas, half-open owners and paired seams. Mesh coordinates
are retained, so its two atlas endpoints are labelled Apex rather than geographic poles.
