#include "residual_view.hpp"
#include "raster_budget.hpp"
#include "slice_io.hpp"
#include <QSaveFile>
#include <cmath>
#include <algorithm>

namespace {
class ResidualCanvas : public QGraphicsView {
public:
  using QGraphicsView::QGraphicsView;
protected:
  void wheelEvent(QWheelEvent *event) override {
    const double current=std::abs(transform().m11());
    const double steps=double(event->angleDelta().y())/120;
    if(steps==0 || current<=0) {event->ignore();return;}
    const double target=std::clamp(current*std::pow(1.25,std::clamp(steps,-128.0,128.0)),.002,2048.0);
    scale(target/current,target/current);event->accept();
  }
};
}
ResidualView::ResidualView(QWidget *parent):QWidget(parent,Qt::Window) {
  setObjectName("residualView");setWindowTitle(tr("Residual map: Target - Anchor"));resize(780,620);
  auto *layout=new QVBoxLayout(this);auto *tools=new QToolBar(this);layout->addWidget(tools);
  auto *scene=new QGraphicsScene(this);m_image=scene->addPixmap(QPixmap());
  m_canvas=new ResidualCanvas(scene,this);m_canvas->setAccessibleName(tr("Residual image"));
  m_canvas->setDragMode(QGraphicsView::ScrollHandDrag);m_canvas->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  tools->addAction(tr("Zoom in"),this,[this]{m_canvas->scale(1.25,1.25);});
  tools->addAction(tr("Zoom out"),this,[this]{m_canvas->scale(.8,.8);});
  tools->addAction(tr("Fit"),this,[this]{m_canvas->fitInView(m_canvas->sceneRect(),Qt::KeepAspectRatio);});
  m_export_action=tools->addAction(tr("Export PNG"),this,[this]{
    const auto image=image_for_export();if(image.isNull()) return;
    const auto path=QFileDialog::getSaveFileName(this,tr("Export residual PNG"),{},tr("PNG (*.png)"));if(path.isEmpty())return;
    const auto error=export_png(path);
    if(!error.isEmpty())QMessageBox::warning(this,tr("Export residual"),error);
  });
  m_export_action->setObjectName("residualExportPngAction");
  layout->addWidget(m_canvas,1);m_bar=new ColorBar(this);layout->addWidget(m_bar);
  m_summary=new QLabel(this);m_summary->setWordWrap(true);m_summary->setObjectName("residualSummary");layout->addWidget(m_summary);
  clear_result(tr("Select two matching image planes"));
}
void ResidualView::clear_result(const QString &message) {
  m_residual={};m_image->setPixmap(QPixmap());m_bar->clear();m_summary->setText(message);
  m_reservation.reset();m_reserved_pixels=0;setProperty("residualValid",false);
  m_anchor={};m_target={};m_label.clear();
  m_export_action->setEnabled(false);
}
void ResidualView::update_planes(const PlaneSnapshot &anchor,const PlaneSnapshot &target,const QString &label) {
  if(anchor.out_of_range || target.out_of_range || !anchor.values || !target.values) {clear_result(tr("Out of range or unavailable plane"));return;}
  if(m_residual.valid && m_anchor.values==anchor.values && m_target.values==target.values &&
     m_anchor.revision==anchor.revision && m_target.revision==target.revision && m_label==label) return;
  const quint64 pixels=quint64(anchor.values->size());
  if(!pixels || pixels>16*1024*1024) {clear_result(tr("Residual exceeds the 16 megapixel limit"));return;}
  if(!m_reservation || m_reserved_pixels!=pixels) {
    m_reservation.reset();m_reservation=reserve_raster(pixels);m_reserved_pixels=pixels;
    if(!m_reservation) {clear_result(tr("Raster memory budget reached"));return;}
  }
  m_residual=nc_pair_residual(*anchor.values,anchor.rows,anchor.cols,*target.values,target.rows,target.cols);
  if(!m_residual.valid) {const auto error=m_residual.error;clear_result(error);return;}
  QString error;const auto palettes=HdfColorMap::load_registry(error);
  const HdfColorMap::Palette *selected=nullptr;
  for(const auto &palette:palettes) if(palette.id=="ncl:BlueWhiteOrangeRed") selected=&palette;
  if(!selected) {clear_result(tr("Residual diverging color map is unavailable"));return;}
  HdfColorMap::Mapping mapping;
  mapping.normalization=HdfColorMap::Normalization::Centered;
  const double extent=m_residual.absolute_max>0 ? m_residual.absolute_max : 1;
  mapping.minimum=-extent;mapping.maximum=extent;mapping.center=0;
  QImage image(int(m_residual.cols),int(m_residual.rows),QImage::Format_RGB32);
  if(image.isNull()) {clear_result(tr("Cannot allocate residual image"));return;}
  for(int row=0;row<image.height();++row) {
    auto *line=reinterpret_cast<QRgb*>(image.scanLine(row));
    for(int col=0;col<image.width();++col) line[col]=HdfColorMap::map_value(*selected,mapping,m_residual.values[size_t(row)*m_residual.cols+size_t(col)]);
  }
  const bool first=m_image->pixmap().isNull();m_image->setPixmap(QPixmap::fromImage(image));
  m_canvas->scene()->setSceneRect(0,0,image.width(),image.height());
  const QString units=anchor.units==target.units && plane_units_known(anchor.units) ? anchor.units : tr("unverified: target %1 / anchor %2").arg(target.units,anchor.units);
  m_bar->set_mapping(*selected,mapping,units);
  m_summary->setText(tr("%1\nTarget - Anchor; %2 x %3; valid %4; excluded %5; units: %6%7")
    .arg(label).arg(m_residual.cols).arg(m_residual.rows).arg(m_residual.valid_pairs).arg(m_residual.excluded_pairs).arg(units,
      m_residual.absolute_max==0 ? tr("; all residuals zero (display range -1..1)") : QString()));
  setProperty("residualValid",true);setProperty("anchorRevision",anchor.revision);setProperty("targetRevision",target.revision);
  m_anchor=anchor;m_target=target;m_label=label;
  m_export_action->setEnabled(true);
  if(first) m_canvas->fitInView(m_canvas->sceneRect(),Qt::KeepAspectRatio);
}
QImage ResidualView::image_for_export() const {return m_residual.valid ? m_image->pixmap().toImage() : QImage();}

QString ResidualView::export_png(const QString &path) const {
  if((!m_anchor.file_path.isEmpty()&&slice_same_file(path,m_anchor.file_path))||(!m_target.file_path.isEmpty()&&slice_same_file(path,m_target.file_path)))return tr("Cannot overwrite a source dataset");
  for(const QObject *root=this;root;root=root->parent())for(const auto &file:root->property("openSourceFiles").toStringList())if(slice_same_file(path,file))return tr("Cannot overwrite an open source dataset");
  const auto image=image_for_export();if(image.isNull())return tr("No residual available");
  QSaveFile output(path);if(!output.open(QIODevice::WriteOnly))return output.errorString();
  if(!image.save(&output,"PNG")||!output.commit())return tr("Cannot save residual PNG: %1").arg(output.errorString());
  return {};
}
