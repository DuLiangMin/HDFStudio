// Compatibility HDF/NetCDF grid buffer API; ownership remains with ItemData.
#pragma once
#include <QString>
#include <QtGlobal>
#include <vector>
class ItemData;
inline constexpr quint64 grid_buffer_limit_bytes = 2ULL * 1024 * 1024 * 1024;
QString classic_file_of(const ItemData *item_data);
QString classic_load_variable(ItemData *item_data, const std::vector<int> &layers);
