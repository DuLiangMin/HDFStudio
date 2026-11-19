// Limited CF-1.8 checks: declaration and root variable bounds references.
#include "nc_reader.hpp"
#include <cmath>
#include <QRegularExpression>
#include <QFile>

bool valid_vector_pair(const NcVectorPlane &planes) {
  if(planes.u.rows==0 || planes.u.cols==0 || planes.v.rows==0 || planes.v.cols==0 ||
     planes.u.rows!=planes.v.rows || planes.u.cols!=planes.v.cols ||
     planes.u.values.size()!=planes.u.rows*planes.u.cols ||
     planes.v.values.size()!=planes.v.rows*planes.v.cols) return false;
  return true;
}

FormatProbe probe_data_format(const QString &file) {
  const QByteArray encoded=nc_path_bytes(file);
  int ncid=-1;
  if(nc_open(encoded.constData(),NC_NOWRITE,&ncid)==NC_NOERR) {
    int format=0,mode=0,apiFormat=0;
    FormatProbe result;
    if(nc_inq_format_extended(ncid,&format,&mode)==NC_NOERR) {
      if(format==NC_FORMATX_NC4) {
        nc_type markerType;size_t markerLength=0;
        if(nc_inq_att(ncid,NC_GLOBAL,"_NCProperties",&markerType,&markerLength)!=NC_NOERR ||
           markerType!=NC_CHAR || markerLength==0 || markerLength>65536) {
          nc_close(ncid);
          return {"HDF5 container (NetCDF identity unverified)",
                  "netCDF-C can open the HDF5 container, but the NetCDF-4 _NCProperties marker is absent",false};
        }
      }
      result.netcdf=true;
      if(format==NC_FORMATX_NC3) {
        if(mode&NC_64BIT_DATA) result.kind="NetCDF CDF-5";
        else if(mode&NC_64BIT_OFFSET) result.kind="NetCDF CDF-2";
        else result.kind="NetCDF CDF-1";
      } else if(format==NC_FORMATX_NC4) {
        result.kind=(nc_inq_format(ncid,&apiFormat)==NC_NOERR && apiFormat==NC_FORMAT_NETCDF4_CLASSIC)
            ?"NetCDF-4 classic model":"NetCDF-4 enhanced";
      } else result.kind="NetCDF format (unclassified)";
      result.detail=QString("format=%1 mode=0x%2").arg(format).arg(mode,0,16);
    } else {
      result.kind="NetCDF (format query unavailable)";
    }
    nc_close(ncid);
    return result;
  }
  QFile input(file);
  if(!input.open(QIODevice::ReadOnly)) return {"Unreadable",input.errorString(),false};
  const QByteArray bytes=input.read(4096);
  const QByteArray signature("\x89" "HDF\r\n\x1a\n",8);
  bool hdf5=bytes.startsWith(signature);
  for(qint64 offset=512;!hdf5 && offset+8<=bytes.size();offset*=2)
    hdf5=bytes.mid(int(offset),8)==signature;
  if(hdf5) return {"HDF5 container (NetCDF identity unverified)","HDF signature found; no verified NetCDF-4 marker",false};
  if(bytes.startsWith("CDF\1") || bytes.startsWith("CDF\2") || bytes.startsWith("CDF\5"))
    return {"NetCDF classic signature (open failed)","CDF signature found but netCDF-C open failed",false};
  return {"Unknown", "Neither supported NetCDF nor HDF5 signature detected",false};
}

GeoCoordinate geographic_coordinate(const NcGeographicGrid &grid,size_t row,size_t column) {
  if(!grid.valid || row>=grid.latitude.size() || column>=grid.longitude.size()) return {NAN,NAN,false};
  return {grid.latitude[row],grid.longitude[column],true};
}

static QString text_attr(int group,int variable,const char *key) {
  nc_type type; size_t length;
  if(nc_inq_att(group,variable,key,&type,&length)!=NC_NOERR || length>65536) return {};
  if(type==NC_CHAR) {
    QByteArray text(int(length),'\0');
    if(nc_get_att_text(group,variable,key,text.data())==NC_NOERR) return QString::fromUtf8(text).trimmed();
  } else if(type==NC_STRING && length==1) {
    char *s=nullptr;
    if(nc_get_att_string(group,variable,key,&s)==NC_NOERR) {
      QString text=QString::fromUtf8(s); nc_free_string(1,&s); return text.trimmed();
    }
  }
  return {};
}

static QString variable_text_attr(int group,int variable,const char *name);

bool NcReader::locate(const std::string &path,int &group,int &variable) const {
  auto slash=path.find_last_of('/'); group=id;
  if(group<0) return false;
  if(slash!=std::string::npos && slash>0 && nc_inq_grp_full_ncid(id,path.substr(0,slash).c_str(),&group)!=NC_NOERR) return false;
  return nc_inq_varid(group,path.substr(slash+1).c_str(),&variable)==NC_NOERR;
}

QString NcReader::cf_summary() const {
  if(id<0) return "CF-1.8: not assessed";
  auto declared=text_attr(id,NC_GLOBAL,"Conventions");
  const auto findings=diagnose_cf18();
  int errors=0,warnings=0,information=0;
  for(const auto &finding:findings) {
    if(finding.severity=="error") ++errors;
    else if(finding.severity=="warning") ++warnings;
    else ++information;
  }
  return QString("CF-1.8 partial/advisory: %1 error, %2 warning, %3 info; not a conformance certification | Conventions: %4")
      .arg(errors).arg(warnings).arg(information).arg(declared.isEmpty()?QString("not declared"):declared);
}

static double number_attr(int group,int variable,const char *name,double fallback) {
  size_t length; double value;
  if(nc_inq_attlen(group,variable,name,&length)==NC_NOERR && length==1 &&
      nc_get_att_double(group,variable,name,&value)==NC_NOERR) return value;
  return fallback;
}

static bool integer_type(nc_type type) {
  return type==NC_BYTE || type==NC_SHORT || type==NC_INT || type==NC_UBYTE ||
         type==NC_USHORT || type==NC_UINT || type==NC_INT64 || type==NC_UINT64;
}

static bool integer_attribute(int group,int variable,const char *name,double &value,bool optional=false) {
  nc_type type;
  size_t length;
  const int status=nc_inq_att(group,variable,name,&type,&length);
  if(status==NC_ENOTATT) return optional;
  return status==NC_NOERR && length==1 && integer_type(type) &&
         nc_get_att_double(group,variable,name,&value)==NC_NOERR;
}

static bool scoped_variable(int scope,const QString &name,int &group,int &variable) {
  const auto bytes=name.toUtf8();
  if(bytes.isEmpty() || bytes.contains('\0') || bytes.contains('/')) return false;
  group=scope;
  return nc_inq_varid(group,bytes.constData(),&variable)==NC_NOERR;
}

std::vector<CfDiagnostic> NcReader::diagnose_cf18() const {
  std::vector<CfDiagnostic> findings;
  if(id<0) return findings;
  auto add=[&](QString rule,QString severity,QString path,QString message) {
    findings.push_back({std::move(rule),std::move(severity),std::move(path),std::move(message)});
  };
  auto groupPath=[&](int group) {
    size_t length=0;
    if(nc_inq_grpname_full(group,&length,nullptr)!=NC_NOERR || length>65536) return QString("/");
    QByteArray path(int(length)+1,'\0');
    if(nc_inq_grpname_full(group,&length,path.data())!=NC_NOERR) return QString("/");
    return QString::fromUtf8(path.constData());
  };
  const QString conventions=text_attr(id,NC_GLOBAL,"Conventions");
  if(!conventions.split(QRegularExpression("[\\s,]+"),Qt::SkipEmptyParts).contains("CF-1.8"))
    add("CF18-GLOBAL-001","info","/","Conventions does not declare CF-1.8; diagnostics are still advisory");
  std::vector<int> groups{id};
  for(size_t index=0;index<groups.size();++index) {
    int childCount=0;
    if(nc_inq_grps(groups[index],&childCount,nullptr)==NC_NOERR && childCount>0) {
      std::vector<int> children(static_cast<size_t>(childCount),0);
      if(nc_inq_grps(groups[index],&childCount,children.data())==NC_NOERR)
        groups.insert(groups.end(),children.begin(),children.end());
    }
    int variableCount=0;
    if(nc_inq_nvars(groups[index],&variableCount)!=NC_NOERR) continue;
    for(int variable=0;variable<variableCount;++variable) {
      char name[NC_MAX_NAME+1]={};
      if(nc_inq_varname(groups[index],variable,name)!=NC_NOERR) continue;
      const QString path=groupPath(groups[index])+"/"+QString::fromUtf8(name);
      auto checkReference=[&](const char *attribute,const char *rule) {
        const QString target=variable_text_attr(groups[index],variable,attribute);
        if(target.isEmpty()) return;
        if(target.contains('/') || target.contains(QRegularExpression("\\s")) || target.contains(',')) {
          add(rule,"warning",path,QString("%1 uses a path/list form outside this diagnostic subset").arg(attribute));return;
        }
        int targetVariable=-1;
        const QByteArray encoded=target.toUtf8();
        if(nc_inq_varid(groups[index],encoded.constData(),&targetVariable)!=NC_NOERR)
          add(rule,"warning",path,QString("Referenced variable '%1' was not found locally; ancestor/external lookup is not checked").arg(target));
      };
      checkReference("bounds","CF18-BOUNDS-001");
      checkReference("climatology","CF18-CLIM-001");
      double fill=0;
      if(nc_inq_att(groups[index],variable,"_FillValue",nullptr,nullptr)==NC_NOERR) {
        nc_type varType,attType;size_t attLength=0;
        nc_inq_vartype(groups[index],variable,&varType);
        nc_inq_att(groups[index],variable,"_FillValue",&attType,&attLength);
        if(varType!=attType || attLength!=1)
          add("CF18-MISSING-001","error",path,"_FillValue must be a scalar with the variable's external type");
      }
      Q_UNUSED(fill);
      const QString coordinates=variable_text_attr(groups[index],variable,"coordinates");
      for(const QString &coordinate:coordinates.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts)) {
        int coordinateVariable=-1;const QByteArray encoded=coordinate.toUtf8();
        if(nc_inq_varid(groups[index],encoded.constData(),&coordinateVariable)!=NC_NOERR)
          add("CF18-COORD-001","warning",path,QString("coordinates variable '%1' not found locally; ancestor lookup is not checked").arg(coordinate));
      }
      const QString gridMapping=variable_text_attr(groups[index],variable,"grid_mapping");
      if(!gridMapping.isEmpty()) {
        const QString firstToken=gridMapping.section(QRegularExpression("\\s+"),0,0).section(':',0,0);
        int mappingVariable=-1;const QByteArray encoded=firstToken.toUtf8();
        if(nc_inq_varid(groups[index],encoded.constData(),&mappingVariable)!=NC_NOERR)
          add("CF18-GRID-001","warning",path,QString("grid_mapping variable '%1' not found locally; ancestor lookup is not checked").arg(firstToken));
        else if(variable_text_attr(groups[index],mappingVariable,"grid_mapping_name").isEmpty())
          add("CF18-GRID-002","error",groupPath(groups[index])+"/"+firstToken,"grid_mapping_name is missing");
      }
    }
  }
  return findings;
}

QString NcReader::read_plane(const std::string &path,const std::vector<size_t> &indices,NcPlane &plane) const {
  plane={}; int group, variable, rank;
  if(!locate(path,group,variable)) return "Variable not found";
  if(nc_inq_varndims(group,variable,&rank)!=NC_NOERR || rank<2 || indices.size()!=size_t(rank-2))
    return "Specify one zero-based index for every axis before the last two";
  std::vector<int> dimids(rank); std::vector<size_t> start(rank,0), count(rank,1);
  if(nc_inq_vardimid(group,variable,dimids.data())!=NC_NOERR) return "Cannot read dimensions";
  for(int i=0;i<rank;++i) {
    size_t length=0;
    if(nc_inq_dimlen(group,dimids[i],&length)!=NC_NOERR || !length) return "Empty or inaccessible axis";
    if(i<rank-2) { if(indices[i]>=length) return "Index outside axis"; start[i]=indices[i]; }
    else count[i]=length;
  }
  size_t rows=count[rank-2], cols=count.back();
  if(rows>16*1024*1024/cols) return "Plane exceeds 16 megapixels";
  NcPlane result; result.rows=rows; result.cols=cols; result.values.resize(rows*cols);
  int status=nc_get_vara_double(group,variable,start.data(),count.data(),result.values.data());
  if(status!=NC_NOERR) return QString::fromUtf8(nc_strerror(status));
  double fill=number_attr(group,variable,"_FillValue",NAN), missing=number_attr(group,variable,"missing_value",NAN);
  double scale=number_attr(group,variable,"scale_factor",1), offset=number_attr(group,variable,"add_offset",0);
  for(double &value:result.values) value=(value==fill || value==missing)?NAN:value*scale+offset;
  plane=std::move(result); return {};
}

QString NcReader::read_vector_planes(const std::string &uPath,const std::string &vPath,
    const std::vector<size_t> &indices,NcVectorPlane &planes) const {
  planes={};
  QString error=read_plane(uPath,indices,planes.u);
  if(!error.isEmpty()) return QString("U component: %1").arg(error);
  error=read_plane(vPath,indices,planes.v);
  if(!error.isEmpty()) return QString("V component: %1").arg(error);
  if(!valid_vector_pair(planes)) {
    planes={};
    return QString("U/V grid shape mismatch");
  }
  return {};
}

static QString variable_text_attr(int group,int variable,const char *name) {
  nc_type type; size_t length=0;
  if(nc_inq_att(group,variable,name,&type,&length)!=NC_NOERR || length>65536) return {};
  if(type==NC_CHAR) {
    QByteArray bytes(int(length),'\0');
    if(nc_get_att_text(group,variable,name,bytes.data())==NC_NOERR) return QString::fromUtf8(bytes).trimmed();
  }
  if(type==NC_STRING && length==1) {
    char *value=nullptr;
    if(nc_get_att_string(group,variable,name,&value)==NC_NOERR) {
      QString text=QString::fromUtf8(value).trimmed(); nc_free_string(1,&value); return text;
    }
  }
  return {};
}

NcGeographicGrid NcReader::geographic_grid(const std::string &dataPath,
    const std::vector<size_t> &dataShape) const {
  NcGeographicGrid result;
  int group,dataVariable;
  if(!locate(dataPath,group,dataVariable)) {result.error="Data variable not found";return result;}
  int rank=0;
  if(nc_inq_varndims(group,dataVariable,&rank)!=NC_NOERR || rank<2 || dataShape.size()!=size_t(rank)) {
    result.error="Data variable shape unavailable";return result;
  }
  const auto coordinates=variable_text_attr(group,dataVariable,"coordinates").split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
  int latGroup=-1,latVar=-1,lonGroup=-1,lonVar=-1;
  auto resolve=[&](const QString &name,int &coordGroup,int &coordVar) {
    QByteArray encoded=name.toUtf8();
    return locate(encoded.toStdString(),coordGroup,coordVar);
  };
  for(const QString &candidate:coordinates) {
    int candidateGroup,candidateVar;
    if(!resolve(candidate,candidateGroup,candidateVar)) continue;
    QString standard=variable_text_attr(candidateGroup,candidateVar,"standard_name");
    QString units=variable_text_attr(candidateGroup,candidateVar,"units").toLower();
    if(standard=="latitude" && units=="degrees_north") {result.latitudePath=candidate.toStdString();latGroup=candidateGroup;latVar=candidateVar;}
    if(standard=="longitude" && units=="degrees_east") {result.longitudePath=candidate.toStdString();lonGroup=candidateGroup;lonVar=candidateVar;}
  }
  auto matches_dimension=[&](int coordinateGroup,int coordinateVar,int dataDimension) {
    int coordinateRank=0,coordinateDims[NC_MAX_VAR_DIMS]={},dataDims[NC_MAX_VAR_DIMS]={};
    if(nc_inq_varndims(coordinateGroup,coordinateVar,&coordinateRank)!=NC_NOERR || coordinateRank!=1 ||
       nc_inq_vardimid(coordinateGroup,coordinateVar,coordinateDims)!=NC_NOERR ||
       nc_inq_vardimid(group,dataVariable,dataDims)!=NC_NOERR) return false;
    return coordinateDims[0]==dataDims[dataDimension];
  };
  if(latVar<0 || lonVar<0 || !matches_dimension(latGroup,latVar,rank-2) || !matches_dimension(lonGroup,lonVar,rank-1)) {
    result.error="No verified one-dimensional CF latitude/longitude coordinates match the last two data dimensions";return result;
  }
  size_t latLength=0,lonLength=0;
  int latDims[1],lonDims[1];
  if(nc_inq_vardimid(latGroup,latVar,latDims)!=NC_NOERR || nc_inq_vardimid(lonGroup,lonVar,lonDims)!=NC_NOERR ||
     nc_inq_dimlen(latGroup,latDims[0],&latLength)!=NC_NOERR || nc_inq_dimlen(lonGroup,lonDims[0],&lonLength)!=NC_NOERR ||
     latLength!=dataShape[rank-2] || lonLength!=dataShape[rank-1]) {
    result.error="Coordinate lengths do not match the data grid";return result;
  }
  result.latitude.resize(latLength);result.longitude.resize(lonLength);
  if(nc_get_var_double(latGroup,latVar,result.latitude.data())!=NC_NOERR ||
     nc_get_var_double(lonGroup,lonVar,result.longitude.data())!=NC_NOERR) {
    result.latitude.clear();result.longitude.clear();result.error="Cannot read coordinate values";return result;
  }
  for(double value:result.latitude) if(!std::isfinite(value)||value < -90 || value > 90) {result.error="Latitude values are outside valid geographic bounds";return result;}
  for(double value:result.longitude) if(!std::isfinite(value) || value < -180 || value > 360) {
    result.error="Longitude values are outside the supported -180..360 degree range";return result;
  }
  result.valid=true;
  return result;
}

NcMesh NcReader::read_ugrid_faces(const std::string &dataPath) const {
  NcMesh result;
  int dataGroup,dataVariable;
  if(!locate(dataPath,dataGroup,dataVariable)) {result.error="Data variable not found";return result;}
  const QString meshName=variable_text_attr(dataGroup,dataVariable,"mesh");
  if(meshName.isEmpty()) {result.error="Data variable has no UGRID mesh attribute";return result;}
  if(text_attr(dataGroup,dataVariable,"location")!="face") {
    result.error="UGRID data must declare location=face";return result;
  }
  int meshGroup,meshVariable;
  if(!scoped_variable(dataGroup,meshName,meshGroup,meshVariable) ||
     variable_text_attr(meshGroup,meshVariable,"cf_role")!="mesh_topology") {
    result.error="UGRID mesh topology variable not found";return result;
  }
  double topologyDimension=0;
  if(!integer_attribute(meshGroup,meshVariable,"topology_dimension",topologyDimension) || topologyDimension!=2) {
    result.error="Only 2D UGRID topology is supported";return result;
  }
  result.topologyPath=meshName.toStdString();
  const auto coordinates=variable_text_attr(meshGroup,meshVariable,"node_coordinates").split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
  if(coordinates.size()!=2) {result.error="UGRID node_coordinates must identify exactly two variables";return result;}
  int latitudeGroup=-1,latitudeVariable=-1,longitudeGroup=-1,longitudeVariable=-1;
  for(const QString &coordinate:coordinates) {
    int coordinateGroup,coordinateVariable;
    if(!scoped_variable(meshGroup,coordinate,coordinateGroup,coordinateVariable)) continue;
    const QString standard=variable_text_attr(coordinateGroup,coordinateVariable,"standard_name");
    const QString units=variable_text_attr(coordinateGroup,coordinateVariable,"units").toLower();
    if(standard=="latitude" && units=="degrees_north") {latitudeGroup=coordinateGroup;latitudeVariable=coordinateVariable;result.latitudePath=coordinate.toStdString();}
    if(standard=="longitude" && units=="degrees_east") {longitudeGroup=coordinateGroup;longitudeVariable=coordinateVariable;result.longitudePath=coordinate.toStdString();}
  }
  if(latitudeVariable<0 || longitudeVariable<0) {result.error="UGRID node coordinates lack explicit CF latitude/longitude metadata";return result;}
  int latRank=0,lonRank=0,latDims[NC_MAX_VAR_DIMS]={},lonDims[NC_MAX_VAR_DIMS]={};
  size_t latitudeLength=0,longitudeLength=0;
  if(nc_inq_varndims(latitudeGroup,latitudeVariable,&latRank)!=NC_NOERR || latRank!=1 ||
     nc_inq_varndims(longitudeGroup,longitudeVariable,&lonRank)!=NC_NOERR || lonRank!=1 ||
     nc_inq_vardimid(latitudeGroup,latitudeVariable,latDims)!=NC_NOERR ||
     nc_inq_vardimid(longitudeGroup,longitudeVariable,lonDims)!=NC_NOERR || latDims[0]!=lonDims[0] ||
     nc_inq_dimlen(latitudeGroup,latDims[0],&latitudeLength)!=NC_NOERR ||
     nc_inq_dimlen(longitudeGroup,lonDims[0],&longitudeLength)!=NC_NOERR || latitudeLength!=longitudeLength) {
    result.error="UGRID node coordinate variables must share one one-dimensional node dimension";return result;
  }
  if(latitudeLength<3 || latitudeLength>100000) {result.error="UGRID node count outside the preview limit (3..100000)";return result;}
  result.nodeCount=latitudeLength;result.latitude.resize(latitudeLength);result.longitude.resize(longitudeLength);
  if(nc_get_var_double(latitudeGroup,latitudeVariable,result.latitude.data())!=NC_NOERR ||
     nc_get_var_double(longitudeGroup,longitudeVariable,result.longitude.data())!=NC_NOERR) {
    result.error="Cannot read UGRID node coordinates";return result;
  }
  for(size_t node=0;node<latitudeLength;++node) {
    if(!std::isfinite(result.latitude[node]) || std::abs(result.latitude[node])>90 ||
       !std::isfinite(result.longitude[node]) || result.longitude[node]<-180 || result.longitude[node]>360) {
      result.error="UGRID node coordinates outside the supported latitude/longitude range";return result;
    }
  }
  const QString connectivityName=variable_text_attr(meshGroup,meshVariable,"face_node_connectivity");
  int connectivityGroup,connectivityVariable;
  if(!scoped_variable(meshGroup,connectivityName,connectivityGroup,connectivityVariable) ||
     variable_text_attr(connectivityGroup,connectivityVariable,"cf_role")!="face_node_connectivity") {
    result.error="UGRID face_node_connectivity variable is missing or has an invalid cf_role";return result;
  }
  result.faceConnectivityPath=connectivityName.toStdString();
  int connectivityRank=0,connectivityDims[NC_MAX_VAR_DIMS]={};
  size_t faceCount=0,nodesPerFace=0;
  if(nc_inq_varndims(connectivityGroup,connectivityVariable,&connectivityRank)!=NC_NOERR || connectivityRank!=2 ||
     nc_inq_vardimid(connectivityGroup,connectivityVariable,connectivityDims)!=NC_NOERR ||
     nc_inq_dimlen(connectivityGroup,connectivityDims[0],&faceCount)!=NC_NOERR ||
     nc_inq_dimlen(connectivityGroup,connectivityDims[1],&nodesPerFace)!=NC_NOERR || nodesPerFace!=3) {
    result.error="Only fixed three-node triangular face connectivity is supported";return result;
  }
  if(!faceCount || faceCount>50000) {result.error="UGRID face count outside the preview limit (1..50000)";return result;}
  const auto faceDimension=text_attr(meshGroup,meshVariable,"face_dimension");
  int declaredFace=-1;
  if(!faceDimension.isEmpty() && (nc_inq_dimid(meshGroup,faceDimension.toUtf8().constData(),&declaredFace)!=NC_NOERR ||
      declaredFace!=connectivityDims[0])) {
    result.error="Only face-first connectivity is supported";return result;
  }
  nc_type connectivityType;
  if(nc_inq_vartype(connectivityGroup,connectivityVariable,&connectivityType)!=NC_NOERR || !integer_type(connectivityType)) {
    result.error="UGRID face connectivity must be an integer variable";return result;
  }
  double startIndex=0;
  if(!integer_attribute(connectivityGroup,connectivityVariable,"start_index",startIndex,true) ||
     (startIndex!=0 && startIndex!=1)) {result.error="UGRID start_index must be a scalar integer 0 or 1";return result;}
  double fillValue=NAN;
  if(!integer_attribute(connectivityGroup,connectivityVariable,"_FillValue",fillValue,true)) {
    result.error="UGRID connectivity fill must be a scalar integer";return result;
  }
  std::vector<double> connectivity(faceCount*nodesPerFace);
  if(nc_get_var_double(connectivityGroup,connectivityVariable,connectivity.data())!=NC_NOERR) {
    result.error="Cannot read UGRID face connectivity";return result;
  }
  result.faceNodes.reserve(connectivity.size());
  for(double index:connectivity) {
    if(!std::isfinite(index) || index==fillValue || index<startIndex || index-startIndex>=result.nodeCount) {
      result.error="Face connectivity contains a fill or out-of-range node index; mixed faces are not supported";return result;
    }
    result.faceNodes.push_back(static_cast<size_t>(index-startIndex));
  }
  for(size_t face=0;face<faceCount;++face) {
    const size_t a=result.faceNodes[3*face],b=result.faceNodes[3*face+1],c=result.faceNodes[3*face+2];
    const double x1=result.longitude[b]-result.longitude[a],x2=result.longitude[c]-result.longitude[a];
    const double y1=result.latitude[b]-result.latitude[a],y2=result.latitude[c]-result.latitude[a];
    if(std::abs(x1)>180 || std::abs(x2)>180 || std::abs(x1-x2)>180 || x1*y2==x2*y1) {
      result.error="Degenerate or longitude-wrapping face not supported";return result;
    }
  }
  int dataRank=0,dataDimensions[NC_MAX_VAR_DIMS]={};
  if(nc_inq_varndims(dataGroup,dataVariable,&dataRank)!=NC_NOERR || dataRank!=1 ||
     nc_inq_vardimid(dataGroup,dataVariable,dataDimensions)!=NC_NOERR || dataDimensions[0]!=connectivityDims[0]) {
    result.error="Only one-dimensional data located on the UGRID faces is supported";return result;
  }
  result.faceValues.resize(faceCount);
  if(nc_get_var_double(dataGroup,dataVariable,result.faceValues.data())!=NC_NOERR) {
    result.error="Cannot read face data values";return result;
  }
  const double fill=number_attr(dataGroup,dataVariable,"_FillValue",NAN);
  const double missing=number_attr(dataGroup,dataVariable,"missing_value",NAN);
  const double scale=number_attr(dataGroup,dataVariable,"scale_factor",1);
  const double offset=number_attr(dataGroup,dataVariable,"add_offset",0);
  for(double &value:result.faceValues) value=(value==fill || value==missing)?NAN:value*scale+offset;
  result.faceCount=faceCount;result.nodesPerFace=nodesPerFace;result.valid=true;
  return result;
}
