#include "mesh_view.hpp"
#include "raster_view.hpp"
#include <algorithm>
#include <cmath>

MeshView::MeshView(QString fileName,std::string path)
  :file(std::move(fileName)),dataPath(std::move(path)) {
  setWindowTitle(tr("UGRID Mesh: %1").arg(QString::fromUtf8(dataPath.c_str())));
  auto *layout=new QVBoxLayout(this);
  scene=new QGraphicsScene(this);
  view=new QGraphicsView(scene,this);
  view->setDragMode(QGraphicsView::ScrollHandDrag);
  view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  view->viewport()->setMouseTracking(true);
  view->viewport()->installEventFilter(this);
  layout->addWidget(view,1);
  legend=new QLabel(this);
  layout->addWidget(legend);
  NcReader reader(file);
  mesh=reader.read_ugrid_faces(dataPath);
  if(!mesh.valid) {legend->setText(tr("UGRID mesh unavailable: %1").arg(mesh.error));return;}
  render();
}

void MeshView::render() {
  scene->clear();facePolygons.clear();
  if(mesh.faceCount==0 || mesh.nodeCount==0) {legend->setText(tr("Mesh contains no faces or nodes."));return;}
  const auto [minLonIt,maxLonIt]=std::minmax_element(mesh.longitude.begin(),mesh.longitude.end());
  const auto [minLatIt,maxLatIt]=std::minmax_element(mesh.latitude.begin(),mesh.latitude.end());
  const double lonSpan=*maxLonIt-*minLonIt,latSpan=*maxLatIt-*minLatIt;
  if(!std::isfinite(lonSpan)||!std::isfinite(latSpan)||lonSpan<=0||latSpan<=0) {
    legend->setText(tr("Mesh coordinate extent is degenerate."));return;
  }
  const double scale=900.0/std::max(lonSpan,latSpan);
  const double width=lonSpan*scale,height=latSpan*scale;
  plotBounds=QRectF(0,0,width,height);
  auto *background=scene->addRect(plotBounds,QPen(Qt::NoPen),QBrush(QColor(248,249,247)));
  background->setZValue(-2);
  double minimum=INFINITY,maximum=-INFINITY;
  for(double value:mesh.faceValues) if(std::isfinite(value)) {
    minimum=std::min(minimum,value);maximum=std::max(maximum,value);
  }
  for(size_t face=0;face<mesh.faceCount;++face) {
    QPolygonF polygon;
    for(size_t corner=0;corner<3;++corner) {
      size_t node=mesh.faceNodes[face*3+corner];
      polygon << QPointF((mesh.longitude[node]-*minLonIt)*scale,( *maxLatIt-mesh.latitude[node])*scale);
    }
    facePolygons.push_back(polygon);
    QColor fill=RasterColorMap::colorValue(RasterColorMap::Palette::Viridis,
        mesh.faceValues[face],minimum,maximum,false);
    if(!std::isfinite(mesh.faceValues[face])) fill=QColor(110,110,110);
    scene->addPolygon(polygon,QPen(QColor(46,59,63),0.7),QBrush(fill));
  }
  scene->setSceneRect(plotBounds);
  view->fitInView(plotBounds,Qt::KeepAspectRatio);
  const auto range=std::isfinite(minimum)?QString("%1..%2").arg(minimum,0,'g',8).arg(maximum,0,'g',8):tr("all missing");
  legend->setText(tr("UGRID 2D triangles | %1 nodes | %2 faces | face values %3 | coordinates: %4 / %5")
      .arg(mesh.nodeCount).arg(mesh.faceCount).arg(range)
      .arg(QString::fromUtf8(mesh.latitudePath.c_str()),QString::fromUtf8(mesh.longitudePath.c_str())));
}

bool MeshView::eventFilter(QObject *watched,QEvent *event) {
  if(watched==view->viewport() && event->type()==QEvent::MouseMove && !facePolygons.empty()) {
    auto *mouse=static_cast<QMouseEvent*>(event);
    const QPointF position=view->mapToScene(mouse->pos());
    for(size_t face=0;face<facePolygons.size();++face) {
      if(facePolygons[face].containsPoint(position,Qt::OddEvenFill)) {
        QString coordinate=tr("face %1, value %2").arg(face).arg(mesh.faceValues[face],0,'g',10);
        legend->setToolTip(coordinate);
        return QWidget::eventFilter(watched,event);
      }
    }
    legend->setToolTip(tr("Outside mesh faces."));
  }
  return QWidget::eventFilter(watched,event);
}
