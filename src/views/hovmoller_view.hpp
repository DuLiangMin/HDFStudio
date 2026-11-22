// 独立时空剖面；源身份按值持有，所有C库读数在同目录CLI子进程中。
#pragma once
#include <QtWidgets>
#include "layer_navigation.hpp"
#include "slice_job_controller.hpp"
#include "section_canvas.hpp"
#include "color_bar.hpp"
#include "section_dates.hpp"

class HovmollerView : public QWidget, public LayerNavigation {
  Q_OBJECT
public:
  explicit HovmollerView(QString file,QString variable,QWidget *parent=nullptr);
  ~HovmollerView() override;
  int layer_axis_count() const override;
  QString layer_axis_name(int axis) const override;
  int layer_count(int axis) const override;
  int layer_index(int axis) const override;
  void set_layer_index(int axis,int index) override;
  bool ready() const {return m_state=="Ready";}
  QString state() const {return m_state;}
  QString error() const {return m_error;}
  quint64 read_count() const {return m_reads;}
  std::shared_ptr<const SectionSnapshot> snapshot() const {return ready()?m_snapshot:nullptr;}
  void set_axes(size_t row,size_t col);
  void set_fixed_index(size_t dimension,size_t index);
  void cancel();
  QString export_png(const QString &path,bool overwrite=false) const;
  QString export_csv(const QString &path,bool overwrite=false) const;
signals:
  void layer_changed();
  void ready_changed();
  void stop_playback_requested();
  void export_finished(QString error);
protected:
  void closeEvent(QCloseEvent *) override;
private:
  void inspect();
  void request_plane();
  QJsonObject request(const QString &operation) const;
  void received(std::shared_ptr<SliceWorkerData> result,std::shared_ptr<void> lease);
  void build_axes();
  void rebuild_fixed();
  void refresh_fixed();
  void presets(bool choose_default=false);
  void apply_preset(bool latitude);
  void set_state(const QString &state,const QString &error={});
  void render();
  void pump_render();
  void start_dates();
  void cancel_dates();
  void save_dialog(bool png);
  void update_summary();
  bool safe_output(const QString &path) const;
  QString m_file,m_variable,m_state="Idle",m_error;
  QJsonObject m_stamp;
  quint64 m_request_id=0,m_source_revision=1,m_reads=0,m_render_revision=0;
  SliceInfo m_info;
  NcSliceSpec m_spec;
  std::vector<size_t> m_history,m_fixed_axes;
  SliceJobController *m_jobs=nullptr;
  SectionDates *m_dates=nullptr;
  std::shared_ptr<SectionSnapshot> m_data;
  std::shared_ptr<const SectionSnapshot> m_snapshot;
  bool m_render_running=false,m_render_pending=false,m_closing=false,m_exporting=false;
  std::vector<HdfColorMap::Palette> m_palettes;
  QComboBox *m_row,*m_col,*m_palette,*m_range_mode,*m_normalization;
  QPushButton *m_lat_time,*m_lon_time,*m_export_png,*m_export_csv;
  QLineEdit *m_min,*m_max,*m_center,*m_mask_start,*m_mask_end;
  QCheckBox *m_reverse,*m_flip_h,*m_flip_v;
  QLabel *m_summary,*m_status,*m_dates_status;
  QWidget *m_fixed_widget;
  QGridLayout *m_fixed_layout;
  std::map<size_t,QLineEdit*> m_index_edits;
  std::map<size_t,QLabel*> m_fixed_labels;
  std::map<size_t,QPushButton*> m_coordinate_buttons;
  SectionCanvas *m_canvas;
  ColorBar *m_colorbar;
  size_t m_probe_row=0,m_probe_col=0;
  QTimer m_date_debounce;
  QElapsedTimer m_request_timer;
};
