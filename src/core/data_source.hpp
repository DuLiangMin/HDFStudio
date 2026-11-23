// Shared array description and reads. HDF/NC calls keep their existing thread restrictions.
#pragma once
#include "data_types.hpp"
namespace DataSource {
bool grib_target(const QString& target);
ArrayInfo describe(const QString& file,const QString& target);
PlaneData read_plane(const QString& file,const QString& target,const std::vector<size_t>& indices);
}
