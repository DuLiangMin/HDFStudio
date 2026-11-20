#include "vector_view.hpp"
#include "nc_reader.hpp"
#include <algorithm>
#include <cmath>

VectorView::VectorView(QString fileName,std::string u,std::string v,std::vector<hsize_t> dimensions)
  :file(std::move(fileName)),uPath(std::move(u)),vPath(std::move(v)),shape(std::move(dimensions)) {
  setWindowTitle(tr("Vector View: %1 / %2").arg(QString::fromUtf8(uPath.c_str()),QString::fromUtf8(vPath.c_str())));
  auto *layout=new QVBoxLayout(this);
  auto *tools=new QToolBar(this);
  layout->addWidget(tools);
  for(size_t axis=0;axis+2<shape.size();++axis) {
    tools->addWidget(new QLabel(tr("Axis %1:").arg(axis),tools));
    auto *spin=new QSpinBox(tools);
    spin->setRange(0,shape[axis]?int(std::min<hsize_t>(shape[axis]-1,INT_MAX)):0);
    spin->setKeyboardTracking(false);
    tools->addWidget(spin);
    indices.push_back(spin);
    connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,[this]{refresh();});
  }
  canvas=new QLabel(this);
  canvas->setAlignment(Qt::AlignCenter);
  layout->addWidget(canvas,1);
  legend=new QLabel(tr("U/V direction only; units and magnitude scale are not inferred."),this);
  layout->addWidget(legend);
  QTimer::singleShot(0,this,[this]{refresh();});
}

void VectorView::refresh() {
  if(shape.size()<2 || shape[shape.size()-1]==0 || shape[shape.size()-2]==0 ||
     shape.back()>INT_MAX || shape[shape.size()-2]>INT_MAX ||
     shape.back()>16*1024*1024/shape[shape.size()-2]) {
    legend->setText(tr("Invalid or oversized vector plane.")); return;
  }
  std::vector<size_t> selected;
  for(auto *spin:indices) selected.push_back(size_t(spin->value()));
  NcReader reader(file);
  NcVectorPlane planes;
  const QString error=reader.read_vector_planes(uPath,vPath,selected,planes);
  if(!error.isEmpty()) {legend->setText(error);canvas->clear();return;}
  QImage image(int(planes.u.cols),int(planes.u.rows),QImage::Format_RGB32);
  image.fill(QColor(248,249,247));
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(31,91,109),1.2));
  const int spacing=std::max(1,int(std::max(planes.u.rows,planes.u.cols)/24));
  const double scale=spacing*0.38;
  for(size_t y=spacing/2;y<planes.u.rows;y+=spacing) for(size_t x=spacing/2;x<planes.u.cols;x+=spacing) {
    const size_t offset=y*planes.u.cols+x;
    const double u=planes.u.values[offset],v=planes.v.values[offset];
    if(!std::isfinite(u)||!std::isfinite(v)) continue;
    const double magnitude=std::hypot(u,v);
    if(magnitude==0) continue;
    const double length=scale*std::clamp(magnitude/(magnitude+1.0),0.25,1.0);
    const QPointF center(x,y), end(x+u/magnitude*length,y-v/magnitude*length);
    painter.drawLine(center,end);
    const double angle=std::atan2(-(end.y()-center.y()),end.x()-center.x());
    constexpr double head=3.0;
    painter.drawLine(end,QPointF(end.x()-head*std::cos(angle-0.55),end.y()+head*std::sin(angle-0.55)));
    painter.drawLine(end,QPointF(end.x()-head*std::cos(angle+0.55),end.y()+head*std::sin(angle+0.55)));
  }
  painter.end();
  canvas->setPixmap(QPixmap::fromImage(image));
  legend->setText(tr("%1 x %2   U/V direction only; magnitude and units not interpreted; missing pairs omitted.")
      .arg(planes.u.cols).arg(planes.u.rows));
}
