//Copyright (C) 2016 Pedro Vicente
//GNU General Public License (GPL) Version 3 described in the LICENSE file

// One current row-major plane for high-rank classic NetCDF grids.
#include "legacy_grid_data.hpp"
#include "browser_item.hpp"
#include "nc_classic.hpp"
#include <algorithm>

QString classic_file_of(const ItemData *item_data)
{
  return QString::fromLocal8Bit(item_data->m_file_name.c_str());
}

// Allocates the grid buffer on first use and fills it. A variable with more than two dimensions
// keeps only the plane selected by `layers` (one index per leading axis); anything else is read whole.
QString classic_load_variable(ItemData *item_data, const std::vector<int> &layers)
{
  hdf_dataset_t *dataset = item_data->m_dataset;
  const size_t rank = dataset->m_dim.size();
  std::vector<size_t> start(rank, 0), count(rank, 1);
  quint64 elements = 1;
  for(size_t axis = 0; axis < rank; ++axis)
  {
    const bool leading = rank > 2 && axis < rank - 2;
    count[axis] = leading ? 1 : static_cast<size_t>(dataset->m_dim[axis]);
    if(leading) start[axis] = axis < layers.size() ? static_cast<size_t>(std::max(0, layers[axis])) : 0;
    elements *= count[axis];
  }
  if(elements == 0) return QObject::tr("This variable has no values yet.");
  if(!dataset->m_buf)
  {
    if(elements > grid_buffer_limit_bytes / std::max<size_t>(1, dataset->m_datatype_size))
      return QObject::tr("The values to display need more than 2 GB of memory.");
    dataset->m_buf = malloc(static_cast<size_t>(elements * dataset->m_datatype_size));
    if(!dataset->m_buf) return QObject::tr("Not enough memory to read this variable.");
    dataset->m_slice_mode = rank > 2;
  }
  const QString error = nc_classic_read_variable(classic_file_of(item_data), dataset->m_path,
      rank == 0 ? std::vector<size_t>() : start, count, dataset->m_buf);
  if(!error.isEmpty())
  {
    free(dataset->m_buf);
    dataset->m_buf = nullptr;
  }
  return error;
}
