#pragma once
#include <QtWidgets>
#include "curve_profile.hpp"
#include <vector>
#include <memory>

class ProfileCanvas : public QWidget {
  Q_OBJECT
public:
  explicit ProfileCanvas(QWidget *parent = nullptr);
  void set_profile(CurveProfileResult profile);
  const CurveProfileResult &profile() const { return m_profile; }
protected:
  void paintEvent(QPaintEvent *event) override;
private:
  CurveProfileResult m_profile;
};

class ProfileView : public QWidget {
  Q_OBJECT
public:
  ProfileView(const QString &title,
              std::shared_ptr<const std::vector<double>> decoded_values,
              size_t rows, size_t cols,
               std::vector<QPointF> control_points,
               CurvePathMode path_mode = CurvePathMode::SmoothSpline,
               QWidget *parent = nullptr,
               std::shared_ptr<void> storage_lease = {});

  void update_data(std::shared_ptr<const std::vector<double>> decoded_values,
                    size_t rows, size_t cols,
                    std::vector<QPointF> control_points,
                    CurvePathMode path_mode = CurvePathMode::SmoothSpline,
                    std::shared_ptr<void> storage_lease = {});
private:
  void recompute();
  void export_csv();

  std::shared_ptr<void> m_storage_lease;
  std::shared_ptr<const std::vector<double>> m_values;
  size_t m_rows = 0;
  size_t m_cols = 0;
  std::vector<QPointF> m_controls;
  CurvePathMode m_path_mode = CurvePathMode::SmoothSpline;
  QComboBox *m_interp = nullptr;
  QLabel *m_summary = nullptr;
  ProfileCanvas *m_canvas = nullptr;
  QAction *m_export_action = nullptr;
};
