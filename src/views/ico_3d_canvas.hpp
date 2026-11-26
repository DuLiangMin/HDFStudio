// Ico3DCanvas copied/adapted from HDFStudio_to_Isosahedral ico_3d_view.*.
#pragma once
#include <QtWidgets>
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QQuaternion>
#include "ico_3d_mesh.hpp"
class Ico3DCanvas : public QOpenGLWidget,protected QOpenGLFunctions_3_3_Core {
  Q_OBJECT
public:
  explicit Ico3DCanvas(QWidget *parent=nullptr);
  ~Ico3DCanvas() override;
  void set_data(std::shared_ptr<const Icosahedral::Result> result,bool area,
                const HdfColorMap::Palette &palette,const HdfColorMap::Mapping &mapping);
  void clear_data();
  void set_morph(double value);
  double morph() const { return m_morph; }
  double zoom() const { return m_zoom; }
  QQuaternion rotation() const { return m_rotation; }
  void reset_camera(int preset=0);
  void center_on(double longitude,double latitude);
  void set_highlight(int face);
  bool base_edges=true,sub_mesh=true,seams=false,graticule=true,turntable=false,shading=false;
  bool ready() const { return m_ready; }
  bool data_pending()const{return m_dirty;}
  void set_frame_current(bool current){m_current=current;if(!current){set_highlight(-1);setToolTip({});}}
  QString gl_error() const { return m_error; }
  Ico3DPickHit pick_at(const QPointF &point) const;
signals:
  void picked(int face,int node);
  void camera_changed();
  void gl_status_changed();
protected:
  void initializeGL() override;
  void resizeGL(int,int) override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
private:
  void cleanup();
  QMatrix4x4 matrix() const;
  QVector3D arcball(const QPointF &point) const;
  std::shared_ptr<const Icosahedral::Result> m_result;
  Ico3DBuffers m_buffers;
  QOpenGLShaderProgram *m_program=nullptr;
  GLuint m_vao=0,m_vbo=0;
  bool m_ready=false,m_dirty=true,m_area=false,m_current=false;
  QString m_error;
  QQuaternion m_rotation;
  QPointF m_last,m_press,m_pan;
  double m_zoom=.82,m_morph=1.,m_longitude=0,m_latitude=20;
  int m_highlight=-1;
};
