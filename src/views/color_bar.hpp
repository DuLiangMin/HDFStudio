// The same mapping drives this colorbar, palette cards and raster pixels.
#pragma once
#include "color_map.hpp"
#include <QWidget>
#include <QIcon>

QIcon color_map_icon(const HdfColorMap::Palette &palette);
class ColorBar : public QWidget {
  Q_OBJECT
public:
  explicit ColorBar(QWidget *parent=nullptr);
  void set_mapping(const HdfColorMap::Palette &palette,const HdfColorMap::Mapping &mapping,
                   const QString &units, const QStringList &categories={},bool valid_range=true);
  void clear();
  void set_compact(bool enabled);
  void set_combined_missing_mask(bool combined) { combined_missing_mask = combined; update(); }
protected:
  void paintEvent(QPaintEvent *) override;
private:
  HdfColorMap::Palette colors;
  HdfColorMap::Mapping mapping;
  QString units;
  QStringList categories;
  bool ready=false;
  bool valid_range=true;
  bool combined_missing_mask=false;
  bool compact=false;
};
