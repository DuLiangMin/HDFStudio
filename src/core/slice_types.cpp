#include "slice_types.hpp"
#include <climits>
#include <algorithm>

QString slice_validate(const std::vector<size_t> &shape, const NcSliceSpec &spec) {
  if(shape.size()<2 || spec.row_axis>=shape.size() || spec.col_axis>=shape.size() ||
     spec.row_axis==spec.col_axis || spec.fixed_indices.size()!=shape.size()) return "InvalidSpec";
  for(size_t axis=0;axis<shape.size();++axis) {
    if(!shape[axis]) return "InvalidSpec";
    if(axis!=spec.row_axis && axis!=spec.col_axis && spec.fixed_indices[axis]>=shape[axis]) return "InvalidSpec";
  }
  const size_t rows=shape[spec.row_axis],cols=shape[spec.col_axis];
  if(rows>INT_MAX || cols>INT_MAX || rows>(16ull*1024*1024)/cols) return "PixelLimitExceeded";
  return {};
}
void slice_orient(std::vector<double> &values,size_t rows,size_t cols,bool transpose) {
  if(!transpose) return;
  std::vector<double> ordered(values.size());
  for(size_t row=0;row<rows;++row)
    for(size_t col=0;col<cols;++col) ordered[row*cols+col]=values[col*rows+row];
  values.swap(ordered);
}
