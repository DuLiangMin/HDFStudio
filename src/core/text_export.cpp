#include "text_export.hpp"
#include "hdf_filters.hpp"
#include "nc_classic.hpp"
#include "hdf5.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {
struct Handle {
  hid_t id;
  herr_t (*close)(hid_t);
  ~Handle(){if(id>=0)close(id);}
};
QString numericText(hid_t type,const void *buffer,size_t index) {
  const H5T_class_t cls=H5Tget_class(type);
  const size_t size=H5Tget_size(type);
  if(cls==H5T_FLOAT) {
    if(size==sizeof(float)) return QString::number(static_cast<const float*>(buffer)[index],'g',9);
    if(size==sizeof(double)) return QString::number(static_cast<const double*>(buffer)[index],'g',17);
    return {};
  }
  if(cls!=H5T_INTEGER) return {};
  if(H5Tget_sign(type)==H5T_SGN_NONE) {
    switch(size) {
      case 1:return QString::number(static_cast<const quint8*>(buffer)[index]);
      case 2:return QString::number(static_cast<const quint16*>(buffer)[index]);
      case 4:return QString::number(static_cast<const quint32*>(buffer)[index]);
      case 8:return QString::number(static_cast<const quint64*>(buffer)[index]);
      default:return {};
    }
  }
  switch(size) {
    case 1:return QString::number(static_cast<const qint8*>(buffer)[index]);
    case 2:return QString::number(static_cast<const qint16*>(buffer)[index]);
    case 4:return QString::number(static_cast<const qint32*>(buffer)[index]);
    case 8:return QString::number(static_cast<const qint64*>(buffer)[index]);
    default:return {};
  }
}
}

namespace {
QString classicNumericText(const NcTypeInfo &type,const void *buffer,size_t index) {
  if(type.kind==NcTypeInfo::Float) {
    if(type.size==sizeof(float)) return QString::number(static_cast<const float*>(buffer)[index],'g',9);
    return QString::number(static_cast<const double*>(buffer)[index],'g',17);
  }
  if(type.is_signed) {
    switch(type.size) {
      case 1:return QString::number(static_cast<const qint8*>(buffer)[index]);
      case 2:return QString::number(static_cast<const qint16*>(buffer)[index]);
      case 4:return QString::number(static_cast<const qint32*>(buffer)[index]);
      default:return QString::number(static_cast<const qint64*>(buffer)[index]);
    }
  }
  switch(type.size) {
    case 1:return QString::number(static_cast<const quint8*>(buffer)[index]);
    case 2:return QString::number(static_cast<const quint16*>(buffer)[index]);
    case 4:return QString::number(static_cast<const quint32*>(buffer)[index]);
    default:return QString::number(static_cast<const quint64*>(buffer)[index]);
  }
}

// NetCDF-3 variant of write(): same row-wise layout (one line per last-axis row), read through
// netCDF-C one chunk at a time so a multi-gigabyte variable never has to fit in memory.
HdfTextExport::Result writeClassic(const QString &filePath,const QString &objectPath,
    QIODevice &output,size_t chunkValues,
    const std::function<bool(quint64,quint64)> &continueWriting) {
  HdfTextExport::Result result;
  const NcClassicFile file=nc_classic_inspect(filePath);
  if(!file.error.isEmpty()) { result.error="Cannot open NetCDF file"; return result; }
  const std::string wanted=objectPath.toStdString().substr(objectPath.startsWith('/')?1:0);
  const NcClassicVariable *variable=nullptr;
  for(const auto &candidate:file.variables) if(candidate.name==wanted) variable=&candidate;
  if(!variable) { result.error="Object is not a readable numeric dataset"; return result; }
  const NcTypeInfo type=nc_type_info(variable->type);
  if(type.kind!=NcTypeInfo::Integer && type.kind!=NcTypeInfo::Float) {
    result.error="Only supported signed integer and floating-point datasets can be exported";
    return result;
  }
  const size_t rank=variable->shape.size();
  quint64 total=1;
  for(size_t length:variable->shape) {
    if(length && total>std::numeric_limits<quint64>::max()/length) { result.error="Cannot determine dataset size"; return result; }
    total*=length;
  }
  const size_t columns=rank>0?variable->shape.back():1;
  if(columns==0 || chunkValues>std::numeric_limits<size_t>::max()/type.size) {
    result.error="Invalid dataset or chunk size"; return result;
  }
  std::vector<unsigned char> buffer(chunkValues*type.size);
  for(quint64 offset=0;offset<total;) {
    if(continueWriting && !continueWriting(offset,total)) { result.error="Export cancelled"; return result; }
    const size_t column=static_cast<size_t>(offset%columns);
    size_t count=std::min({chunkValues,columns-column,static_cast<size_t>(total-offset)});
    std::vector<size_t> start(rank,0), selected(rank,1);
    quint64 rowIndex=offset/columns;
    for(int axis=int(rank)-2;axis>=0;--axis) {
      start[size_t(axis)]=static_cast<size_t>(rowIndex%variable->shape[size_t(axis)]);
      rowIndex/=variable->shape[size_t(axis)];
    }
    if(rank>0) { start.back()=column; selected.back()=count; }
    const QString error=nc_classic_read_variable(filePath,wanted,start,selected,buffer.data());
    if(!error.isEmpty()) { result.error="Dataset chunk read failed: "+error; return result; }
    for(size_t i=0;i<count;++i) {
      if(offset+i && (offset+i)%columns==0) { if(output.write("\n",1)!=1) {result.error="Text output write failed";return result;} }
      else if(offset+i) { if(output.write(",",1)!=1) {result.error="Text output write failed";return result;} }
      QByteArray text=classicNumericText(type,buffer.data(),i).toUtf8();
      if(output.write(text.constData(),text.size())!=text.size()) {
        result.error="Text output write failed"; return result;
      }
    }
    result.valuesWritten+=count;
    offset+=count;
  }
  if(total && output.write("\n",1)!=1) {result.error="Text output write failed";return result;}
  result.ok=true;
  return result;
}
}

HdfTextExport::Result HdfTextExport::write(const QString &filePath,
    const QString &objectPath,QIODevice &output,size_t chunkValues,
    const std::function<bool(quint64,quint64)> &continueWriting) {
  Result result;
  if(chunkValues==0) { result.error="Chunk size must be positive"; return result; }
  if(is_classic_netcdf_file(filePath)) return writeClassic(filePath,objectPath,output,chunkValues,continueWriting);
  const QByteArray nativePath=filePath.toLocal8Bit(), objectName=objectPath.toUtf8();
  Handle file{H5Fopen(nativePath.constData(),H5F_ACC_RDONLY,H5P_DEFAULT),H5Fclose};
  if(file.id<0) { result.error="Cannot open HDF5 file"; return result; }
  Handle dataset{H5Dopen2(file.id,objectName.constData(),H5P_DEFAULT),H5Dclose};
  if(dataset.id<0) { result.error="Object is not a readable numeric dataset"; return result; }
  const QString filter_error=hdf_prepare_dataset(dataset.id);
  Handle type{H5Dget_type(dataset.id),H5Tclose};
  Handle memoryType{type.id>=0?H5Tget_native_type(type.id,H5T_DIR_DEFAULT):-1,H5Tclose};
  Handle space{H5Dget_space(dataset.id),H5Sclose};
  if(type.id<0 || memoryType.id<0 || space.id<0) { result.error="Cannot inspect dataset"; return result; }
  const auto cls=H5Tget_class(type.id);
  if((cls!=H5T_INTEGER && cls!=H5T_FLOAT) || H5Tget_size(memoryType.id)>8) {
    result.error="Only supported signed integer and floating-point datasets can be exported";
    return result;
  }
  hssize_t pointCount=H5Sget_simple_extent_npoints(space.id);
  if(pointCount<0) { result.error="Cannot determine dataset size"; return result; }
  const quint64 total=static_cast<quint64>(pointCount);
  int rank=H5Sget_simple_extent_ndims(space.id);
  std::vector<hsize_t> dims(static_cast<size_t>(rank),1);
  if(rank>0 && H5Sget_simple_extent_dims(space.id,dims.data(),nullptr)<0) {
    result.error="Cannot read dataset dimensions"; return result;
  }
  const size_t columns=rank>0?static_cast<size_t>(dims.back()):1;
  if(columns==0 || chunkValues>std::numeric_limits<size_t>::max()/H5Tget_size(memoryType.id)) {
    result.error="Invalid dataset or chunk size"; return result;
  }
  std::vector<unsigned char> buffer(chunkValues*H5Tget_size(memoryType.id));
  for(quint64 offset=0;offset<total;) {
    if(continueWriting && !continueWriting(offset,total)) { result.error="Export cancelled"; return result; }
    const size_t column=static_cast<size_t>(offset%columns);
    size_t count=std::min({chunkValues,columns-column,static_cast<size_t>(total-offset)});
    Handle fileSpace{H5Dget_space(dataset.id),H5Sclose};
    std::vector<hsize_t> start(static_cast<size_t>(rank),0), selected(static_cast<size_t>(rank),1);
    quint64 rowIndex=offset/columns;
    for(int axis=rank-2;axis>=0;--axis) {
      start[size_t(axis)]=rowIndex%dims[size_t(axis)];
      rowIndex/=dims[size_t(axis)];
    }
    if(rank>0) { start.back()=column; selected.back()=count; }
    if(fileSpace.id<0 || (rank>0 && H5Sselect_hyperslab(fileSpace.id,H5S_SELECT_SET,start.data(),nullptr,selected.data(),nullptr)<0)) {
      result.error="Cannot select dataset chunk"; return result;
    }
    hsize_t memoryCount=static_cast<hsize_t>(count);
    Handle memorySpace{rank>0?H5Screate_simple(1,&memoryCount,nullptr):H5Screate(H5S_SCALAR),H5Sclose};
    if(memorySpace.id<0 || H5Dread(dataset.id,memoryType.id,memorySpace.id,fileSpace.id,H5P_DEFAULT,buffer.data())<0) {
      result.error=filter_error.isEmpty()?hdf_filter_read_error("Dataset chunk read failed"):filter_error; return result;
    }
    for(size_t i=0;i<count;++i) {
      if(offset+i && (offset+i)%columns==0) { if(output.write("\n",1)!=1) {result.error="Text output write failed";return result;} }
      else if(offset+i) { if(output.write(",",1)!=1) {result.error="Text output write failed";return result;} }
      QByteArray text=numericText(memoryType.id,buffer.data(),i).toUtf8();
      if(text.isNull() || output.write(text.constData(),text.size())!=text.size()) {
        result.error="Text output write failed"; return result;
      }
    }
    result.valuesWritten+=count;
    offset+=count;
  }
  if(total && output.write("\n",1)!=1) {result.error="Text output write failed";return result;}
  result.ok=true;
  return result;
}
