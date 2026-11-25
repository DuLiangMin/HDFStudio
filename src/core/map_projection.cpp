// Each instance owns its PROJ context/operation; no implicit source datum.
#include "map_projection.hpp"
#include <proj.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <limits>

namespace MapProjection {
namespace {
constexpr double pi=3.14159265358979323846;
QString resources(const QString &path) {return path.isEmpty()?QCoreApplication::applicationDirPath()+"/resources/proj":path;}
void configure(PJ_CONTEXT *context,const QString &path) {
  const auto encoded=resources(path).toUtf8();const char *paths[]={encoded.constData()};
  proj_context_set_search_paths(context,1,paths);proj_context_set_enable_network(context,0);
  proj_log_level(context,PJ_LOG_NONE);
}
}
bool EarthModel::valid() const {return std::isfinite(a)&&std::isfinite(b)&&a>=b&&b>0;}
QString name(Mode mode) {
  switch(mode){case Mode::Source:return "Source grid";case Mode::LonLat:return "Longitude / latitude";
    case Mode::NorthPolar:return "North Polar Stereographic";case Mode::SouthPolar:return "South Polar Stereographic";
    case Mode::Lambert:return "East Asia Lambert";case Mode::Mercator:return "Mercator";}return {};
}
Spec preset(Mode mode,const EarthModel &earth) {
  Spec out;out.mode=mode;out.earth=earth;
  if(mode==Mode::NorthPolar){out.lat0=90;out.lat_ts=60;out.south=20;}
  if(mode==Mode::SouthPolar){out.lat0=-90;out.lat_ts=-60;out.north=-20;}
  if(mode==Mode::Lambert){out.lon0=105;out.lat0=35;out.west=70;out.span=80;out.south=10;out.north=65;}
  if(mode==Mode::Mercator){out.south=-80;out.north=80;}
  return out;
}
double unwrap(double lon,double west) {double delta=std::fmod(lon-west,360);if(delta<0)delta+=360;return west+delta;}
QString validate(const Spec &s) {
  for(double value:{s.lon0,s.lat0,s.lat1,s.lat2,s.lat_ts,s.west,s.span,s.south,s.north})if(!std::isfinite(value))return "Parameters must be finite";
  if(s.mode==Mode::Source)return {};
  if(s.span<=0||s.span>360||s.south< -90||s.north>90||s.south>=s.north||std::abs(s.lon0)>1e6||std::abs(s.west)>1e6)return "Invalid geographic extent";
  if(s.mode!=Mode::LonLat&&!s.earth.valid())return "Verified source sphere/ellipsoid is required for metre projections";
  if(s.mode==Mode::NorthPolar||s.mode==Mode::SouthPolar){const int sign=s.mode==Mode::NorthPolar?1:-1;
    if(s.lat0!=sign*90||s.lat_ts*sign<=0||s.lat_ts*sign>=90||s.span!=360||(sign>0?(s.south<=0||s.north!=90):(s.north>=0||s.south!= -90)))return "Invalid polar origin, true-scale latitude or circular boundary";}
  if(s.mode==Mode::Lambert){const double west=unwrap(s.west,s.lon0-180);
    if(!(0<s.lat1&&s.lat1<s.lat2&&s.lat2<90&&std::abs(s.lat0)<90&&s.south> -90&&s.north<90&&s.span<180&&west+s.span<s.lon0+180))return "Invalid Lambert parallels or extent crosses back seam";}
  if(s.mode==Mode::Mercator&&(std::abs(s.lat_ts)>=90||s.south<= -90||s.north>=90))return "Mercator cannot include poles";
  return {};
}
bool contains(const Spec &s,double lon,double lat){return std::isfinite(lon)&&std::isfinite(lat)&&lat>=s.south-1e-9&&lat<=s.north+1e-9&&(s.span==360||unwrap(lon,s.west)<=s.west+s.span+1e-9);}
struct Transform::Impl {Spec spec;PJ_CONTEXT *context=nullptr;PJ *operation=nullptr;QString error,definition;};
Transform::Transform(const Spec &spec,const QString &path):impl(std::make_unique<Impl>()) {
  impl->spec=spec;impl->error=validate(spec);if(!impl->error.isEmpty()||spec.mode==Mode::LonLat||spec.mode==Mode::Source)return;
  impl->context=proj_context_create();if(!impl->context){impl->error="Cannot allocate PROJ context";return;}configure(impl->context,path);
  QString definition;
  if(spec.mode==Mode::NorthPolar||spec.mode==Mode::SouthPolar)definition=QString("+proj=stere +lat_0=%1 +lat_ts=%2").arg(spec.lat0,0,'g',17).arg(spec.lat_ts,0,'g',17);
  if(spec.mode==Mode::Lambert)definition=QString("+proj=lcc +lat_0=%1 +lat_1=%2 +lat_2=%3").arg(spec.lat0,0,'g',17).arg(spec.lat1,0,'g',17).arg(spec.lat2,0,'g',17);
  if(spec.mode==Mode::Mercator)definition=QString("+proj=merc +lat_ts=%1").arg(spec.lat_ts,0,'g',17);
  definition+=QString(" +lon_0=%1 +a=%2 +b=%3 +x_0=0 +y_0=0 +units=m +over").arg(spec.lon0,0,'g',17).arg(spec.earth.a,0,'g',17).arg(spec.earth.b,0,'g',17);
  impl->definition=definition;
  impl->operation=proj_create(impl->context,definition.toUtf8().constData());
  if(!impl->operation||!proj_angular_input(impl->operation,PJ_FWD)||!proj_angular_output(impl->operation,PJ_INV))impl->error="PROJ operation creation/unit validation failed";
}
Transform::~Transform(){if(impl->operation)proj_destroy(impl->operation);if(impl->context)proj_context_destroy(impl->context);}
QString Transform::error()const{return impl->error;}
QString Transform::operation_definition()const{return impl->spec.mode==Mode::LonLat?QString("Degree display; longitude unwrapped relative to west; no PROJ operation"):impl->definition;}
bool Transform::forward(double lon,double lat,QPointF &point) const {
  if(!impl->error.isEmpty()||!std::isfinite(lon)||!std::isfinite(lat)||std::abs(lat)>90)return false;
  const auto &s=impl->spec;if(s.mode==Mode::Source)return false;
  if(s.mode==Mode::LonLat){point={(lon>=s.west&&lon<=s.west+s.span?lon:unwrap(lon,s.west))-s.lon0,lat};return true;}
  lon=unwrap(lon,s.lon0-180);if(s.mode==Mode::Mercator)lon=unwrap(lon,s.west);
  proj_errno_reset(impl->operation);const auto result=proj_trans(impl->operation,PJ_FWD,proj_coord(lon*pi/180,lat*pi/180,0,0));
  point={result.xy.x,result.xy.y};return !proj_errno(impl->operation)&&std::isfinite(point.x())&&std::isfinite(point.y());
}
bool Transform::inverse(const QPointF &point,double &lon,double &lat)const {
  if(!impl->error.isEmpty()||!std::isfinite(point.x())||!std::isfinite(point.y()))return false;
  if(impl->spec.mode==Mode::LonLat){lon=point.x()+impl->spec.lon0;lat=point.y();return std::abs(lat)<=90;}
  if(!impl->operation)return false;
  proj_errno_reset(impl->operation);const auto result=proj_trans(impl->operation,PJ_INV,proj_coord(point.x(),point.y(),0,0));
  lon=result.lp.lam*180/pi;lat=result.lp.phi*180/pi;return !proj_errno(impl->operation)&&std::isfinite(lon)&&std::isfinite(lat)&&std::abs(lat)<=90;
}
QString Transform::database_status(const QString &path){const auto file=resources(path)+"/proj.db";if(!QFileInfo::exists(file))return "Bundled proj.db is missing";
  PJ_CONTEXT *context=proj_context_create();configure(context,path);PJ *crs=proj_create(context,"EPSG:4326");const bool valid=crs!=nullptr;if(crs)proj_destroy(crs);proj_context_destroy(context);return valid?QString():QString("Bundled proj.db cannot resolve EPSG:4326");}
EarthModel Transform::model_from_wkt(const QString &wkt,QString &error){EarthModel model;error.clear();PJ_CONTEXT *context=proj_context_create();configure(context,{});PJ *crs=proj_create_from_wkt(context,wkt.toUtf8().constData(),nullptr,nullptr,nullptr);
  if(!crs){error="Declared WKT cannot be parsed";proj_context_destroy(context);return model;}
  const auto type=proj_get_type(crs);if(type!=PJ_TYPE_GEOGRAPHIC_2D_CRS&&type!=PJ_TYPE_GEOGRAPHIC_3D_CRS)error="WKT is not a geographic source CRS";
  PJ *ellipsoid=proj_get_ellipsoid(context,crs),*prime=proj_get_prime_meridian(context,crs);double longitude=0,factor=0,inverse=0;int computed=0;
  if(!ellipsoid||!prime||!proj_ellipsoid_get_parameters(context,ellipsoid,&model.a,&model.b,&computed,&inverse)||!proj_prime_meridian_get_parameters(context,prime,&longitude,&factor,nullptr))error="WKT model/prime meridian unavailable";
  else if(!std::isfinite(longitude)||!std::isfinite(factor)||std::abs(longitude*factor)>1e-12)error="WKT non-Greenwich prime meridian requires verified normalization";
  if(ellipsoid)proj_destroy(ellipsoid);if(prime)proj_destroy(prime);proj_destroy(crs);proj_context_destroy(context);
  if(!error.isEmpty()||!model.valid()){if(error.isEmpty())error="Invalid WKT ellipsoid";return {};}
  model.evidence="Declared geographic WKT ellipsoid/Greenwich meridian; no datum transformation";return model;
}
QPointF Viewport::at(double x,double y)const{return {xmin+x*(xmax-xmin)/width,ymax-y*(ymax-ymin)/height};}
QPointF Viewport::pixel(const QPointF &p)const{return {(p.x()-xmin)*width/(xmax-xmin),(ymax-p.y())*height/(ymax-ymin)};}
Viewport fit(const Spec &s,int width,int height,double dpr,QString &error) {
  Viewport v;error=validate(s);if(!error.isEmpty())return v;
  if(width<=0||height<=0||width>16*1024*1024/height||!std::isfinite(dpr)||dpr<=0){error="Invalid or excessive canvas size";return v;}
  Transform transform(s);error=transform.error();if(!error.isEmpty())return v;
  double xmin=INFINITY,xmax=-INFINITY,ymin=INFINITY,ymax=-INFINITY;
  auto include=[&](double lon,double lat){QPointF p;if(transform.forward(lon,lat,p)){xmin=std::min(xmin,p.x());xmax=std::max(xmax,p.x());ymin=std::min(ymin,p.y());ymax=std::max(ymax,p.y());}};
  for(int i=0;i<=1440;++i){const double f=double(i)/1440;const double lon=s.west+s.span*f;
    // Avoid wrap of the right-most global endpoint onto the left edge.
    include(i==1440?std::nextafter(lon,s.west):lon,s.south);include(i==1440?std::nextafter(lon,s.west):lon,s.north);
    include(s.west,s.south+(s.north-s.south)*f);include(std::nextafter(s.west+s.span,s.west),s.south+(s.north-s.south)*f);}
  if(!(xmin<xmax&&ymin<ymax)){error="Projection extent is empty";return v;}
  const double scale=1.02*std::max((xmax-xmin)/width,(ymax-ymin)/height);const double cx=(xmin+xmax)/2,cy=(ymin+ymax)/2;
  v={cx-scale*width/2,cx+scale*width/2,cy-scale*height/2,cy+scale*height/2,dpr,width,height};return v;
}
}
