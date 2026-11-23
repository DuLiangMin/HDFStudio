# 🧩 Development dependencies / 开发依赖

The portable Windows package includes runtime dependencies. Public source snapshots omit third-party code copies to keep the checkout small; downloading source is only needed for development. The packaging recipe checks the public source exclusion list and verifies the pinned GRIB runtime hashes.

## 📥 Pinned inputs and locations

| Dependency | Version / origin | Local input |
|---|---|---|
| Qt | 6.8.3, [official archive](https://download.qt.io/archive/qt/6.8/6.8.3/) | Set `CMAKE_PREFIX_PATH` to the MSVC x64 Qt installation |
| HDF5 | 1.8.20, [upstream](https://github.com/HDFGroup/hdf5/tree/hdf5-1_8_20) | `ext/hdf5-1.8.20`, built static Debug/Release in `build/hdf5-1.8.20` |
| netCDF-C | 4.9.3, [upstream](https://github.com/Unidata/netcdf-c/tree/v4.9.3) | Headers in `ext/netcdf-c-4.9.3/include`; native DLL/import library in `build/native-nc/Release` |
| zlib | 1.3.1, [upstream](https://github.com/madler/zlib/tree/v1.3.1) | `ext/zlib-1.3.1`, static libraries in `build/zlib` |
| ecCodes | 2.47.0, [upstream](https://github.com/ecmwf/eccodes/tree/2.47.0) | `ext/eccodes` or `HDFSTUDIO_ECCODES_ROOT`; see `licenses/eccodes/native-manifest.json` for exact headers/runtime hashes |
| Bitshuffle / LZ4 / Zstandard | 0.5.2 / 1.10.0 / 1.5.7 | Restore exact files under `third_party/hdf_filters` with the command below |
| cmap (optional palette regeneration) | 0.8.0, [upstream](https://github.com/pyapp-kit/cmap/tree/v0.8.0) | Development Python environment only; generated palettes are already included |

```text
python tools/26NOV23_v01_restore_hdf_filters.py
```

The script reads `third_party/hdf_filters/sources.json`, downloads each pinned file and checks its SHA256. Existing matching files are reused; mismatched files are rejected rather than overwritten. Network proxies come from `HTTP_PROXY` / `HTTPS_PROXY`; no proxy is embedded in the public code. `--root PATH` can restore into a separate source checkout containing the manifest.

The native ecCodes distribution includes `eccodes.dll`, `eccodes_memfs.dll`, `aec.dll`, `openjp2.dll`, `libpng16.dll` and `zlib1.dll`. The memfs DLL carries definitions and samples, so runtime users need no external definitions directory. Source builds can be made from the pinned upstream release; validate their behavior before substituting different binaries.

Optional palette provenance copies can be recreated with `cmap==0.8.0` and `tools/26NOV21_v01_export_colormaps.py --fetch`. The existing generated JSON, licenses and source hashes remain in the public tree; Python source copies from cmap are excluded. This regeneration is not required to build or run the application.

## 📤 Publication contract

- `tools/public-source-excludes.txt` lists third-party code omitted from the public Git tree and source ZIP. License texts, hashes, build adapters and application code remain.
- `ext/`, generated `build/`, logs, private design notes and business samples are not published as source.
- The binary ZIP retains every runtime dependency, the local help page and required license texts. Third-party source exclusion does not remove runtime DLLs or palette data.
- Existing public history and older releases are retained. This policy makes new source snapshots smaller; it does not rewrite previously published Git history.

See [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) for the source tree, target dependencies and input/output locations, and [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) for attribution.
