// GRIB catalog lifetime and adapter; no Widgets, HDF5 or netCDF dependencies.
#pragma once
#include "grib_core.hpp"
#include "data_types.hpp"
#include <functional>
namespace GribData {
bool is_grib(const QString& file);
std::shared_ptr<const grib::Catalog> open(const QString& file, const std::function<bool()>& cancelled = {});
void release(const QString& file);
DataSource::ArrayInfo describe(const grib::Catalog& catalog, const QString& target);
DataSource::ArrayInfo describe(const QString& file, const QString& target);
quint64 slice_source_bytes(const grib::Catalog& catalog, const QString& target);
DataSource::PlaneData read(const grib::Catalog& catalog, const QString& target,
                           const std::vector<size_t>& indices, bool coordinates=false, bool quantized=false);
DataSource::PlaneData read(const QString& file, const QString& target,
                           const std::vector<size_t>& indices, bool coordinates=false, bool quantized=false);
DataSource::PlaneData slice(const QString& file, const QString& target,
                            const DataSource::ReadSelection& selection);
QString properties(const QString& file, const QString& target);
}
