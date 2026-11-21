// Live derived residual map, using the same numeric decoder contract and colorbar.
#pragma once
#include <QtWidgets>
#include "plane_snapshot.hpp"
#include "image_metrics.hpp"
#include "color_bar.hpp"

class ResidualView : public QWidget {
  Q_OBJECT
public:
  explicit ResidualView(QWidget *parent=nullptr);
  void update_planes(const PlaneSnapshot &anchor,const PlaneSnapshot &target,const QString &label);
  void clear_result(const QString &message);
  const NcResidualPlane &residual() const {return m_residual;}
  QImage image_for_export() const;
private:
  QGraphicsView *m_canvas;
  QGraphicsPixmapItem *m_image;
  QLabel *m_summary;
  ColorBar *m_bar;
  NcResidualPlane m_residual;
  std::shared_ptr<void> m_reservation;
  quint64 m_reserved_pixels=0;
  PlaneSnapshot m_anchor,m_target;
  QString m_label;
};
