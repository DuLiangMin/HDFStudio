# 🧩 Built-in HDF5 decoding dependencies

| Library | Pinned source | License |
|---|---|---|
| Bitshuffle core | kiyo-masui/bitshuffle tag `0.5.2` | MIT, `bitshuffle/LICENSE` |
| LZ4 | lz4/lz4 tag `v1.10.0` | BSD-2-Clause, `lz4/LICENSE` |
| Zstandard decode library | facebook/zstd tag `v1.5.7` | BSD-3-Clause, `zstd/LICENSE` (upstream alternative in `COPYING`) |

📦 `sources.json` records exact upstream URLs and SHA256 for every unmodified source/license file. Public source snapshots omit third-party code copies and local maintenance scripts. Before building, download each entry's `url` to the path in `file` relative to this directory and verify `sha256`, preserving original bytes. See [dependency instructions](../../docs/DEPENDENCIES.md). License texts stay in both source and portable packages. Only the core bit transform and native decompression functions are used. HDFStudio's bounded, decode-only HDF5 adapter is in `src/core/hdf_filters.cpp`.

🔌 Input is filter 32008 metadata and an HDF5 chunk; output is the original stored bytes. HDF5 then converts datatypes and the existing reader applies packing attributes. No Python dependency or global plugin-path changes are introduced.
