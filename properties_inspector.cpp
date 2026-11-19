#include "properties_inspector.hpp"
#include "hdf5.h"
#include <limits>
#include <algorithm>

namespace {
struct Handle {
  hid_t id;
  herr_t (*close)(hid_t);
  ~Handle() { if(id >= 0) close(id); }
};

QString typeName(hid_t type) {
  switch(H5Tget_class(type)) {
    case H5T_INTEGER: {
      H5T_sign_t sign=H5Tget_sign(type);
      return QString("%1-bit %2 integer").arg(H5Tget_size(type)*8)
        .arg(sign==H5T_SGN_NONE ? QString("unsigned") : QString("signed"));
    }
    case H5T_FLOAT: return QString("%1-bit floating point").arg(H5Tget_size(type)*8);
    case H5T_STRING: return H5Tis_variable_str(type)>0 ? QString("variable-length string")
                                                        : QString("%1-byte string").arg(H5Tget_size(type));
    case H5T_ENUM: return QString("enum (%1-byte base)").arg(H5Tget_size(type));
    case H5T_COMPOUND: return QString("compound (%1 members)").arg(H5Tget_nmembers(type));
    case H5T_ARRAY: return QString("array datatype");
    case H5T_VLEN: return QString("variable-length sequence");
    case H5T_REFERENCE: return QString("reference");
    default: return QString("unsupported HDF5 datatype class %1").arg(int(H5Tget_class(type)));
  }
}

QString shapeName(hid_t space, quint64 &elements) {
  int rank=H5Sget_simple_extent_ndims(space);
  if(rank<0) return QString("unavailable");
  if(rank==0) { elements=1; return QString("Scalar"); }
  std::vector<hsize_t> dims(static_cast<size_t>(rank),0);
  if(H5Sget_simple_extent_dims(space,dims.data(),nullptr)<0) return QString("unavailable");
  QStringList parts;
  elements=1;
  for(hsize_t dim:dims) {
    if(dim && elements>std::numeric_limits<quint64>::max()/dim) return QString("overflow");
    elements*=dim;
    parts << QString::number(qulonglong(dim));
  }
  return QString("[%1]").arg(parts.join(" x "));
}
}

QString HdfProperties::formatAttribute(const QString &name,const QString &type,
    const QString &shape,const QString &value,int maximumCharacters) {
  const int safeLimit=std::max(0,maximumCharacters);
  const bool truncated=value.size()>safeLimit;
  const QString shown=truncated ? value.left(safeLimit)+QString::fromUtf8("…") : value;
  return QString("%1 | %2 | %3 | %4%5").arg(name,type,shape,shown,
    truncated ? QString(" [truncated]") : QString());
}

HdfProperties::Summary HdfProperties::inspect(const QString &filePath,
                                               const QString &objectPath) {
  Summary result;
  QByteArray fileName=filePath.toLocal8Bit(), path=objectPath.toUtf8();
  Handle file{H5Fopen(fileName.constData(),H5F_ACC_RDONLY,H5P_DEFAULT),H5Fclose};
  if(file.id<0) return result;
  H5O_info_t info{};
  const bool root=objectPath.isEmpty() || objectPath==QStringLiteral("/");
  if(root) {
    if(H5Oget_info(file.id,&info)<0) return result;
  } else if(H5Oget_info_by_name(file.id,path.constData(),&info,H5P_DEFAULT)<0) return result;
  hid_t object=-1;
  const char *objectName=root?"/":path.constData();
  if(info.type==H5G_DATASET) object=H5Dopen2(file.id,objectName,H5P_DEFAULT);
  else if(info.type==H5G_GROUP) object=H5Gopen2(file.id,objectName,H5P_DEFAULT);
  else if(info.type==H5G_TYPE) object=H5Topen2(file.id,objectName,H5P_DEFAULT);
  Handle objectHandle{object,info.type==H5G_DATASET?H5Dclose:info.type==H5G_GROUP?H5Gclose:H5Tclose};
  if(object<0) return result;
  result.objectType=info.type==H5G_DATASET?QString("Dataset"):
                    info.type==H5G_GROUP?QString("Group"):QString("Named datatype");
  if(info.type==H5G_DATASET) {
    Handle space{H5Dget_space(object),H5Sclose};
    Handle type{H5Dget_type(object),H5Tclose};
    Handle creation{H5Dget_create_plist(object),H5Pclose};
    if(space.id<0 || type.id<0 || creation.id<0) return result;
    result.shape=shapeName(space.id,result.elements);
    result.dataType=typeName(type.id);
    if(result.elements && H5Tget_size(type.id)>std::numeric_limits<quint64>::max()/result.elements)
      result.byteCountOverflow=true;
    else
      result.bytes=result.elements*H5Tget_size(type.id);
    H5D_layout_t layout=H5Pget_layout(creation.id);
    result.storage=layout==H5D_CHUNKED?QString("Chunked"):layout==H5D_COMPACT?QString("Compact"):
                   layout==H5D_CONTIGUOUS?QString("Contiguous"):QString("Unknown");
    if(layout==H5D_CHUNKED) {
      int rank=H5Sget_simple_extent_ndims(space.id);
      std::vector<hsize_t> chunks(static_cast<size_t>(rank),0);
      if(rank>0 && H5Pget_chunk(creation.id,rank,chunks.data())==rank) {
        QStringList parts;
        for(hsize_t dim:chunks) parts << QString::number(qulonglong(dim));
        result.storage+=QString(" [%1]").arg(parts.join(" x "));
      }
    }
    int filters=H5Pget_nfilters(creation.id);
    QStringList names;
    for(int i=0;i<filters;++i) {
      unsigned int flags=0, config=0, values[16]={}; size_t valuesCount=16;
      char filterName[128]={};
      auto filter=H5Pget_filter2(creation.id,unsigned(i),&flags,&valuesCount,values,
                                 sizeof(filterName),filterName,&config);
      if(filter>=0) names << QString::fromLocal8Bit(filterName[0]?filterName:std::to_string(filter).c_str());
    }
    result.compression=names.isEmpty()?QString("None"):names.join(", ");
  }
  int count=H5Aget_num_attrs(object);
  constexpr int maximumAttributes=256;
  result.attributeListTruncated=count>maximumAttributes;
  for(int i=0;i<std::min(count,maximumAttributes);++i) {
    Handle attr{H5Aopen_by_idx(object,".",H5_INDEX_NAME,H5_ITER_INC,unsigned(i),H5P_DEFAULT,H5P_DEFAULT),H5Aclose};
    if(attr.id<0) continue;
    ssize_t nameLength=H5Aget_name(attr.id,0,nullptr);
    if(nameLength<0 || nameLength>65536) continue;
    std::vector<char> attrName(static_cast<size_t>(nameLength)+1,'\0');
    if(H5Aget_name(attr.id,attrName.size(),attrName.data())<0) continue;
    Handle type{H5Aget_type(attr.id),H5Tclose};
    Handle space{H5Aget_space(attr.id),H5Sclose};
    if(type.id<0 || space.id<0) continue;
    quint64 elements=0; QString shape=shapeName(space.id,elements);
    QString value=QString("value not expanded for safe metadata preview");
    if(H5Tget_class(type.id)==H5T_STRING && elements==1 && H5Tis_variable_str(type.id)<=0) {
      size_t size=H5Tget_size(type.id);
      if(size<=1024*1024) {
        std::vector<char> buffer(size+1,'\0');
        if(H5Aread(attr.id,type.id,buffer.data())>=0) value=QString::fromUtf8(buffer.data());
      }
    } else if((H5Tget_class(type.id)==H5T_INTEGER || H5Tget_class(type.id)==H5T_FLOAT) && elements==1) {
      double number=0;
      if(H5Aread(attr.id,H5T_NATIVE_DOUBLE,&number)>=0) value=QString::number(number,'g',17);
    }
    result.attributes << formatAttribute(QString::fromLocal8Bit(attrName.data()),typeName(type.id),shape,value);
  }
  if(result.attributeListTruncated)
    result.attributes << QString("Attribute list truncated after %1 of %2 attributes")
      .arg(maximumAttributes).arg(count);
  result.valid=true;
  return result;
}
