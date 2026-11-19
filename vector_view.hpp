// 2D vector-field preview with synchronized leading-axis selections.
#pragma once
#include <QtWidgets>
#include <string>
#include <vector>
#include "hdf5.h"

class VectorView : public QWidget {
  Q_OBJECT
public:
  VectorView(QString file,std::string uPath,std::string vPath,std::vector<hsize_t> shape);
private:
  void refresh();
  QString file;
  std::string uPath,vPath;
  std::vector<hsize_t> shape;
  std::vector<QSpinBox*> indices;
  QLabel *canvas;
  QLabel *legend;
};
