// netCDF原生类型hyperslab；不改变旧read_plane路径。
#include "nc_reader.hpp"
#include "slice_decode.hpp"
#include <QRegularExpression>

namespace {
QString text_attribute(int group,int variable,const char *key) {
  nc_type type;size_t count=0;
  if(nc_inq_att(group,variable,key,&type,&count)!=NC_NOERR)return {};
  if(type==NC_CHAR && count<=65536) {
    QByteArray text(int(count),'\0');if(nc_get_att_text(group,variable,key,text.data())==NC_NOERR)return QString::fromUtf8(text).trimmed();
  }
  if(type==NC_STRING && count==1) {char *text=nullptr;if(nc_get_att_string(group,variable,key,&text)==NC_NOERR){QString result=QString::fromUtf8(text).trimmed();nc_free_string(1,&text);return result;}}
  return {};
}
bool number_attribute(int group,int variable,const char *key,double &value,QString &error) {
  nc_type type;size_t count=0;int status=nc_inq_att(group,variable,key,&type,&count);
  if(status==NC_ENOTATT)return true;
  if(status!=NC_NOERR || count!=1 || type==NC_CHAR || type==NC_STRING || nc_get_att_double(group,variable,key,&value)!=NC_NOERR || !std::isfinite(value)) {
    error=QString("InvalidPacking: %1 must be a finite numeric scalar").arg(key);return false;
  }
  return true;
}
template<class T> NcSliceResult read_typed(int group,int variable,nc_type type,const std::vector<size_t> &start,
    const std::vector<size_t> &count,const NcSliceSpec &spec) {
  NcSliceResult result;result.reader="netCDF-C";
  T fill{},missing{};bool has_fill=false,has_missing=false;double scale=1,offset=0;
  auto attribute=[&](const char *key,T &value,bool &present) {
    nc_type attr_type;size_t length=0;const int status=nc_inq_att(group,variable,key,&attr_type,&length);
    if(status==NC_ENOTATT)return true;
    if(status!=NC_NOERR || length!=1 || attr_type!=type || nc_get_att(group,variable,key,&value)!=NC_NOERR) {
      result.error=QString("UnsupportedPacking: %1 must be a scalar of the storage type").arg(key);return false;
    }
    present=true;return true;
  };
  if(!attribute("_FillValue",fill,has_fill)||!attribute("missing_value",missing,has_missing) ||
     !number_attribute(group,variable,"scale_factor",scale,result.error)||!number_attribute(group,variable,"add_offset",offset,result.error)) {
    result.error_code=result.error.startsWith("InvalidPacking")?"InvalidPacking":"UnsupportedPacking";return result;
  }
  if(!has_fill) {int no_fill=0;if(nc_inq_var_fill(group,variable,&no_fill,&fill)==NC_NOERR)has_fill=!no_fill;}
  const size_t rows=count.size()==1?1:count[spec.row_axis],cols=count.size()==1?count[0]:count[spec.col_axis];
  std::vector<T> raw(rows*cols);
  int status=nc_get_vara(group,variable,start.data(),count.data(),raw.data());
  if(status!=NC_NOERR){result.error_code="ReadError";result.error=QString::fromUtf8(nc_strerror(status));return result;}
  slice_decode_values(raw,has_fill,fill,has_missing,missing,scale,offset,result.plane.values,result.precision_warning);
  slice_orient(result.plane.values,rows,cols,count.size()>1&&spec.row_axis>spec.col_axis);
  result.plane.rows=rows;result.plane.cols=cols;return result;
}
}
NcSliceResult NcReader::read_coordinate(const std::string &path,size_t index) const {
  NcSliceResult result;result.reader="netCDF-C";int group=-1,variable=-1,rank=0,dimension=-1;nc_type type;size_t length=0;
  auto fail=[&](const QString &code,const QString &message){result.error_code=code;result.error=message;return result;};
  if(!locate(path,group,variable)||nc_inq_varndims(group,variable,&rank)!=NC_NOERR||rank!=1||nc_inq_vardimid(group,variable,&dimension)!=NC_NOERR||nc_inq_dimlen(group,dimension,&length)!=NC_NOERR||nc_inq_vartype(group,variable,&type)!=NC_NOERR)return fail("ReadError","Coordinate unavailable");
  if(!length||(index!=size_t(-1)&&index>=length))return fail("InvalidSpec","Coordinate index outside dimension");
  if(index==size_t(-1)&&length>16ull*1024*1024)return fail("PixelLimitExceeded","Coordinate too large");
  const std::vector<size_t> start={index==size_t(-1)?0:index},count={index==size_t(-1)?length:1};
  if(text_attribute(group,variable,"_Unsigned").compare("true",Qt::CaseInsensitive)==0)return fail("UnsupportedPacking","Coordinate _Unsigned reinterpretation is unsupported");
  const NcSliceSpec spec;
  switch(type) {
    case NC_BYTE:return read_typed<signed char>(group,variable,type,start,count,spec);
    case NC_UBYTE:return read_typed<unsigned char>(group,variable,type,start,count,spec);
    case NC_SHORT:return read_typed<short>(group,variable,type,start,count,spec);
    case NC_USHORT:return read_typed<unsigned short>(group,variable,type,start,count,spec);
    case NC_INT:return read_typed<int>(group,variable,type,start,count,spec);
    case NC_UINT:return read_typed<unsigned int>(group,variable,type,start,count,spec);
    case NC_INT64:return read_typed<long long>(group,variable,type,start,count,spec);
    case NC_UINT64:return read_typed<unsigned long long>(group,variable,type,start,count,spec);
    case NC_FLOAT:return read_typed<float>(group,variable,type,start,count,spec);
    case NC_DOUBLE:return read_typed<double>(group,variable,type,start,count,spec);
    default:return fail("UnsupportedType","Unsupported numeric coordinate type");
  }
}
NcSliceResult NcReader::read_slice(const std::string &path,const NcSliceSpec &spec) const {
  NcSliceResult result;result.reader="netCDF-C";
  int group=-1,variable=-1,rank=0;nc_type type;
  auto fail=[&](const QString &code,const QString &message){result.error_code=code;result.error=message;return result;};
  if(!locate(path,group,variable)||nc_inq_varndims(group,variable,&rank)!=NC_NOERR || rank<2)return fail("InvalidSpec","Variable requires at least two dimensions");
  std::vector<int> ids(rank);std::vector<size_t> shape(rank),start(rank,0),count(rank,1);
  if(nc_inq_vardimid(group,variable,ids.data())!=NC_NOERR||nc_inq_vartype(group,variable,&type)!=NC_NOERR)return fail("ReadError","Cannot inspect variable");
  for(int axis=0;axis<rank;++axis)if(nc_inq_dimlen(group,ids[axis],&shape[axis])!=NC_NOERR)return fail("ReadError","Cannot inspect shape");
  const auto error=slice_validate(shape,spec);if(!error.isEmpty())return fail(error,"Invalid slice axes, fixed indices, or plane size");
  for(int axis=0;axis<rank;++axis) {
    if(size_t(axis)==spec.row_axis||size_t(axis)==spec.col_axis)count[axis]=shape[axis];
    else start[axis]=spec.fixed_indices[axis];
  }
  if(text_attribute(group,variable,"_Unsigned").compare("true",Qt::CaseInsensitive)==0)return fail("UnsupportedPacking","_Unsigned reinterpretation is not supported by DecodeProfileV1");
  // Bound the per-variable library cache as well as our own plane allocations.
  nc_set_var_chunk_cache(group,variable,1024*1024,521,.75f);
  switch(type) {
    case NC_BYTE:return read_typed<signed char>(group,variable,type,start,count,spec);
    case NC_UBYTE:return read_typed<unsigned char>(group,variable,type,start,count,spec);
    case NC_SHORT:return read_typed<short>(group,variable,type,start,count,spec);
    case NC_USHORT:return read_typed<unsigned short>(group,variable,type,start,count,spec);
    case NC_INT:return read_typed<int>(group,variable,type,start,count,spec);
    case NC_UINT:return read_typed<unsigned int>(group,variable,type,start,count,spec);
    case NC_INT64:return read_typed<long long>(group,variable,type,start,count,spec);
    case NC_UINT64:return read_typed<unsigned long long>(group,variable,type,start,count,spec);
    case NC_FLOAT:return read_typed<float>(group,variable,type,start,count,spec);
    case NC_DOUBLE:return read_typed<double>(group,variable,type,start,count,spec);
    default:return fail("UnsupportedType","Only fixed-width numeric scalar elements are supported");
  }
}
