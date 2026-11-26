// Adapted from HDFStudio_to_Isosahedral/src/core/ico_render.cpp.
// Retains pixel-center rasterization, half-open owners and paired seam geometry.
#include "ico_atlas_render.hpp"
#include <QPainter>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include <map>
void IcoAtlasRenderer::build(const Icosahedral::Mesh &mesh){
  if(m_level==mesh.level&&m_rect==rectangular&&!m_face.empty())return;
  m_level=mesh.level;m_rect=rectangular;const size_t pixels=size_t(width)*height;
  m_face.assign(pixels,-1);m_bary_a.assign(pixels,0);m_bary_b.assign(pixels,0);m_points.resize(mesh.faces.size());
  const double chart_height=rectangular?215:255;
  for(size_t face=0;face<mesh.atlas.triangles.size();++face){const auto &tri=mesh.atlas.triangles[face];auto &p=m_points[face];
    for(int k=0;k<3;++k)p[k]={65+410*(tri.uv[k].u+(rectangular?0:.5*(1-tri.uv[k].v))),55+tri.chart*300+(1-tri.uv[k].v)*chart_height};
    const double det=(p[1].y()-p[2].y())*(p[0].x()-p[2].x())+(p[2].x()-p[1].x())*(p[0].y()-p[2].y());
    const int left=std::max(0,int(std::floor(std::min({p[0].x(),p[1].x(),p[2].x()})))),right=std::min(width-1,int(std::ceil(std::max({p[0].x(),p[1].x(),p[2].x()}))));
    const int top=std::max(0,int(std::floor(std::min({p[0].y(),p[1].y(),p[2].y()})))),bottom=std::min(height-1,int(std::ceil(std::max({p[0].y(),p[1].y(),p[2].y()}))));
    for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){
      const double a=((p[1].y()-p[2].y())*(x+.5-p[2].x())+(p[2].x()-p[1].x())*(y+.5-p[2].y()))/det,b=((p[2].y()-p[0].y())*(x+.5-p[2].x())+(p[0].x()-p[2].x())*(y+.5-p[2].y()))/det;
      if(a>=-1e-10&&b>=-1e-10&&a+b<=1+1e-10){const size_t address=size_t(y)*width+x;if(m_face[address]<0){m_face[address]=int32_t(face);m_bary_a[address]=float(a);m_bary_b[address]=float(b);}}
    }
  }
}
QImage IcoAtlasRenderer::render(const Icosahedral::Result &r,const HdfColorMap::Palette &palette,const HdfColorMap::Mapping &mapping){
  const auto &mesh=r.mesh;const auto &atlas=mesh.atlas;
  if(!r.error.isEmpty()||r.node_values.size()!=mesh.xyz.size()||atlas.triangles.size()!=mesh.faces.size()||atlas.node_chart.size()+2!=mesh.xyz.size()||!HdfColorMap::validate(palette,mapping).isEmpty())return {};
  build(mesh);QImage image(width,height,QImage::Format_RGB32);image.fill(QColor("#f5f6f8"));const int n=1<<mesh.level;
  for(int y=0;y<height;++y){auto *line=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<width;++x){const size_t address=size_t(y)*width+x;const int face=m_face[address];if(face<0)continue;double value=NAN;
    if(rectangular){const int chart=atlas.triangles[face].chart,col=std::clamp(int((x+.5-65)/410*n),0,2*n-1),row=std::clamp(int((1-(y+.5-55-chart*300)/215)*n),0,n-1);value=r.node_values[atlas.node_chart[size_t(chart*2*n*n+row*2*n+col)]];}
    else {const auto &ids=mesh.faces[face];const double a=m_bary_a[address],b=m_bary_b[address];value=a*r.node_values[ids[0]]+b*r.node_values[ids[1]]+(1-a-b)*r.node_values[ids[2]];}
    line[x]=HdfColorMap::map_value(palette,mapping,value);
  }}
  QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);
  if(mesh_lines){painter.setPen(QPen(QColor(30,40,50,90),.5));if(!rectangular&&mesh.level<=4)for(const auto &p:m_points){painter.drawLine(p[0],p[1]);painter.drawLine(p[1],p[2]);painter.drawLine(p[2],p[0]);}
    painter.setPen(QPen(QColor("#374151"),1.4));
    if(rectangular){for(int chart=0;chart<5;++chart)painter.drawRect(QRectF(65,55+chart*300,820,215));}
    else {const int indices[4][3]={{0,1,2},{2,1,3},{2,3,4},{4,3,5}};const Icosahedral::ChartPoint corners[6]={{0,1},{0,0},{1,1},{1,0},{2,1},{2,0}};const size_t stride=size_t(1)<<(2*mesh.level);
      for(int base=0;base<20;++base){const auto &tri=atlas.triangles[size_t(base)*stride];QPolygonF polygon;for(int k=0;k<3;++k){const auto p=corners[indices[tri.quad][k]];polygon<<QPointF(65+410*(p.u+.5*(1-p.v)),55+tri.chart*300+(1-p.v)*255);}painter.drawPolygon(polygon);}
    }
  }
  if(seams&&!rectangular){struct EdgeCopy{QPointF a,b;int chart;};std::map<std::pair<int32_t,int32_t>,std::vector<EdgeCopy>> edges;const size_t stride=size_t(1)<<(2*mesh.level);
    for(int base=0;base<20;++base){std::map<int32_t,QPointF> corners;for(size_t f=base*stride;f<(base+1)*stride;++f)for(int k=0;k<3;++k){const auto node=mesh.faces[f][k];if(node<12)corners[node]=m_points[f][k];}
      for(auto a=corners.begin();a!=corners.end();++a)for(auto b=std::next(a);b!=corners.end();++b)edges[{a->first,b->first}].push_back({a->second,b->second,atlas.triangles[base*stride].chart});
    }
    int pair=0;for(const auto &entry:edges){const auto &copies=entry.second;if(copies.size()!=2||copies[0].chart==copies[1].chart)continue;painter.setPen(QPen(QColor::fromHsv((pair++*137)%360,180,170),3));for(const auto &edge:copies)painter.drawLine(edge.a,edge.b);}
  }
  if(qobject_cast<QGuiApplication*>(QCoreApplication::instance())){QFont font=painter.font();font.setPointSize(14);painter.setFont(font);painter.setPen(QColor("#243244"));
    for(int chart=0;chart<5;++chart)painter.drawText(QPointF(65,38+chart*300),QString("Chart %1 | %2").arg(chart+1).arg(rectangular?"rectangular node card":"4 base faces"));
    painter.drawText(QPointF(65,1550),"Same node samples | exterior = NaN | full mesh retained");
    if(rectangular)for(int k=0;k<2;++k){const auto node=atlas.apex[k];const double value=r.node_values[node];painter.fillRect(QRectF(910,70+k*100,35,35),QColor::fromRgba(HdfColorMap::map_value(palette,mapping,value)));painter.drawText(QRectF(950,65+k*100,240,70),Qt::AlignLeft|Qt::TextWordWrap,QString("Apex #%1\n%2 %3").arg(node).arg(std::isfinite(value)?QString::number(value,'g',7):"missing").arg(r.source.units));}
  }
  return image;
}
IcoAtlasPick IcoAtlasRenderer::pick(int x,int y,const Icosahedral::Result &r)const{
  IcoAtlasPick hit;if(x<0||y<0||x>=width||y>=height||m_face.empty()||m_level!=r.mesh.level)return hit;const auto &atlas=r.mesh.atlas;
  if(rectangular&&x>=910&&x<945)for(int k=0;k<2;++k)if(y>=70+k*100&&y<105+k*100){hit.node=atlas.apex[k];hit.value=r.node_values[hit.node];return hit;}
  const size_t address=size_t(y)*width+x;hit.face=m_face[address];if(hit.face<0)return hit;hit.chart=atlas.triangles[hit.face].chart;
  if(rectangular){const int n=1<<r.mesh.level,col=std::clamp(int((x+.5-65)/410*n),0,2*n-1),row=std::clamp(int((1-(y+.5-55-hit.chart*300)/215)*n),0,n-1);hit.node=atlas.node_chart[size_t(hit.chart*2*n*n+row*2*n+col)];hit.value=r.node_values[hit.node];}
  else {const auto &ids=r.mesh.faces[hit.face];const double a=m_bary_a[address],b=m_bary_b[address];hit.node=ids[a>b&&a>1-a-b?0:(b>1-a-b?1:2)];hit.value=r.node_values[hit.node];}
  return hit;
}
