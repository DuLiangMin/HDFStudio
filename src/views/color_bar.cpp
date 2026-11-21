#include "color_bar.hpp"
#include <QPainter>
#include <cmath>
#include <algorithm>

QIcon color_map_icon(const HdfColorMap::Palette &palette) {
  QImage image(128,18,QImage::Format_ARGB32);
  for(int column=0;column<image.width();++column) {
    const auto color=HdfColorMap::sample(palette,double(column)/(image.width()-1),false,palette.stepped);
    for(int row=0;row<image.height();++row) image.setPixel(column,row,color);
  }
  return QIcon(QPixmap::fromImage(image));
}
ColorBar::ColorBar(QWidget *parent):QWidget(parent) {
  setObjectName("colorBar"); setAccessibleName(tr("Colorbar"));
  setMinimumHeight(94); setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
}
void ColorBar::clear() {ready=false;setToolTip({});update();}
void ColorBar::set_mapping(const HdfColorMap::Palette &palette,const HdfColorMap::Mapping &settings,
                          const QString &unit_text,const QStringList &labels,bool range_available) {
  colors=palette; mapping=settings;units=unit_text;categories=labels;ready=true;valid_range=range_available;
  setToolTip(tr("%1\n%2\n%3\n%4").arg(colors.name,units,categories.join("; "),
    mapping.mode==HdfColorMap::Mode::Stepped
      ? (mapping.boundaries.empty()?tr("Equal display intervals; not meteorological thresholds"):tr("Custom decoded-value boundaries"))
      :tr("Continuous color mapping")));
  if(!mapping.boundaries.empty()) {
    QStringList values;for(double value:mapping.boundaries) values<<QString::number(value,'g',15);
    setToolTip(toolTip()+tr("\nBoundaries: %1").arg(values.join(", ")));
  }
  update();
}
void ColorBar::paintEvent(QPaintEvent *) {
  if(!ready||width()<100) return;
  QPainter painter(this); const auto text_color=palette().color(QPalette::WindowText);
  const QFontMetrics metrics(font());
  const int left=44, right=width()-44, top=24, bar_height=16;
  const QString title=colors.name+"  |  "+(units.isEmpty()?tr("units unspecified"):units);
  painter.setPen(text_color); painter.drawText(QRect(8,0,width()-16,22),Qt::AlignLeft|Qt::AlignVCenter,metrics.elidedText(title,Qt::ElideRight,width()-16));
  if(valid_range) for(int column=left;column<=right;++column) {
    const double position=double(column-left)/(right-left);
    uint32_t color;
    if(!categories.empty()) {
      size_t index=std::min(size_t(position*categories.size()),size_t(categories.size()-1));
      color=HdfColorMap::sample(colors,(double(index)+.5)/categories.size(),mapping.reversed);
    } else color=HdfColorMap::map_value(colors,mapping,HdfColorMap::value_at(position,mapping));
    painter.setPen(QColor::fromRgba(color)); painter.drawLine(column,top,column,top+bar_height);
  }
  painter.setPen(text_color);
  if(valid_range) painter.drawRect(left,top,right-left,bar_height);
  else painter.drawText(left,top+bar_height,tr("No finite values — no numeric color range"));
  int ticks=std::max(2,std::min(5,(right-left)/110+1));
  if(categories.empty()&&mapping.minimum==mapping.maximum) ticks=1;
  if(!categories.empty()) ticks=std::min(ticks,int(categories.size()));
  if(!mapping.boundaries.empty()) ticks=std::min(ticks,int(mapping.boundaries.size()));
  int previous_right=-1;
  for(int index=0;valid_range&&index<ticks;++index) {
    double position=ticks==1?.5:double(index)/(ticks-1);
    QString text;
    if(!categories.empty()) {
      const int category=qRound(position*(categories.size()-1));
      position=(double(category)+.5)/categories.size();
      text=categories[category].section(" = ",0,0);
    } else if(!mapping.boundaries.empty()) {
      const int boundary=qRound(position*(mapping.boundaries.size()-1));
      const double value=mapping.boundaries[size_t(boundary)];
      position=HdfColorMap::fraction(value,mapping);
      text=QString::number(value,'g',6);
    } else text=QString::number(HdfColorMap::value_at(position,mapping),'g',6);
    const int xpos=left+qRound(position*(right-left));
    painter.drawLine(xpos,top+bar_height,xpos,top+bar_height+4);
    const int text_width=metrics.horizontalAdvance(text);
    const int text_left=std::clamp(xpos-text_width/2,0,std::max(0,width()-text_width));
    if(text_left>previous_right+4) {
      painter.drawText(text_left,top+bar_height+metrics.height()+3,text);
      previous_right=text_left+text_width;
    }
  }
  if(valid_range&&categories.empty()&&mapping.normalization!=HdfColorMap::Normalization::Cyclic) {
    if(mapping.custom_under) {
      painter.setBrush(QColor::fromRgba(mapping.under));
      painter.drawPolygon(QPolygon{QPoint(left-3,top),QPoint(left-13,top+bar_height/2),QPoint(left-3,top+bar_height)});
    }
    if(mapping.custom_over) {
      painter.setBrush(QColor::fromRgba(mapping.over));
      painter.drawPolygon(QPolygon{QPoint(right+3,top),QPoint(right+13,top+bar_height/2),QPoint(right+3,top+bar_height)});
    }
    painter.setBrush(Qt::NoBrush);
  }
  // Source missing and user mask are different states even if a palette contains white.
  const int baseline=height()-8;
  painter.fillRect(10,baseline-11,11,11,QColor::fromRgba(HdfColorMap::missing_color));
  painter.drawRect(10,baseline-11,11,11); painter.drawText(27,baseline,combined_missing_mask ? tr("Missing / Mask") : tr("Missing"));
  if(!combined_missing_mask) {
    painter.fillRect(115,baseline-11,11,11,Qt::white); painter.drawRect(115,baseline-11,11,11);
    painter.drawText(132,baseline,tr("Mask"));
  }
  QString note;
  if(!categories.empty()) note=tr("Category keys: %1").arg(categories.join("; "));
  else if(mapping.normalization==HdfColorMap::Normalization::Cyclic) note=tr("Cyclic: origin %1, period %2").arg(mapping.origin).arg(mapping.period);
  else if(!mapping.boundaries.empty()) {
    QStringList values;for(double value:mapping.boundaries) values<<QString::number(value,'g',6);
    note=tr("Boundaries: %1").arg(values.join(", "));
  }
  else if(mapping.mode==HdfColorMap::Mode::Stepped) note=tr("Equal display intervals");
  else if(mapping.normalization==HdfColorMap::Normalization::Centered) note=tr("Center %1").arg(mapping.center);
  else note=tr("Linear");
  if(mapping.custom_under||mapping.custom_over) note+=tr("; custom under/over");
  painter.drawText(205,baseline,metrics.elidedText(note,Qt::ElideRight,std::max(0,width()-215)));
}
