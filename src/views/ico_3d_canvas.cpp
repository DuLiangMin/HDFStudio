// GL 3.3 canvas copied from the compiled HDFStudio_to_Isosahedral project (2026-11-22).
// Adaptation: immutable shared Result, original HDFStudio palette/Mask, no source reads.
#include "ico_3d_canvas.hpp"
#include <QOpenGLContext>
#include <algorithm>
#include <cmath>
namespace { constexpr double pi=3.14159265358979323846; }
Ico3DCanvas::Ico3DCanvas(QWidget *parent):QOpenGLWidget(parent) {
  setObjectName("ico3DCanvas"); setAccessibleName("Icosahedral 3D OpenGL canvas");
  setMouseTracking(true); setFocusPolicy(Qt::StrongFocus); setMinimumSize(220,180);
  QSurfaceFormat format; format.setRenderableType(QSurfaceFormat::OpenGL); format.setVersion(3,3);
  format.setProfile(QSurfaceFormat::CoreProfile); format.setDepthBufferSize(24); format.setSamples(4); setFormat(format);
  reset_camera();
}
Ico3DCanvas::~Ico3DCanvas() { cleanup(); }
void Ico3DCanvas::cleanup() {
  if(context()&&context()->isValid()) {
    makeCurrent();
    if(m_vbo) glDeleteBuffers(1,&m_vbo); if(m_vao) glDeleteVertexArrays(1,&m_vao);
    delete m_program; m_program=nullptr; doneCurrent();
  } else { delete m_program; m_program=nullptr; }
  m_vbo=0; m_vao=0; m_ready=false; m_dirty=true;
  if(context()) disconnect(context(),&QOpenGLContext::aboutToBeDestroyed,this,&Ico3DCanvas::cleanup);
}
void Ico3DCanvas::initializeGL() {
  if(!initializeOpenGLFunctions()) { m_error="OpenGL 3.3 Core is unavailable"; emit gl_status_changed(); return; }
  connect(context(),&QOpenGLContext::aboutToBeDestroyed,this,&Ico3DCanvas::cleanup,Qt::DirectConnection);
  m_error.clear();setProperty("icoGlVendor",QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_VENDOR))));
  setProperty("icoGlVersion",QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_VERSION))));
  m_program=new QOpenGLShaderProgram;
  const char *vertex_shader=R"GLSL(#version 330 core
layout(location=0) in vec3 poly; layout(location=1) in vec3 sphere; layout(location=2) in vec3 color;
uniform mat4 mvp; uniform float morph; uniform float lift;
out vec3 rgb; out vec3 pos;
void main(){ pos=mix(poly,sphere,morph); gl_Position=mvp*vec4(pos*(1.0+lift),1.0); rgb=color; }
)GLSL";
  const char *fragment_shader=R"GLSL(#version 330 core
in vec3 rgb; in vec3 pos; uniform bool shading; uniform bool highlight; out vec4 frag;
void main(){ float light=1.0; if(shading){vec3 n=normalize(cross(dFdx(pos),dFdy(pos)));light=0.65+0.35*abs(dot(n,normalize(vec3(1,1,2))));} frag=vec4(highlight?vec3(1.0,0.15,0.0):rgb*light,1); }
)GLSL";
  if(!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,vertex_shader)||
     !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,fragment_shader)||!m_program->link()) {
    m_error=m_program->log(); emit gl_status_changed(); return;
  }
  glGenVertexArrays(1,&m_vao); glGenBuffers(1,&m_vbo); glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER,m_vbo);
  for(GLuint k=0;k<3;++k) { glEnableVertexAttribArray(k); glVertexAttribPointer(k,3,GL_FLOAT,GL_FALSE,sizeof(Ico3DVertex),reinterpret_cast<void*>(size_t(k)*3*sizeof(float))); }
  glBindVertexArray(0); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
  // Preserve sRGB control bytes: default Unlit agrees with the existing color bar.
  glDisable(GL_FRAMEBUFFER_SRGB); glDisable(GL_BLEND); glDisable(GL_DITHER); m_ready=true; m_dirty=true;
  emit gl_status_changed();
}
void Ico3DCanvas::resizeGL(int,int) { emit camera_changed(); }
QMatrix4x4 Ico3DCanvas::matrix() const {
  const float aspect=float(std::max(width(),1))/std::max(height(),1);
  const float half_x=std::max(1.f,aspect)/float(m_zoom),half_y=std::max(1.f,1.f/aspect)/float(m_zoom);
  QMatrix4x4 projection,model;
  projection.ortho(-half_x,half_x,-half_y,half_y,-5.f,5.f);
  model.translate(float(m_pan.x()),float(m_pan.y()),0); model.rotate(m_rotation);
  return projection*model;
}
void Ico3DCanvas::paintGL() {
  if(!m_ready) {
    QPainter painter(this); painter.fillRect(rect(),QColor("#f3f5f8")); painter.setPen(Qt::darkRed);
    painter.drawText(rect().adjusted(12,12,-12,-12),Qt::AlignCenter|Qt::TextWordWrap,"OpenGL 3.3 initialization failed: "+m_error); return;
  }
  glClearColor(.95f,.96f,.975f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  if(!m_result) return;
  if(m_dirty) {
    std::vector<Ico3DVertex> packed=m_buffers.fill_vertices;
    for(const auto *lines:{&m_buffers.sub_edge_lines,&m_buffers.base_edge_lines,&m_buffers.chart_seam_lines,&m_buffers.graticule_lines})
      packed.insert(packed.end(),lines->begin(),lines->end());
    glBindBuffer(GL_ARRAY_BUFFER,m_vbo); glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(packed.size()*sizeof(Ico3DVertex)),packed.data(),GL_DYNAMIC_DRAW);
    setProperty("icoUploadCount",property("icoUploadCount").toULongLong()+1); m_dirty=false;
  }
  m_program->bind(); m_program->setUniformValue("mvp",matrix()); m_program->setUniformValue("morph",float(m_morph));
  m_program->setUniformValue("lift",0.f); m_program->setUniformValue("shading",shading);
  m_program->setUniformValue("highlight",false);
  glBindVertexArray(m_vao); glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.f,1.f);
  glDrawArrays(GL_TRIANGLES,0,GLsizei(m_buffers.fill_vertices.size())); glDisable(GL_POLYGON_OFFSET_FILL);
  m_program->setUniformValue("shading",false);
  GLint offset=GLint(m_buffers.fill_vertices.size());
  const bool visible[4]={sub_mesh,base_edges,seams,graticule}; int index=0;
  for(const auto *lines:{&m_buffers.sub_edge_lines,&m_buffers.base_edge_lines,&m_buffers.chart_seam_lines,&m_buffers.graticule_lines}) {
    m_program->setUniformValue("lift",index>=2?.001f:0.f);
    if(visible[index]) glDrawArrays(GL_LINES,offset,GLsizei(lines->size())); offset+=GLint(lines->size()); ++index;
  }
  if(m_highlight>=0&&size_t(m_highlight)<m_result->mesh.faces.size()) {
    m_program->setUniformValue("highlight",true); m_program->setUniformValue("lift",.002f); glDrawArrays(GL_LINE_LOOP,3*m_highlight,3);
  }
  glBindVertexArray(0); m_program->release();
  const GLenum error=glGetError(); if(error!=GL_NO_ERROR) { m_error=QString("GL error %1").arg(error);emit gl_status_changed(); }
  setProperty("icoPaintCount",property("icoPaintCount").toULongLong()+1);
}
void Ico3DCanvas::set_data(std::shared_ptr<const Icosahedral::Result> result,bool area,const HdfColorMap::Palette &palette,const HdfColorMap::Mapping &mapping) {
  if(!result||!result->error.isEmpty()){clear_data();return;}
  if(m_result!=result) {
    if(!ico_build_3d_topology_buffers(result->mesh,m_buffers)) { m_error="Invalid 3D topology";clear_data();emit gl_status_changed();return; }
    m_highlight=-1;
  }
  if(!ico_update_3d_fill_colors(*result,area,palette,mapping,m_buffers)) { m_error="Invalid 3D colors or values";clear_data();emit gl_status_changed();return; }
  m_result=std::move(result);m_area=area;m_dirty=true;m_current=true; update();
}
void Ico3DCanvas::clear_data(){m_result.reset();m_buffers={};m_highlight=-1;m_dirty=true;m_current=false;setToolTip({});update();}
void Ico3DCanvas::set_morph(double value) { m_morph=std::clamp(value,0.,1.); update(); emit camera_changed(); }
void Ico3DCanvas::reset_camera(int preset) {
  center_on(preset==4?105.:(preset==5?180.:0.),preset==1?90.:(preset==2?-90.:(preset==3?0.:20.)));
}
void Ico3DCanvas::center_on(double longitude,double latitude){
  m_pan={};m_zoom=.82;m_longitude=longitude;m_latitude=latitude;
  QMatrix4x4 view;
  const double lon=m_longitude*pi/180,lat=m_latitude*pi/180;
  const QVector3D eye(float(std::cos(lat)*std::cos(lon)),float(std::cos(lat)*std::sin(lon)),float(std::sin(lat)));
  const QVector3D up(float(-std::sin(lat)*std::cos(lon)),float(-std::sin(lat)*std::sin(lon)),float(std::cos(lat)));
  view.lookAt(eye,QVector3D(),up); m_rotation=QQuaternion::fromRotationMatrix(view.normalMatrix());
  update(); emit camera_changed();
}
QVector3D Ico3DCanvas::arcball(const QPointF &point) const {
  const double side=std::max(1,std::min(width(),height())),x=(2*point.x()-width())/side,y=(height()-2*point.y())/side,r=x*x+y*y;
  return r<=1?QVector3D(float(x),float(y),float(std::sqrt(1-r))):QVector3D(float(x/std::sqrt(r)),float(y/std::sqrt(r)),0);
}
Ico3DPickHit Ico3DCanvas::pick_at(const QPointF &point) const {
  if(!m_result||!m_current)return {};
  bool valid=false; const auto inverse=matrix().inverted(&valid); if(!valid) return {};
  const float x=float(2*point.x()/std::max(width(),1)-1),y=float(1-2*point.y()/std::max(height(),1));
  const auto near=inverse.map(QVector3D(x,y,-1)),far=inverse.map(QVector3D(x,y,1));
  return ico_pick_3d_ray(near,(far-near).normalized(),m_morph,*m_result,m_buffers,m_area);
}
void Ico3DCanvas::set_highlight(int face) { m_highlight=face; setProperty("icoHighlightedFace",face); update(); }
void Ico3DCanvas::mousePressEvent(QMouseEvent *event) { m_last=m_press=event->position(); setFocus(); event->accept(); }
void Ico3DCanvas::mouseMoveEvent(QMouseEvent *event) {
  const auto point=event->position(),delta=point-m_last;
  if(event->buttons()&Qt::LeftButton) {
    if(turntable) {
      m_longitude-=delta.x()*.4; m_latitude=std::clamp(m_latitude+delta.y()*.4,-89.99,89.99);
      const auto pan=m_pan; const double zoom=m_zoom,lon=m_longitude,lat=m_latitude;
      reset_camera(); m_longitude=lon; m_latitude=lat; m_pan=pan; m_zoom=zoom;
      QMatrix4x4 view; const double a=lon*pi/180,b=lat*pi/180;
      view.lookAt(QVector3D(float(std::cos(b)*std::cos(a)),float(std::cos(b)*std::sin(a)),float(std::sin(b))),{},
        QVector3D(float(-std::sin(b)*std::cos(a)),float(-std::sin(b)*std::sin(a)),float(std::cos(b))));
      m_rotation=QQuaternion::fromRotationMatrix(view.normalMatrix());
    } else m_rotation=(QQuaternion::rotationTo(arcball(m_last),arcball(point))*m_rotation).normalized();
    update(); emit camera_changed();
  } else if(event->buttons()&(Qt::RightButton|Qt::MiddleButton)) {
    const double scale=2./(std::max(1,std::min(width(),height()))*m_zoom);
    m_pan+=QPointF(delta.x()*scale,-delta.y()*scale); update(); emit camera_changed();
  } else {
    const auto hit=pick_at(point); set_highlight(hit.face);
    setToolTip(hit.hit?QString("face #%1 | node #%2 | lon %3 lat %4 | area %5 sr | %6 %7 (nearest node in point mode)")
      .arg(hit.face).arg(hit.nearest_node).arg(hit.lon_deg,0,'g',8).arg(hit.lat_deg,0,'g',8)
      .arg(m_result->mesh.area[hit.face],0,'g',8).arg(std::isfinite(hit.value)?QString::number(hit.value,'g',10):"missing / masked").arg(m_area?QString("sr"):m_result->source.units):"Outside globe");
  }
  m_last=point; event->accept();
}
void Ico3DCanvas::mouseReleaseEvent(QMouseEvent *event) {
  if(event->button()==Qt::LeftButton&&QLineF(m_press,event->position()).length()<4) {
    const auto hit=pick_at(event->position()); set_highlight(hit.face); emit picked(hit.face,hit.nearest_node);
  }
  event->accept();
}
void Ico3DCanvas::wheelEvent(QWheelEvent *event) { m_zoom=std::clamp(m_zoom*std::pow(1.15,event->angleDelta().y()/120.),.35,8.); update(); emit camera_changed(); event->accept(); }
void Ico3DCanvas::mouseDoubleClickEvent(QMouseEvent *event) { reset_camera(); event->accept(); }
