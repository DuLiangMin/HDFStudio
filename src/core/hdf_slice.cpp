// Plain HDF5 arbitrary-axis read; native numeric signedness is preserved until decoding.
#include "plane_reader.hpp"
#include "hdf_filters.hpp"
#include "slice_decode.hpp"
#include "nc_classic.hpp"
#include <QFile>

namespace {
struct Handle {hid_t id;herr_t (*close)(hid_t);~Handle(){if(id>=0)close(id);}};
template<class T> NcSliceResult typed(hid_t dataset,hid_t space,hid_t memory,hid_t native,
    size_t rows,size_t cols,const NcSliceSpec &spec) {
  NcSliceResult out;out.reader="HDF5";
  T fill{},missing{};bool has_fill=false,has_missing=false;double scale=1,offset=0;
  auto attr=[&](const char *name,void *value,hid_t type,bool packed,bool &present) {
    if(H5Aexists(dataset,name)<=0)return true;
    Handle attribute{H5Aopen(dataset,name,H5P_DEFAULT),H5Aclose};
    Handle shape{H5Aget_space(attribute.id),H5Sclose},stored{H5Aget_type(attribute.id),H5Tclose};
    if(shape.id<0||H5Sget_simple_extent_npoints(shape.id)!=1||
       (H5Tget_class(stored.id)!=H5T_INTEGER&&H5Tget_class(stored.id)!=H5T_FLOAT))return false;
    if(!packed && (H5Tget_class(stored.id)!=H5Tget_class(type)||H5Tget_size(stored.id)!=H5Tget_size(type)||
       (H5Tget_class(type)==H5T_INTEGER&&H5Tget_sign(stored.id)!=H5Tget_sign(type))))return false;
    present=true;return H5Aread(attribute.id,type,value)>=0;
  };
  bool present=false;
  if(!attr("_FillValue",&fill,native,false,has_fill)||!attr("missing_value",&missing,native,false,has_missing)||
     !attr("scale_factor",&scale,H5T_NATIVE_DOUBLE,true,present)||!attr("add_offset",&offset,H5T_NATIVE_DOUBLE,true,present)) {
    out.error_code="UnsupportedPacking";out.error="Packing attributes require compatible numeric scalar types";return out;
  }
  if(!std::isfinite(scale)||!std::isfinite(offset)){out.error_code="InvalidPacking";out.error="Non-finite scale/offset";return out;}
  std::vector<T> raw(rows*cols);
  const auto filter_error=hdf_prepare_dataset(dataset);
  if(H5Dread(dataset,native,memory,space,H5P_DEFAULT,raw.data())<0){out.error_code=filter_error.isEmpty()?"ReadError":"UnsupportedFilter";out.error=filter_error.isEmpty()?hdf_filter_read_error("HDF5 hyperslab read failed"):filter_error;return out;}
  slice_decode_values(raw,has_fill,fill,has_missing,missing,scale,offset,out.plane.values,out.precision_warning);
  slice_orient(out.plane.values,rows,cols,spec.row_axis>spec.col_axis);
  out.plane.rows=rows;out.plane.cols=cols;return out;
}
}
NcSliceResult nc_read_slice_decoded(const QString &file,const std::string &path,
    const std::vector<hsize_t> &expected,const NcSliceSpec &spec) {
  NcSliceResult out;
  auto fail=[&](const QString &code,const QString &message){out.error_code=code;out.error=message;return out;};
  const std::vector<size_t> shape(expected.begin(),expected.end());
  const auto error=slice_validate(shape,spec);if(!error.isEmpty())return fail(error,"Invalid requested slice");
  if(is_classic_netcdf_file(file) || probe_data_format(file).netcdf) {
    NcReader reader(file);int group=-1,variable=-1;
    if(reader.locate(path,group,variable)) {
      int rank=0;nc_inq_varndims(group,variable,&rank);std::vector<int> ids(rank);std::vector<size_t> actual(rank);
      if(nc_inq_vardimid(group,variable,ids.data())!=NC_NOERR)return fail("ReadError","Cannot inspect shape");
      for(int axis=0;axis<rank;++axis)if(nc_inq_dimlen(group,ids[axis],&actual[axis])!=NC_NOERR)return fail("ReadError","Cannot read dimensions");
      if(actual!=shape)return fail("ShapeChanged","Variable shape differs from expected shape");
      return reader.read_slice(path,spec);
    }
  }
  Handle fid{H5Fopen(nc_path_bytes(file).constData(),H5F_ACC_RDONLY,H5P_DEFAULT),H5Fclose};
  Handle access{H5Pcreate(H5P_DATASET_ACCESS),H5Pclose};H5Pset_chunk_cache(access.id,521,1024*1024,.75);
  Handle dataset{fid.id>=0?H5Dopen2(fid.id,path.c_str(),access.id):-1,H5Dclose};
  if(dataset.id<0)return fail("ReadError","Cannot open numeric dataset");
  Handle space{H5Dget_space(dataset.id),H5Sclose},type{H5Dget_type(dataset.id),H5Tclose};
  if(H5Sget_simple_extent_ndims(space.id)!=int(shape.size()))return fail("ShapeChanged","Dataset rank changed");
  std::vector<hsize_t> actual(shape.size());H5Sget_simple_extent_dims(space.id,actual.data(),nullptr);
  if(actual!=expected)return fail("ShapeChanged","Dataset shape changed");
  if(H5Aexists(dataset.id,"_Unsigned")>0)return fail("UnsupportedPacking","Plain HDF5 _Unsigned reinterpretation is unsupported");
  std::vector<hsize_t> start(shape.size(),0),count(shape.size(),1);
  for(size_t axis=0;axis<shape.size();++axis) {
    if(axis==spec.row_axis||axis==spec.col_axis)count[axis]=shape[axis];else start[axis]=spec.fixed_indices[axis];
  }
  const size_t rows=shape[spec.row_axis],cols=shape[spec.col_axis];const hsize_t elements=rows*cols;
  Handle memory{H5Screate_simple(1,&elements,nullptr),H5Sclose};
  if(H5Sselect_hyperslab(space.id,H5S_SELECT_SET,start.data(),nullptr,count.data(),nullptr)<0)return fail("ReadError","Invalid HDF5 hyperslab");
  const auto category=H5Tget_class(type.id);const size_t width=H5Tget_size(type.id);const bool sign=H5Tget_sign(type.id)!=H5T_SGN_NONE;
  if(category==H5T_FLOAT) {
    if(width==4)return typed<float>(dataset.id,space.id,memory.id,H5T_NATIVE_FLOAT,rows,cols,spec);
    if(width==8)return typed<double>(dataset.id,space.id,memory.id,H5T_NATIVE_DOUBLE,rows,cols,spec);
  }
  if(category==H5T_INTEGER) {
    if(width==1){if(sign)return typed<signed char>(dataset.id,space.id,memory.id,H5T_NATIVE_SCHAR,rows,cols,spec);return typed<unsigned char>(dataset.id,space.id,memory.id,H5T_NATIVE_UCHAR,rows,cols,spec);}
    if(width==2){if(sign)return typed<short>(dataset.id,space.id,memory.id,H5T_NATIVE_SHORT,rows,cols,spec);return typed<unsigned short>(dataset.id,space.id,memory.id,H5T_NATIVE_USHORT,rows,cols,spec);}
    if(width==4){if(sign)return typed<int>(dataset.id,space.id,memory.id,H5T_NATIVE_INT,rows,cols,spec);return typed<unsigned int>(dataset.id,space.id,memory.id,H5T_NATIVE_UINT,rows,cols,spec);}
    if(width==8){if(sign)return typed<long long>(dataset.id,space.id,memory.id,H5T_NATIVE_LLONG,rows,cols,spec);return typed<unsigned long long>(dataset.id,space.id,memory.id,H5T_NATIVE_ULLONG,rows,cols,spec);}
  }
  return fail("UnsupportedType","Dataset is not a supported numeric scalar type");
}
