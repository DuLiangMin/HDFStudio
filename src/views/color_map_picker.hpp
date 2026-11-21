#pragma once
#include "color_map.hpp"
#include <QDialog>
class QComboBox;
class QLineEdit;
class QCheckBox;
class ColorMapPicker : public QDialog {
  Q_OBJECT
public:
  ColorMapPicker(const std::vector<HdfColorMap::Palette> &palettes,int selected,
                 const HdfColorMap::Mapping &mapping,QWidget *parent=nullptr);
  int selected_index() const;
  HdfColorMap::Mapping selected_mapping() const;
private:
  const std::vector<HdfColorMap::Palette> &palettes;
  HdfColorMap::Mapping initial;
  QComboBox *selector,*mode,*normalization;
  QLineEdit *center,*origin,*period,*boundaries;
  QCheckBox *reverse,*custom_under,*custom_over;
  uint32_t under,over;
  HdfColorMap::Mapping result_mapping;
  void apply();
};
