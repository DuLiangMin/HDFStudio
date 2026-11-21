// Controlled view state: no data, file handles or workspace dependencies.
#pragma once
#include <QTransform>

struct ViewportState {
  QTransform transform;
  double h_ratio = 0.5, v_ratio = 0.5;
  bool flip_h = false, flip_v = false;
};
