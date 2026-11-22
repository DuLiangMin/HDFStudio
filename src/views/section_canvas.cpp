#include "section_canvas.hpp"
#include <QWheelEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QPainter>
#include <cmath>
#include <algorithm>

class SectionAxis : public QWidget {
public:
  SectionAxis(SectionCanvas *canvas,bool horizontal):QWidget(canvas),m_canvas(canvas),m_horizontal(horizontal){if(horizontal)setFixedHeight(82);else setFixedWidth(170);}
protected:
  void paintEvent(QPaintEvent *) override{QPainter painter(this);m_canvas->paint_axis(painter,m_horizontal,this);}
private:
  SectionCanvas *m_canvas;bool m_horizontal;
};
SectionCanvas::SectionCanvas(QWidget *parent):QWidget(parent) {
  auto *layout=new QGridLayout(this);layout->setContentsMargins(0,0,0,0);m_y=new SectionAxis(this,false);m_x=new SectionAxis(this,true);m_probe=new QLabel;m_probe->setMinimumHeight(35);m_probe->setWordWrap(true);m_probe->setObjectName("hovmollerProbe");
  m_view=new QGraphicsView(this);m_view->setObjectName("hovmollerCanvas");m_view->setAccessibleName("Space-Time Section image");
  m_view->setScene(new QGraphicsScene(m_view));m_item=m_view->scene()->addPixmap({});m_view->setDragMode(QGraphicsView::ScrollHandDrag);m_view->setMouseTracking(true);m_view->viewport()->setMouseTracking(true);m_view->viewport()->installEventFilter(this);
  layout->addWidget(m_y,0,0);layout->addWidget(m_view,0,1);layout->addWidget(m_x,1,1);layout->addWidget(m_probe,2,0,1,2);layout->setRowStretch(0,1);layout->setColumnStretch(1,1);
  connect(m_view->horizontalScrollBar(),&QScrollBar::valueChanged,this,[this]{axes();emit viewport_changed();});connect(m_view->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{axes();emit viewport_changed();});
}
void SectionCanvas::clear(){m_snapshot.reset();m_probe_valid=false;m_item->setPixmap({});axes();m_probe->clear();m_view->viewport()->setToolTip({});}
void SectionCanvas::set_snapshot(std::shared_ptr<const SectionSnapshot> snapshot) {
  m_snapshot=std::move(snapshot);m_dates.clear();m_item->setPixmap(QPixmap::fromImage(m_snapshot->image.mirrored(m_h,m_v)));m_view->scene()->setSceneRect(m_item->boundingRect());if(m_fit)fit();axes();
}
void SectionCanvas::fit(){m_fit=true;if(!m_item->pixmap().isNull())m_view->fitInView(m_item,Qt::KeepAspectRatio);axes();emit viewport_changed();}
void SectionCanvas::zoom(double scale){m_fit=false;m_view->scale(scale,scale);axes();emit viewport_changed();}
void SectionCanvas::set_flips(bool horizontal,bool vertical){m_h=horizontal;m_v=vertical;if(m_snapshot){m_item->setPixmap(QPixmap::fromImage(m_snapshot->image.mirrored(m_h,m_v)));axes();emit viewport_changed();}}
void SectionCanvas::set_date_labels(const QMap<QString,QString> &labels){m_dates=labels;axes();if(m_probe_valid)update_probe(m_probe_row,m_probe_col);}
std::vector<size_t> SectionCanvas::visible_indices(size_t dimension) const {
  if(!m_snapshot||(dimension!=m_snapshot->spec.row_axis&&dimension!=m_snapshot->spec.col_axis))return {};
  const bool horizontal=dimension==m_snapshot->spec.col_axis;const auto visible=m_view->mapToScene(m_view->viewport()->rect()).boundingRect();const auto length=m_snapshot->info.shape[dimension];
  const int first=std::max(0,int(std::ceil((horizontal?visible.left():visible.top())-.5))),last=std::min(int(length)-1,int(std::floor((horizontal?visible.right():visible.bottom())-.5)));
  if(first>last)return {};std::vector<size_t> indices;for(int position:{first,(first+last+1)/2,last})indices.push_back((horizontal?m_h:m_v)?length-1-size_t(position):size_t(position));return indices;
}
QString SectionCanvas::coordinate(size_t axis,size_t index) const {
  const auto found=m_snapshot->coordinates->find(axis);if(found==m_snapshot->coordinates->end()||index>=found->second.size())return QString("index0 %1").arg(index);
  const double value=found->second[index];if(!std::isfinite(value))return "coordinate missing";
  const auto key=QString::number(axis)+":"+QString::number(value,'g',17);return m_dates.value(key,QString::number(value,'g',10));
}
void SectionCanvas::axes(){m_x->update();m_y->update();}
void SectionCanvas::paint_axis(QPainter &painter,bool horizontal,QWidget *widget){
  if(!m_snapshot)return;
  const size_t dimension=horizontal?m_snapshot->spec.col_axis:m_snapshot->spec.row_axis;const auto &axis=m_snapshot->info.axes[dimension];
  const auto viewport=m_view->viewport();const auto origin=widget->mapFromGlobal(viewport->mapToGlobal(QPoint(0,0)));const auto visible=m_view->mapToScene(viewport->rect()).boundingRect();
  const int first=std::max(0,int(std::ceil((horizontal?visible.left():visible.top())-.5))),last=std::min(int(axis.length)-1,int(std::floor((horizontal?visible.right():visible.bottom())-.5)));
  painter.setPen(palette().color(QPalette::Text));
  if(horizontal)painter.drawText(QRect(0,43,widget->width(),36),Qt::AlignHCenter|Qt::TextWordWrap,axis.dimension_name+" ["+(axis.units.isEmpty()?"index0":axis.units)+"] — source-index spacing");
  else {painter.save();painter.translate(0,widget->height()/2);painter.rotate(-90);painter.drawText(QRect(-widget->height()/2,0,widget->height(),30),Qt::AlignHCenter|Qt::AlignVCenter|Qt::TextWordWrap,axis.dimension_name+" ["+(axis.units.isEmpty()?"index0":axis.units)+"]");painter.restore();}
  if(first>last)return;
  int previous_end=-10000;
  for(int display_index:{first,(first+last+1)/2,last}) {
    if(display_index<0)continue;const bool flip=horizontal?m_h:m_v;const size_t index=flip?axis.length-1-size_t(display_index):size_t(display_index);
    const auto point=m_view->mapFromScene(horizontal?QPointF(display_index+.5,0):QPointF(0,display_index+.5));QString label=coordinate(dimension,index);
    if(horizontal){const int position=origin.x()+point.x();label.replace(' ','\n');const int width=std::min(180,std::max(70,painter.fontMetrics().horizontalAdvance(label.section('\n',0,0))+12));int left=std::clamp(position-width/2,0,std::max(0,widget->width()-width));if(left<previous_end+3)continue;painter.drawLine(position,0,position,5);painter.drawText(QRect(left,7,width,34),Qt::AlignHCenter,label);previous_end=left+width;}
    else {const int position=origin.y()+point.y();const int top=std::clamp(position-17,0,std::max(0,widget->height()-34));if(top<previous_end+3)continue;label.replace(' ','\n');painter.drawLine(widget->width()-6,position,widget->width(),position);painter.drawText(QRect(24,top,widget->width()-34,34),Qt::AlignRight|Qt::AlignVCenter,label);previous_end=top+34;}
  }
}
bool SectionCanvas::eventFilter(QObject *watched,QEvent *event){
  if(watched==m_view->viewport()&&event->type()==QEvent::Wheel){auto *wheel=static_cast<QWheelEvent*>(event);zoom(wheel->angleDelta().y()>0?1.25:.8);return true;}
  if(watched==m_view->viewport()&&event->type()==QEvent::MouseMove&&m_snapshot){auto *mouse=static_cast<QMouseEvent*>(event);const QPointF p=m_view->mapToScene(mouse->position().toPoint());
    const auto rows=m_snapshot->info.shape[m_snapshot->spec.row_axis],cols=m_snapshot->info.shape[m_snapshot->spec.col_axis];
    if(p.x()<0||p.y()<0||p.x()>=cols||p.y()>=rows){m_probe_valid=false;m_probe->clear();return false;}
    const size_t row=m_v?rows-1-size_t(p.y()):size_t(p.y()),col=m_h?cols-1-size_t(p.x()):size_t(p.x());const size_t index=row*cols+col;
    Q_UNUSED(index);m_probe_valid=true;m_probe_row=row;m_probe_col=col;update_probe(row,col);emit probe_changed(row,col,m_probe->text());
  }return QWidget::eventFilter(watched,event);
}
void SectionCanvas::update_probe(size_t row,size_t col){
  const auto index=row*m_snapshot->info.shape[m_snapshot->spec.col_axis]+col;const double value=(*m_snapshot->effective)[index];const QString state=std::isfinite(value)?QString::number(value,'g',17):std::isfinite((*m_snapshot->decoded)[index])?"masked":"missing";
  m_probe->setText(QString("Y index0=%1 (%2); X index0=%3 (%4); Value=%5 %6").arg(row).arg(coordinate(m_snapshot->spec.row_axis,row)).arg(col).arg(coordinate(m_snapshot->spec.col_axis,col)).arg(state,m_snapshot->info.units));
}
void SectionCanvas::resizeEvent(QResizeEvent *event){QWidget::resizeEvent(event);if(m_fit)fit();}
