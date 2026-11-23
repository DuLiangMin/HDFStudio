#include "compare_panel.hpp"
#include "image_metrics.hpp"
#include "slice_io.hpp"
#include <QSaveFile>
#include <cmath>
#include <algorithm>

namespace {
QString number(double value,bool valid=true) {return valid && std::isfinite(value) ? QString::number(value,'g',15) : QString("N/A");}
QString indices_text(const std::vector<size_t> &indices) {
  QStringList result;for(size_t index:indices)result<<QString::number(index);return result.join(',');
}
QString csv_row(const QStringList &values) {
  QStringList escaped;for(QString value:values){value.replace('"',"\"\"");escaped<<'"'+value+'"';}return escaped.join(',')+"\r\n";
}
}
ComparePanel::ComparePanel(WorkspaceSyncController *controller,QWidget *parent):QDockWidget(tr("Compare & Metrics"),parent),m_controller(controller) {
  setObjectName("compareMetricsDock");setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea|Qt::BottomDockWidgetArea);
  auto *content=new QWidget(this);setWidget(content);auto *layout=new QVBoxLayout(content);auto *form=new QFormLayout;layout->addLayout(form);
  auto combo=[content](const char *name,const QString &accessible) {auto *widget=new QComboBox(content);widget->setObjectName(name);widget->setAccessibleName(accessible);widget->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);widget->setMinimumContentsLength(18);return widget;};
  m_anchor=combo("compareAnchorCombo",tr("Anchor image"));m_target=combo("compareTargetCombo",tr("Target image"));m_axis=combo("compareAxisCombo",tr("Lag axis"));
  form->addRow(tr("Anchor"),m_anchor);form->addRow(tr("Target"),m_target);form->addRow(tr("Index axis"),m_axis);
  m_lag=new QSpinBox(content);m_lag->setObjectName("compareLagSpin");m_lag->setAccessibleName(tr("Lag offset"));m_lag->setRange(-10000,10000);form->addRow(tr("Lag (index steps)"),m_lag);
  m_layer=new QSpinBox(content);m_layer->setObjectName("compareAnchorLayerSpin");m_layer->setAccessibleName(tr("Anchor layer index"));form->addRow(tr("Anchor index (0-based)"),m_layer);
  m_slider=new QSlider(Qt::Horizontal,content);m_slider->setObjectName("compareAnchorLayerSlider");m_slider->setAccessibleName(tr("Anchor layer slider"));layout->addWidget(m_slider);
  auto checkbox=[&](const char *name,const QString &text,bool checked){auto *widget=new QCheckBox(text,content);widget->setObjectName(name);widget->setChecked(checked);layout->addWidget(widget);return widget;};
  m_sync_layer=checkbox("compareSyncLayer",tr("Sync layer indices"),true);
  m_sync_viewport=checkbox("compareSyncViewport",tr("Sync zoom / pan / flips"),true);
  m_sync_colormap=checkbox("compareSyncColormap",tr("Sync colors / resolved range"),false);
  auto *note=new QLabel(tr("Target index = Anchor index + lag. Index alignment only; time coordinates and units are not converted."),content);note->setWordWrap(true);layout->addWidget(note);
  m_metrics=new QPlainTextEdit(content);m_metrics->setObjectName("compareMetricsText");m_metrics->setAccessibleName(tr("Comparison metrics"));m_metrics->setReadOnly(true);m_metrics->setMinimumHeight(210);layout->addWidget(m_metrics,1);
  auto *buttons=new QHBoxLayout;layout->addLayout(buttons);
  auto button=[&](const char *name,const QString &label,const QString &accessible){auto *widget=new QPushButton(label,content);widget->setObjectName(name);widget->setAccessibleName(accessible);buttons->addWidget(widget);return widget;};
  auto *copy=button("compareCopyButton",tr("Copy"),tr("Copy metrics"));
  auto *export_button=button("compareExportCsvButton",tr("CSV..."),tr("Export comparison CSV"));
  m_residual_button=button("compareResidualButton",tr("Residual map"),tr("Residual map"));
  connect(copy,&QPushButton::clicked,this,[this]{refresh_now();QApplication::clipboard()->setText(m_metrics->toPlainText());});
  connect(export_button,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getSaveFileName(this,tr("Export comparison CSV"),{},tr("CSV (*.csv)"));if(path.isEmpty())return;const auto error=export_csv(path);if(!error.isEmpty())QMessageBox::warning(this,tr("Export comparison"),error);});
  connect(m_residual_button,&QPushButton::clicked,this,&ComparePanel::show_residual);
  connect(m_anchor,&QComboBox::currentIndexChanged,this,[this]{if(!m_editing)select_anchor();});
  connect(m_target,&QComboBox::currentIndexChanged,this,[this]{if(!m_editing)select_target();});
  connect(m_axis,&QComboBox::currentIndexChanged,this,[this]{if(!m_editing)apply_binding();});
  connect(m_lag,&QSpinBox::valueChanged,this,[this]{if(!m_editing)apply_binding();});
  for(auto *check:{m_sync_layer,m_sync_viewport,m_sync_colormap})connect(check,&QCheckBox::toggled,this,[this]{if(!m_editing)apply_binding();});
  connect(m_layer,&QSpinBox::valueChanged,this,[this](int index){if(!m_editing)if(auto *view=selected_view(m_anchor))view->set_layer_index(m_axis->currentIndex(),index);});
  connect(m_slider,&QSlider::valueChanged,m_layer,&QSpinBox::setValue);
  connect(controller,&WorkspaceSyncController::metrics_updated,this,&ComparePanel::schedule_refresh);
  connect(controller,&WorkspaceSyncController::sync_changed,this,&ComparePanel::schedule_refresh);
  set_views({});
}
RasterView *ComparePanel::selected_view(QComboBox *combo) const {
  const int index=combo->currentIndex()-1;return index>=0 && index<m_views.size() ? m_views[index].data() : nullptr;
}
void ComparePanel::set_views(const QList<RasterView*> &views) {
  const QPointer<RasterView> previous_anchor=selected_view(m_anchor),previous_target=selected_view(m_target);
  for(const auto &connection:m_connections)disconnect(connection);m_connections.clear();
  m_editing=true;QSignalBlocker anchor_block(m_anchor),target_block(m_target);
  m_anchor->clear();m_target->clear();m_anchor->addItem(tr("None"));m_target->addItem(tr("None"));m_views.clear();
  int a=0,b=0;
  for(auto *view:views)if(view) {
    m_views.append(view);const auto snapshot=view->plane_snapshot();
    const QString label=tr("%1: %2 | %3").arg(m_views.size()).arg(QFileInfo(snapshot.file_path).fileName(),snapshot.variable_path);
    m_anchor->addItem(label);m_target->addItem(label);
    m_anchor->setItemData(m_views.size(),snapshot.file_path,Qt::ToolTipRole);m_target->setItemData(m_views.size(),snapshot.file_path,Qt::ToolTipRole);
    if(view==previous_anchor)a=m_views.size();if(view==previous_target)b=m_views.size();
    m_connections.push_back(connect(view,&RasterView::plane_snapshot_changed,this,[this,view]{
      if(view==selected_view(m_anchor)||view==selected_view(m_target))invalidate_current();
      schedule_refresh();
    }));
    m_connections.push_back(connect(view,&QObject::destroyed,this,[this]{
      invalidate_current();
      QTimer::singleShot(0,this,[this]{QList<RasterView*> live;for(const auto &view:m_views)if(view)live.append(view);set_views(live);});
    }));
  }
  if(!m_initialized && !m_views.empty()) {a=1;b=m_views.size()>1?2:0;m_initialized=true;}
  m_anchor->setCurrentIndex(a);m_target->setCurrentIndex(b);m_editing=false;
  if(a && !m_controller->anchor())select_anchor();else select_target();
}
void ComparePanel::select_anchor() {
  auto *view=selected_view(m_anchor);
  m_controller->set_anchor(view);
  if(selected_view(m_target)==view){QSignalBlocker block(m_target);m_target->setCurrentIndex(0);}
  select_target();
}
void ComparePanel::select_target() {
  m_editing=true;
  auto *anchor=selected_view(m_anchor);auto *target=selected_view(m_target);
  QSignalBlocker axis_block(m_axis);m_axis->clear();
  if(anchor)for(int i=0;i<anchor->layer_axis_count();++i)m_axis->addItem(QString("%1: %2").arg(i).arg(anchor->layer_axis_name(i)));
  SyncBinding binding;binding.view=target;
  for(const auto &existing:m_controller->followers())if(existing.view==target)binding=existing;
  m_axis->setCurrentIndex(m_axis->count()>0 ? std::clamp(binding.lag_axis,0,m_axis->count()-1) : -1);
  m_lag->setValue(binding.lag_offset);m_sync_layer->setChecked(binding.sync_layer);m_sync_viewport->setChecked(binding.sync_viewport);m_sync_colormap->setChecked(binding.sync_colormap);
  m_editing=false;apply_binding();
}
void ComparePanel::apply_binding() {
  auto *anchor=selected_view(m_anchor);auto *target=selected_view(m_target);
  if(anchor && target && anchor!=target) {
    SyncBinding binding;binding.view=target;binding.lag_axis=std::max(0,m_axis->currentIndex());binding.lag_offset=m_lag->value();
    binding.sync_layer=m_sync_layer->isChecked();binding.sync_viewport=m_sync_viewport->isChecked();binding.sync_colormap=m_sync_colormap->isChecked();
    m_controller->add_or_update_follower(binding);
  }
  schedule_refresh();
}
void ComparePanel::invalidate_current() {
  m_residual_button->setEnabled(false);
  if(m_residual)m_residual->clear_result(tr("Current planes are updating or unavailable"));
  m_metrics->setPlainText(tr("Pairing: by index\nStatus: Updating current planes\nRMSE: N/A"));
}
void ComparePanel::schedule_refresh() {
  // 控制器可能在RasterView的QObject析构阶段发信号，此处不能读取派生成员。
  if(m_refresh_pending)return;m_refresh_pending=true;
  QTimer::singleShot(0,this,[this]{m_refresh_pending=false;refresh_now();});
}
void ComparePanel::refresh_now() {
  auto *anchor=selected_view(m_anchor);auto *target=selected_view(m_target);
  m_a=anchor?anchor->plane_snapshot():PlaneSnapshot();m_b=target?target->plane_snapshot():PlaneSnapshot();
  const int axis=std::max(0,m_axis->currentIndex());
  m_editing=true;
  const bool has_layers=anchor && anchor->layer_axis_count()>axis && anchor->layer_count(axis)>0;
  m_layer->setEnabled(has_layers);m_slider->setEnabled(has_layers);
  m_layer->setRange(0,has_layers?anchor->layer_count(axis)-1:0);m_slider->setRange(m_layer->minimum(),m_layer->maximum());
  m_layer->setValue(has_layers?anchor->layer_index(axis):0);m_slider->setValue(m_layer->value());m_editing=false;
  const QString lag=QString(m_lag->value()>=0?"+%1":"%1").arg(m_lag->value());
  m_alignment=tr("Anchor[%1] <-> Target[%2] (lag %3; axis %4, index steps)").arg(indices_text(m_a.leading_indices),indices_text(m_b.leading_indices),lag).arg(axis);
  QString status;NcPairMetrics pair;
  if(!anchor || !target || anchor==target)status=tr("Select two different image views");
  else if(m_a.out_of_range || m_b.out_of_range)status=tr("Out of range");
  else if(!m_a.values || !m_b.values)status=tr("Plane unavailable");
  else {
    if(!m_computed || m_computed_a.values!=m_a.values || m_computed_b.values!=m_b.values ||
       m_computed_a.rows!=m_a.rows || m_computed_a.cols!=m_a.cols || m_computed_b.rows!=m_b.rows || m_computed_b.cols!=m_b.cols) {
      m_pair=nc_pair_metrics(*m_a.values,m_a.rows,m_a.cols,*m_b.values,m_b.rows,m_b.cols);
      m_computed_a=m_a;m_computed_b=m_b;m_computed=true;
      setProperty("metricsComputeCount",property("metricsComputeCount").toULongLong()+1);
    }
    pair=m_pair;
    status=pair.valid?tr("Ready"):pair.error;
  }
  if(!anchor || !target || !m_a.values || !m_b.values || m_a.out_of_range || m_b.out_of_range) {
    m_computed=false;m_computed_a={};m_computed_b={};
  }
  const QString diagnostic=plane_pair_diagnostics(m_a,m_b);
  const QString units=m_a.units==m_b.units && plane_units_known(m_a.units)?m_a.units:tr("unverified: anchor %1 / target %2").arg(m_a.units,m_b.units);
  m_metrics->setPlainText(tr("%1\nStatus: %2\nShape: %3x%4 / %5x%6\nValid pairs: %7\nExcluded pairs: %8\nPearson r: %9\nRMSE: %10\nMAE: %11\nBias (Target - Anchor): %12\nUnits: %13\nRevisions: %14 / %15\nLinked followers: %16")
    .arg(m_alignment,status).arg(m_a.rows).arg(m_a.cols).arg(m_b.rows).arg(m_b.cols)
    .arg(pair.valid_pairs).arg(pair.excluded_pairs).arg(number(pair.pearson_r,pair.r_valid),number(pair.rmse,pair.valid),number(pair.mae,pair.valid),number(pair.bias,pair.valid),units)
     .arg(m_a.revision).arg(m_b.revision).arg(m_controller->followers().size()));
  m_metrics->appendPlainText(tr("Pairing: by index; no resampling or coordinate matching"));
  if(!diagnostic.isEmpty())m_metrics->appendPlainText(tr("Diagnostics: %1").arg(diagnostic));
  m_residual_button->setEnabled(pair.valid);
  if(m_residual) {
    if(!pair.valid)m_residual->clear_result(status);
    else if(m_residual->isVisible())m_residual->update_planes(m_a,m_b,m_alignment+tr("\nPairing: by index; ")+diagnostic);
  }
  QStringList headers={"status","anchor_file","anchor_variable","anchor_indices","anchor_revision","anchor_units","target_file","target_variable","target_indices","target_revision","target_units","lag_axis","lag_offset","sync_layer","sync_viewport","sync_colormap","anchor_rows","anchor_cols","target_rows","target_cols","valid_pairs","excluded_pairs","pearson_r","rmse","mae","bias_target_minus_anchor"};
  QStringList values={status,m_a.file_path,m_a.variable_path,indices_text(m_a.leading_indices),QString::number(m_a.revision),m_a.units,m_b.file_path,m_b.variable_path,indices_text(m_b.leading_indices),QString::number(m_b.revision),m_b.units,QString::number(axis),QString::number(m_lag->value()),QString::number(m_sync_layer->isChecked()),QString::number(m_sync_viewport->isChecked()),QString::number(m_sync_colormap->isChecked()),QString::number(m_a.rows),QString::number(m_a.cols),QString::number(m_b.rows),QString::number(m_b.cols),QString::number(pair.valid_pairs),QString::number(pair.excluded_pairs),number(pair.pearson_r,pair.r_valid),number(pair.rmse,pair.valid),number(pair.mae,pair.valid),number(pair.bias,pair.valid)};
  headers << "pairing" << "diagnostics" << "anchor_grid_identity" << "target_grid_identity" << "anchor_time_identity" << "target_time_identity";
  values << "by index" << diagnostic << m_a.grid_identity << m_b.grid_identity << m_a.time_identity << m_b.time_identity;
  m_csv=csv_row(headers)+csv_row(values);
}
QString ComparePanel::comparison_csv() const {return m_csv;}
QString ComparePanel::export_csv(const QString &path) {
  refresh_now();
  if((!m_a.file_path.isEmpty()&&slice_same_file(path,m_a.file_path))||(!m_b.file_path.isEmpty()&&slice_same_file(path,m_b.file_path)))return tr("Cannot overwrite a source dataset");
  for(const QObject *root=this;root;root=root->parent())for(const auto &file:root->property("openSourceFiles").toStringList())if(slice_same_file(path,file))return tr("Cannot overwrite an open source dataset");
  QSaveFile output(path);if(!output.open(QIODevice::WriteOnly))return output.errorString();
  const auto bytes=m_csv.toUtf8();if(output.write(bytes)!=bytes.size() || !output.commit())return output.errorString();return {};
}
void ComparePanel::show_residual() {
  if(!m_residual)m_residual=new ResidualView(this);
  m_residual->show();m_residual->raise();refresh_now();
}
