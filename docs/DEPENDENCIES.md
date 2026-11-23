# 🧩 Development dependencies / 开发依赖

The portable Windows package includes runtime dependencies. Public source snapshots omit `tools/` and third-party code copies to keep the checkout small; downloading dependency source is only needed for development. Versions, source URLs, hashes and license texts remain available.

## 📥 Pinned inputs and locations

| Dependency | Version / origin | Local input |
|---|---|---|
| Qt | 6.8.3, [official archive](https://download.qt.io/archive/qt/6.8/6.8.3/) | Set `CMAKE_PREFIX_PATH` to the MSVC x64 Qt installation |
| HDF5 | 1.8.20, [upstream](https://github.com/HDFGroup/hdf5/tree/hdf5-1_8_20) | `ext/hdf5-1.8.20`, built static Debug/Release in `build/hdf5-1.8.20` |
| netCDF-C | 4.9.3, [upstream](https://github.com/Unidata/netcdf-c/tree/v4.9.3) | Headers in `ext/netcdf-c-4.9.3/include`; native DLL/import library in `build/native-nc/Release` |
| zlib | 1.3.1, [upstream](https://github.com/madler/zlib/tree/v1.3.1) | `ext/zlib-1.3.1`, static libraries in `build/zlib` |
| ecCodes | 2.47.0, [upstream](https://github.com/ecmwf/eccodes/tree/2.47.0) | `ext/eccodes` or `HDFSTUDIO_ECCODES_ROOT`; see `licenses/eccodes/native-manifest.json` for exact headers/runtime hashes |
| Bitshuffle / LZ4 / Zstandard | 0.5.2 / 1.10.0 / 1.5.7 | Download the files listed in `third_party/hdf_filters/sources.json` into their recorded relative paths |
| cmap (optional palette regeneration) | 0.8.0, [upstream](https://github.com/pyapp-kit/cmap/tree/v0.8.0) | Development Python environment only; generated palettes are already included |

For each entry in [sources.json](../third_party/hdf_filters/sources.json), download `url` to `third_party/hdf_filters/<file>` and verify its `sha256` before building. Paths are relative to `third_party/hdf_filters/`; preserve upstream bytes and line endings. Do not overwrite an existing file with different contents. Local maintenance and download scripts are not part of the public source.

The native ecCodes distribution includes `eccodes.dll`, `eccodes_memfs.dll`, `aec.dll`, `openjp2.dll`, `libpng16.dll` and `zlib1.dll`. The memfs DLL carries definitions and samples, so runtime users need no external definitions directory. Source builds can be made from the pinned upstream release; validate their behavior before substituting different binaries.

Optional palette provenance can be inspected in upstream `cmap==0.8.0`; the recorded paths and hashes are in `resources/colormaps/manifest.json`. Generated palette JSON and licenses remain in the public tree; upstream cmap Python copies and local generation scripts are excluded. Regeneration is not required to build or run the application.

## 📤 Publication contract

- The public Git tree and updated source ZIP exclude the entire `tools/` directory and third-party code copies. License texts, hashes, build adapters and application code remain.
- `ext/`, generated `build/`, logs, private design notes and business samples are not published as source.
- The binary ZIP retains every runtime dependency, the local help page and required license texts. Third-party source exclusion does not remove runtime DLLs or palette data.
- Existing public history and older releases are retained. This policy makes new source snapshots smaller; it does not rewrite previously published Git history.

See [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) for the source tree, target dependencies and input/output locations, and [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) for attribution.
