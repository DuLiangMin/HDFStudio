#include "map_render.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace MapRender {
namespace {
bool clip(QPointF &a,QPointF &b,double west,double east,double south,double north){
  double start=0,end=1;const double dx=b.x()-a.x(),dy=b.y()-a.y();
  auto edge=[&](double p,double q){if(p==0)return q>=0;const double t=q/p;if(p<0)start=std::max(start,t);else end=std::min(end,t);return start<=end;};
  if(!edge(-dx,a.x()-west)||!edge(dx,east-a.x())||!edge(-dy,a.y()-south)||!edge(dy,north-a.y()))return false;
  const auto original=a;a=original+QPointF(dx,dy)*start;b=original+QPointF(dx,dy)*end;return true;
}
double deviation(const QPointF &p,const QPointF &a,const QPointF &b){const auto delta=b-a;const double length=delta.x()*delta.x()+delta.y()*delta.y();const double t=length?std::clamp(((p-a).x()*delta.x()+(p-a).y()*delta.y())/length,0.0,1.0):0;const auto d=p-(a+delta*t);return std::hypot(d.x(),d.y());}
struct Lines {
  const Input &input;MapProjection::Transform transform;std::function<bool()> cancelled;
  size_t vertices=0;
  Lines(const Input &source,std::function<bool()> cancel):input(source),transform(source.spec),cancelled(std::move(cancel)){}
  bool project(const QPointF &p,QPointF &screen){QPointF out;if(!transform.forward(p.x(),p.y(),out))return false;screen=input.viewport.pixel(out);return true;}
  void subdivide(const QPointF &a,const QPointF &b,std::vector<QLineF>&out,int depth){
    if(cancelled&&cancelled())throw std::runtime_error("Cancelled");
    QPointF pa,pb,pq,pm,pt;const bool valid=project(a,pa)&&project(b,pb)&&project(a*.75+b*.25,pq)&&project((a+b)/2,pm)&&project(a*.25+b*.75,pt);
    const double tolerance=.75*input.viewport.dpr;
    if(!valid||std::max({deviation(pq,pa,pb),deviation(pm,pa,pb),deviation(pt,pa,pb)})>tolerance){
      if(depth==12)throw std::runtime_error("Overlay incomplete: subdivision limit or invalid projection");
      subdivide(a,(a+b)/2,out,depth+1);subdivide((a+b)/2,b,out,depth+1);return;}
    if(vertices+2>500000)throw std::runtime_error("Overlay vertex limit reached");
    QPointF first=pa,last=pb;if(clip(first,last,0,input.viewport.width,0,input.viewport.height)){out.emplace_back(first,last);vertices+=2;}
  }
  void segment(QPointF a,QPointF b,std::vector<QLineF> &out){
    const auto &s=input.spec;const double branch=(s.mode==MapProjection::Mode::NorthPolar||s.mode==MapProjection::Mode::SouthPolar||s.mode==MapProjection::Mode::Lambert)?s.lon0-180:s.west;
    a.setX(MapProjection::unwrap(a.x(),branch));b.setX(a.x()+std::remainder(b.x()-a.x(),360));
    // Duplicate into adjacent branches and clip before transforming; never connect across seam.
    for(int shift=-1;shift<=1;++shift){auto first=a+QPointF(360*shift,0),last=b+QPointF(360*shift,0);
      const double west=MapProjection::unwrap(s.west,branch),east=west+s.span;
      auto append=[&](double low,double high){auto start=first,end=last;if(clip(start,end,low,std::nextafter(high,low),s.south,s.north))subdivide(start,end,out,0);};
      if(east<=branch+360+1e-8)append(west,std::min(east,branch+360));
      else {append(west,branch+360);append(branch,east-360);}}
  }
};
}
QString state_name(State s){switch(s){case State::Valid:return "Valid";case State::Missing:return "Missing in source";case State::Masked:return "User Mask";case State::ProjectionOutside:return "Projection domain outside";case State::ClipOutside:return "Geographic clip outside";case State::SourceOutside:return "Outside source coverage";}return {};}
std::shared_ptr<Output> render(const Input &input,const std::function<bool()> &cancelled){auto out=std::make_shared<Output>();out->lease=input.output_lease;out->viewport=input.viewport;
  try {
    if(!input.grid||!input.values||!input.original)throw std::runtime_error("Missing immutable map input");
    auto error=GeoGrid::validate_frame(*input.grid,*input.values,*input.original);if(!error.isEmpty())throw std::runtime_error(error.toStdString());
    const auto &v=input.viewport;const auto &s=input.spec;const size_t rows=input.grid->latitude.centers.size(),cols=input.grid->longitude.centers.size();
    if(v.width<=0||v.height<=0||v.width>16*1024*1024/v.height||input.source.size()!=QSize(int(cols),int(rows)))throw std::runtime_error("Invalid map/source canvas");
    MapProjection::Transform transform(s);if(!transform.error().isEmpty())throw std::runtime_error(transform.error().toStdString());
    const size_t count=size_t(v.width)*v.height;out->indices.assign(count,-1);out->states.assign(count,quint8(State::ProjectionOutside));out->image=QImage(v.width,v.height,QImage::Format_RGB32);
    if(out->image.isNull())throw std::runtime_error("Map image allocation failed");out->image.fill(qRgb(247,247,247));
    // Explicit state distinguishes source missing from user Mask before pixel sampling.
    std::vector<quint8> source_state(rows*cols);for(size_t i=0;i<source_state.size();++i)source_state[i]=quint8(!std::isfinite((*input.original)[i])?State::Missing:!std::isfinite((*input.values)[i])?State::Masked:State::Valid);
    for(int y=0;y<v.height;++y){if(cancelled&&cancelled())throw std::runtime_error("Cancelled");auto *line=reinterpret_cast<QRgb*>(out->image.scanLine(y));
      for(int x=0;x<v.width;++x){const size_t target=size_t(y)*v.width+x;double lon=0,lat=0;if(!transform.inverse(v.at(x+.5,y+.5),lon,lat))continue;
        const bool unwrapped_outside=(s.mode==MapProjection::Mode::LonLat||s.mode==MapProjection::Mode::Mercator)&&(lon<s.west-1e-8||lon>s.west+s.span+1e-8);
        if(unwrapped_outside||!MapProjection::contains(s,lon,lat)){out->states[target]=quint8(State::ClipOutside);continue;}
        if(std::abs(std::abs(lat)-90)<1e-9)lon=s.lon0;
        const qint64 r=GeoGrid::nearest(input.grid->latitude,lat),c=GeoGrid::nearest(input.grid->longitude,lon,true);
        if(r<0||c<0){out->states[target]=quint8(State::SourceOutside);continue;}
        const size_t source=size_t(r)*cols+size_t(c);out->indices[target]=qint64(source);out->states[target]=source_state[source];line[x]=input.source.pixel(int(c),int(r));++out->covered;}}
    if(input.geometry)out->geometry=input.geometry;
    else {auto geometry=std::make_shared<Geometry>();geometry->storage_lease=input.output_lease;Lines lines(input,cancelled);
      try {for(int lon=-180;lon<180;lon+=30)for(int lat=-90;lat<90;lat+=2)lines.segment({double(lon),double(lat)},{double(lon),double(lat+2)},geometry->graticule);
        for(int lat=-60;lat<=60;lat+=30)for(int lon=-180;lon<180;lon+=2)lines.segment({double(lon),double(lat)},{double(lon+2),double(lat)},geometry->graticule);
      }catch(const std::exception &e){geometry->graticule.clear();geometry->diagnostic=QString::fromUtf8(e.what());}
      if(input.coastline){if(!input.coastline->error.isEmpty())geometry->diagnostic+=" "+input.coastline->error;
        else try {for(const auto &part:input.coastline->parts)for(size_t i=1;i<part.size();++i)lines.segment(part[i-1],part[i],geometry->coast);}catch(const std::exception &e){geometry->coast.clear();geometry->diagnostic+=" Coastline: "+QString::fromUtf8(e.what());}}
      out->geometry=geometry;}
  }catch(const std::exception &error){out->error=QString::fromUtf8(error.what());out->image={};out->indices.clear();out->states.clear();}
  return out;
}
}
