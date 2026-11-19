// Restricted UGRID 2D triangular face preview.
#pragma once
#include <QtWidgets>
#include "nc_reader.hpp"

class MeshView : public QWidget {
  Q_OBJECT
public:
  MeshView(QString file,std::string dataPath);
protected:
  bool eventFilter(QObject *watched,QEvent *event) override;
private:
  void render();
  QString file;
  std::string dataPath;
  NcMesh mesh;
  QGraphicsView *view;
  QGraphicsScene *scene;
  QLabel *legend;
  std::vector<QPolygonF> facePolygons;
  QRectF plotBounds;
};
