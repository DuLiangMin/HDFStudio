// Format adapters feed the existing decoded-plane and analysis algorithms.
#include "data_source.hpp"
#include "grib/grib_data.hpp"
#include "plane_reader.hpp"
#include "nc_classic.hpp"
#include <QFileInfo>
namespace DataSource {
bool grib_target(const QString& target) { return target.startsWith("/field/")||target.startsWith("/cube/"); }
ArrayInfo describe(const QString& file,const QString& target) {
  if(GribData::is_grib(file))return GribData::describe(file,target);
  ArrayInfo info;info.object.object_id=target;info.title=target;
  NcReader reader(file);int group=-1,variable=-1,rank=0;
  if(reader.locate(target.toUtf8().toStdString(),group,variable)) {
    if(nc_inq_varndims(group,variable,&rank)!=NC_NOERR){info.error="Cannot read rank";return info;}
    std::vector<int> dims(size_t(rank),0);
    if(nc_inq_vardimid(group,variable,dims.data())!=NC_NOERR){info.error="Cannot read dimensions";return info;}
    for(int id:dims) {
      size_t length=0;char name[NC_MAX_NAME+1]={};
      if(nc_inq_dim(group,id,name,&length)!=NC_NOERR){info.error="Cannot read dimension";return info;}
      info.shape.push_back(length);AxisInfo axis;axis.name=QString::fromUtf8(name);axis.length=length;info.axes.push_back(axis);
    }
    info.reader="netCDF-C";
  } else {
    const auto path=nc_path_bytes(file);
    const hid_t handle=H5Fopen(path.constData(),H5F_ACC_RDONLY,H5P_DEFAULT);
    if(handle<0){info.error="Cannot open file";return info;}
    const hid_t dataset=H5Dopen2(handle,target.toUtf8().constData(),H5P_DEFAULT);
    const hid_t space=dataset<0?-1:H5Dget_space(dataset);
    rank=space<0?-1:H5Sget_simple_extent_ndims(space);
    if(rank>=0) {
      std::vector<hsize_t> dims(size_t(rank),0);H5Sget_simple_extent_dims(space,dims.data(),nullptr);
      for(auto length:dims){info.shape.push_back(size_t(length));AxisInfo axis;axis.name=QString("axis %1").arg(info.axes.size());axis.length=size_t(length);info.axes.push_back(axis);}
    } else info.error="Variable not found";
    if(space>=0)H5Sclose(space);if(dataset>=0)H5Dclose(dataset);H5Fclose(handle);info.reader="HDF5";
  }
  info.rectangular=info.shape.size()>=2;info.can_image=info.rectangular;info.can_section=info.rectangular;
  return info;
}
PlaneData read_plane(const QString& file,const QString& target,const std::vector<size_t>& indices) {
  if(GribData::is_grib(file))return GribData::read(file,target,indices);
  const auto info=describe(file,target);PlaneData result;
  if(!info.error.isEmpty()){result.error=info.error;return result;}
  if(info.shape.size()<2){result.error="A plane needs at least two dimensions";return result;}
  if(indices.size()!=info.shape.size()-2){result.error="Specify one zero-based index per leading axis";return result;}
  for(size_t axis=0;axis<indices.size();++axis)if(indices[axis]>=info.shape[axis]){result.error="Index outside axis";return result;}
  const std::vector<hsize_t> shape(info.shape.begin(),info.shape.end());
  auto read=nc_read_plane_decoded(file,target.toUtf8().toStdString(),shape,indices);
  result.rows=read.plane.rows;result.cols=read.plane.cols;result.values=std::move(read.plane.values);result.error=read.error;result.reader=read.reader;return result;
}
}
