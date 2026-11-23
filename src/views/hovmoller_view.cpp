#include "hovmoller_view.hpp"
#include "raster_budget.hpp"
#include "grib/grib_data.hpp"
#include <QThreadPool>
#include <QJsonDocument>
#include <QJsonArray>
#include <QImageWriter>
#include <QRegularExpressionValidator>
#include <climits>
#include <cmath>
#include <limits>
#include <algorithm>

namespace {
constexpr size_t no_axis=size_t(-1);
QString value_text(double value){return std::isfinite(value)?QString::number(value,'g',17):QString("coordinate missing");}
QString write_png(const QString &path,const std::shared_ptr<const SectionSnapshot> &snapshot,bool overwrite) {
  if(!snapshot)return "No current Ready section";
  if(slice_same_file(path,snapshot->file))return "Cannot overwrite a source dataset";
  return slice_write_atomic(path,overwrite,[&](QIODevice &output){
    QImage image=snapshot->image.mirrored(snapshot->flip_h,snapshot->flip_v);
    image.setText("HovmollerMetadata",QString::fromUtf8(QJsonDocument(snapshot->metadata).toJson(QJsonDocument::Compact)));
    QImageWriter writer(&output,"png");return writer.write(image)?QString():writer.errorString();
  });
}
QString write_csv(const QString &path,const std::shared_ptr<const SectionSnapshot> &snapshot,bool overwrite) {
  if(!snapshot)return "No current Ready section";
  const auto rows=snapshot->coordinates->find(snapshot->spec.row_axis),cols=snapshot->coordinates->find(snapshot->spec.col_axis);
  const std::vector<double> empty;
  NcPlane plane;plane.rows=snapshot->info.shape[snapshot->spec.row_axis];plane.cols=snapshot->info.shape[snapshot->spec.col_axis];plane.values=*snapshot->effective;
  return slice_write_csv(path,plane,*snapshot->decoded,rows==snapshot->coordinates->end()?empty:rows->second,cols==snapshot->coordinates->end()?empty:cols->second,snapshot->metadata,overwrite);
}
}
HovmollerView::HovmollerView(QString file,QString variable,QWidget *parent):QWidget(parent),m_file(QFileInfo(file).absoluteFilePath()),m_variable(variable) {
  m_grib=GribData::is_grib(m_file);
  setObjectName("hovmollerView");setWindowTitle(tr("Space-Time Section (Hovmöller) — %1").arg(variable));
  m_stamp=slice_stamp(m_file);m_jobs=new SliceJobController;m_dates=new SectionDates;
  m_date_debounce.setSingleShot(true);m_date_debounce.setInterval(150);connect(&m_date_debounce,&QTimer::timeout,this,&HovmollerView::start_dates);
  auto *layout=new QVBoxLayout(this);auto *axes=new QHBoxLayout;
  auto button=[&](const QString &text,const QString &name,QBoxLayout *row){auto *b=new QPushButton(text);b->setObjectName(name);b->setAccessibleName(text);row->addWidget(b);return b;};
  m_lat_time=button(tr("Lat–Time"),"hovmollerLatTime",axes);m_lon_time=button(tr("Lon–Time"),"hovmollerLonTime",axes);
  auto *swap=button(tr("Swap Axes"),"hovmollerSwapAxes",axes);
  m_row=new QComboBox;m_row->setObjectName("hovmollerRowAxis");m_row->setAccessibleName(tr("Y-Axis"));m_col=new QComboBox;m_col->setObjectName("hovmollerColAxis");m_col->setAccessibleName(tr("X-Axis"));
  axes->addWidget(new QLabel(tr("Y-Axis")));axes->addWidget(m_row,1);axes->addWidget(new QLabel(tr("X-Axis")));axes->addWidget(m_col,1);layout->addLayout(axes);
  auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setMaximumHeight(125);m_fixed_widget=new QWidget;m_fixed_layout=new QGridLayout(m_fixed_widget);scroll->setWidget(m_fixed_widget);layout->addWidget(scroll);
  auto *colors=new QHBoxLayout;m_palette=new QComboBox;m_palette->setObjectName("hovmollerPalette");m_palette->setAccessibleName(tr("Color map"));QString palette_error;m_palettes=HdfColorMap::load_registry(palette_error);
  for(const auto &palette:m_palettes)m_palette->addItem(color_map_icon(palette),palette.name,palette.id);
  const int default_palette=m_palette->findData(HdfColorMap::default_palette_id);if(default_palette>=0)m_palette->setCurrentIndex(default_palette);
  m_reverse=new QCheckBox(tr("Reverse"));m_reverse->setObjectName("hovmollerReverse");
  m_range_mode=new QComboBox;m_range_mode->setObjectName("hovmollerRangeMode");m_range_mode->addItems({"Auto","Fixed"});m_range_mode->setToolTip(tr("Use Fixed for comparable colors when sweeping fixed positions"));
  auto field=[&](const QString &name,const QString &placeholder){auto *edit=new QLineEdit;edit->setObjectName(name);edit->setAccessibleName(placeholder);edit->setPlaceholderText(placeholder);edit->setMaximumWidth(110);return edit;};
  m_min=field("hovmollerMinimum","Minimum");m_max=field("hovmollerMaximum","Maximum");m_center=field("hovmollerCenter","Center");m_center->setText("0");
  m_normalization=new QComboBox;m_normalization->setObjectName("hovmollerNormalization");m_normalization->addItems({"Linear","Centered"});
  colors->addWidget(m_palette,1);colors->addWidget(m_reverse);colors->addWidget(m_range_mode);colors->addWidget(m_min);colors->addWidget(m_max);colors->addWidget(m_normalization);colors->addWidget(m_center);layout->addLayout(colors);
  auto *tools=new QHBoxLayout;m_mask_start=field("hovmollerMaskStart","Mask start");m_mask_end=field("hovmollerMaskEnd","Mask end");tools->addWidget(new QLabel(tr("Exclude [start, end]")));tools->addWidget(m_mask_start);tools->addWidget(m_mask_end);
  auto *apply=button(tr("Apply"),"hovmollerApply",tools);auto *fit=button(tr("Fit"),"hovmollerFit",tools);auto *plus=button("+","hovmollerZoomIn",tools);auto *minus=button("−","hovmollerZoomOut",tools);
  m_flip_h=new QCheckBox("Flip H");m_flip_v=new QCheckBox("Flip V");m_flip_h->setObjectName("hovmollerFlipH");m_flip_v->setObjectName("hovmollerFlipV");tools->addWidget(m_flip_h);tools->addWidget(m_flip_v);
  auto *cancel_button=button(tr("Cancel"),"hovmollerCancel",tools);layout->addLayout(tools);
  m_summary=new QLabel;m_summary->setObjectName("hovmollerSummary");m_summary->setWordWrap(true);m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(m_summary);
  m_canvas=new SectionCanvas;layout->addWidget(m_canvas,1);m_colorbar=new ColorBar;layout->addWidget(m_colorbar);
  auto *bottom=new QHBoxLayout;m_status=new QLabel;m_status->setObjectName("hovmollerStatus");m_status->setWordWrap(true);bottom->addWidget(m_status,1);m_export_png=button("Export PNG","hovmollerExportPng",bottom);m_export_csv=button("Export CSV","hovmollerExportCsv",bottom);layout->addLayout(bottom);
  m_dates_status=new QLabel(tr("Date labels: numeric until available"));m_dates_status->setObjectName("hovmollerDateStatus");m_dates_status->setAccessibleName(m_dates_status->text());layout->addWidget(m_dates_status);
  for(auto *control:findChildren<QWidget*>())if(control->accessibleName().isEmpty()&&!control->objectName().isEmpty())control->setAccessibleName(control->objectName());
  connect(m_jobs,&SliceJobController::completed,this,&HovmollerView::received);
  connect(m_jobs,&SliceJobController::failed,this,[this](const QString &error){emit stop_playback_requested();set_state("Error",error);});
  connect(m_row,&QComboBox::activated,this,[this](int row){set_axes(row,size_t(row)==m_spec.col_axis?m_spec.row_axis:m_spec.col_axis);});
  connect(m_col,&QComboBox::activated,this,[this](int col){set_axes(size_t(col)==m_spec.row_axis?m_spec.col_axis:m_spec.row_axis,col);});
  connect(swap,&QPushButton::clicked,this,[this]{set_axes(m_spec.col_axis,m_spec.row_axis);});
  connect(m_lat_time,&QPushButton::clicked,this,[this]{apply_preset(true);});connect(m_lon_time,&QPushButton::clicked,this,[this]{apply_preset(false);});
  auto rerender=[this]{emit stop_playback_requested();render();};
  connect(apply,&QPushButton::clicked,this,rerender);
  for(auto *edit:{m_min,m_max,m_center,m_mask_start,m_mask_end})connect(edit,&QLineEdit::editingFinished,this,rerender);
  for(auto *combo:{m_palette,m_range_mode,m_normalization})connect(combo,&QComboBox::activated,this,rerender);
  connect(m_reverse,&QCheckBox::toggled,this,rerender);
  fit->setToolTip(tr("Fill the plot width and height with smooth display interpolation; original samples are unchanged"));
  connect(fit,&QPushButton::clicked,m_canvas,&SectionCanvas::fit);connect(plus,&QPushButton::clicked,this,[this]{m_canvas->zoom(1.25);});connect(minus,&QPushButton::clicked,this,[this]{m_canvas->zoom(.8);});
  for(auto *flip:{m_flip_h,m_flip_v})connect(flip,&QCheckBox::toggled,this,[this]{if(!m_snapshot)return;auto snapshot=std::make_shared<SectionSnapshot>(*m_snapshot);snapshot->flip_h=m_flip_h->isChecked();snapshot->flip_v=m_flip_v->isChecked();snapshot->metadata["flip_h"]=snapshot->flip_h;snapshot->metadata["flip_v"]=snapshot->flip_v;m_snapshot=snapshot;m_canvas->set_flips(snapshot->flip_h,snapshot->flip_v);});
  connect(cancel_button,&QPushButton::clicked,this,&HovmollerView::cancel);
  connect(m_export_png,&QPushButton::clicked,this,[this]{save_dialog(true);});connect(m_export_csv,&QPushButton::clicked,this,[this]{save_dialog(false);});
  connect(m_canvas,&SectionCanvas::probe_changed,this,[this](size_t row,size_t col,const QString &){if(row!=m_probe_row||col!=m_probe_col){m_probe_row=row;m_probe_col=col;m_date_debounce.start();}});
  connect(m_canvas,&SectionCanvas::viewport_changed,this,[this]{if(ready())m_date_debounce.start();});
  connect(m_dates,&SectionDates::labels_ready,this,[this](quint64 revision,const QMap<QString,QString> &labels,bool available){if(!ready()||revision!=m_render_revision)return;m_canvas->set_date_labels(labels);refresh_fixed();for(auto axis:m_fixed_axes)if(m_info.axes[axis].role==SliceAxisRole::Time)for(const auto &fixed:m_snapshot->metadata["fixed_coordinates"].toArray()){const auto value=fixed.toObject();if(value["dimension_index"].toInt()==int(axis)&&value["value"].isDouble()){const auto key=QString::number(axis)+":"+QString::number(value["value"].toDouble(),'g',17);if(labels.contains(key))m_fixed_labels[axis]->setText(m_fixed_labels[axis]->text()+" — "+labels[key]);}}m_dates_status->setText(available?tr("Calendar labels available; source calendar, no local timezone conversion"):tr("Date labels unavailable — numeric time and source calendar retained"));m_dates_status->setAccessibleName(m_dates_status->text());});
  set_state("Inspecting");if(!palette_error.isEmpty())set_state("Error",palette_error);else QTimer::singleShot(0,this,&HovmollerView::inspect);
}
HovmollerView::~HovmollerView(){m_closing=true;++m_request_id;++m_render_revision;cancel_dates();disconnect(m_dates,nullptr,this,nullptr);m_dates->dispose();disconnect(m_jobs,nullptr,this,nullptr);m_jobs->dispose();}
void HovmollerView::closeEvent(QCloseEvent *event){m_closing=true;emit stop_playback_requested();m_jobs->cancel();cancel_dates();QWidget::closeEvent(event);}
void HovmollerView::set_state(const QString &state,const QString &error) {
  m_state=state;m_error=error;m_status->setText(error.isEmpty()?state:state+": "+error);m_status->setAccessibleName(m_status->text());m_export_png->setEnabled(ready()&&!m_exporting);m_export_csv->setEnabled(ready()&&!m_exporting);
  setProperty("sectionState",state);if(!ready()){m_snapshot.reset();m_canvas->clear();m_colorbar->clear();}emit ready_changed();
}
QJsonObject HovmollerView::request(const QString &operation) const {
  QJsonObject object{{"schema",1},{"operation",operation},{"file",m_file},{"variable",m_variable},{"source_stamp",m_stamp},{"request_id",QString::number(m_request_id)},{"source_revision",QString::number(m_source_revision)}};
  if(m_grib){object["schema"]=2;object["source_kind"]="grib";object["decode_profile"]="GribDecodedV1";}
  if(operation=="read") {
    const auto metadata=slice_metadata(m_file,m_variable,m_info,m_spec);object["expected_shape"]=metadata["shape"];object["spec"]=metadata["spec"];
    QJsonArray coordinates;for(size_t axis=0;axis<m_info.shape.size();++axis)if(axis==m_spec.row_axis||axis==m_spec.col_axis||m_info.shape[axis]<=1048576)coordinates.append(int(axis));object["coordinate_axes"]=coordinates;
  }return object;
}
void HovmollerView::inspect(){if(m_closing)return;++m_request_id;set_state("Inspecting");m_jobs->submit(request("inspect"),16*1024*1024);}
void HovmollerView::received(std::shared_ptr<SliceWorkerData> result,std::shared_ptr<void> lease) {
  if(m_closing||result->metadata["request_id"].toString()!=QString::number(m_request_id))return;
  if(slice_stamp(m_file)!=m_stamp){emit stop_playback_requested();set_state("Error","SourceChanged: reopen source file");return;}
  if(result->plane.values.empty()) {
    m_info=result->info;m_history.assign(m_info.shape.size(),0);m_spec.fixed_indices=m_history;build_axes();presets(true);rebuild_fixed();request_plane();emit layer_changed();return;
  }
  if(result->info.shape!=m_info.shape||result->spec.row_axis!=m_spec.row_axis||result->spec.col_axis!=m_spec.col_axis||result->spec.fixed_indices!=m_spec.fixed_indices){set_state("Error","ProtocolError: result does not match requested slice");return;}
  auto data=std::make_shared<SectionSnapshot>();data->file=m_file;data->variable=m_variable;data->info=result->info;data->spec=result->spec;data->coordinates=std::make_shared<const std::map<size_t,std::vector<double>>>(std::move(result->coordinates));data->lease=std::move(lease);
  data->decoded=std::make_shared<const std::vector<double>>(std::move(result->plane.values));data->metadata=slice_metadata(m_file,m_variable,result->info,m_spec);data->metadata["source_stamp"]=m_stamp;data->metadata["source_revision"]=QString::number(m_source_revision);
  for(const char *key:{"fixed_coordinates","precision_warning","diagnostics","reader","timing_ms"})data->metadata[key]=result->metadata[key];
  if(m_grib)data->metadata["missing_fields"]=result->metadata["missing_fields"];
  data->request_id=m_request_id;data->metadata["request_id"]=QString::number(m_request_id);
  m_info=result->info;m_data=std::move(data);refresh_fixed();presets();render();
}
void HovmollerView::build_axes() {
  QSignalBlocker row_block(m_row),col_block(m_col);m_row->clear();m_col->clear();
  for(const auto &axis:m_info.axes){const QString name=QString("%1 (axis %2, %3)").arg(axis.dimension_name).arg(axis.dimension_index).arg(axis.length);m_row->addItem(name);m_col->addItem(name);}
  m_spec.row_axis=0;m_spec.col_axis=1;m_row->setCurrentIndex(0);m_col->setCurrentIndex(1);
}
void HovmollerView::presets(bool choose_default) {
  if(m_grib){
    size_t time=no_axis;for(const auto &axis:m_info.axes)if(axis.dimension_name=="time (reference + step)"&&!axis.source_labels.empty())time=axis.dimension_index;
    const bool available=time!=no_axis&&m_info.shape.size()>2;
    m_lat_time->setText(tr("Grid Y–Time index"));m_lon_time->setText(tr("Time index–Grid X"));
    for(auto *button:{m_lat_time,m_lon_time}){button->setEnabled(available);button->setAccessibleName(button->text());button->setToolTip(tr("GRIB source-grid indices and original reference/step labels; no geographic or elapsed-time resampling"));}
    if(choose_default){m_spec.row_axis=m_info.shape.size()-2;m_spec.col_axis=available?time:0;if(m_spec.row_axis==m_spec.col_axis)m_spec.col_axis=m_info.shape.size()-1;
      QSignalBlocker r(m_row),c(m_col);m_row->setCurrentIndex(int(m_spec.row_axis));m_col->setCurrentIndex(int(m_spec.col_axis));}
    return;
  }
  auto unique=[&](SliceAxisRole role){size_t found=no_axis;for(const auto &axis:m_info.axes)if(axis.role==role){if(found!=no_axis)return no_axis;found=axis.dimension_index;}return found;};
  size_t time=unique(SliceAxisRole::Time),lat=unique(SliceAxisRole::Latitude),lon=unique(SliceAxisRole::Longitude);
  auto coordinates_valid=[&](size_t axis){if(!m_data)return true;const auto found=m_data->coordinates->find(axis);if(found==m_data->coordinates->end())return false;for(double value:found->second)if(!std::isfinite(value)||(axis==lat&&(value< -90||value>90)))return false;return true;};
  bool lat_ok=time!=no_axis&&lat!=no_axis&&(m_info.shape.size()==2||lon!=no_axis)&&coordinates_valid(time)&&coordinates_valid(lat)&&(lon==no_axis||coordinates_valid(lon));
  bool lon_ok=time!=no_axis&&lon!=no_axis&&(m_info.shape.size()==2||lat!=no_axis)&&coordinates_valid(time)&&coordinates_valid(lon)&&(lat==no_axis||coordinates_valid(lat));
  m_lat_time->setEnabled(lat_ok);m_lon_time->setEnabled(lon_ok);
  if(choose_default){if(lat_ok){m_spec.row_axis=lat;m_spec.col_axis=time;}else if(lon_ok){m_spec.row_axis=time;m_spec.col_axis=lon;}QSignalBlocker r(m_row),c(m_col);m_row->setCurrentIndex(int(m_spec.row_axis));m_col->setCurrentIndex(int(m_spec.col_axis));}
}
void HovmollerView::apply_preset(bool latitude) {
  if(!(latitude?m_lat_time:m_lon_time)->isEnabled())return;size_t time=0,space=0;
  if(m_grib){for(const auto &axis:m_info.axes)if(axis.dimension_name=="time (reference + step)")time=axis.dimension_index;
    set_axes(latitude?m_info.shape.size()-2:time,latitude?time:m_info.shape.size()-1);return;}
  for(const auto &axis:m_info.axes){if(axis.role==SliceAxisRole::Time)time=axis.dimension_index;if(axis.role==(latitude?SliceAxisRole::Latitude:SliceAxisRole::Longitude))space=axis.dimension_index;}
  set_axes(latitude?space:time,latitude?time:space);
}
void HovmollerView::set_axes(size_t row,size_t col) {
  if(row>=m_info.shape.size()||col>=m_info.shape.size()||row==col||m_closing)return;
  emit stop_playback_requested();cancel_dates();size_t navigation=no_axis;if(navigation_axis>=0&&navigation_axis<int(m_fixed_axes.size()))navigation=m_fixed_axes[navigation_axis];
  m_spec.row_axis=row;m_spec.col_axis=col;m_spec.fixed_indices=m_history;m_spec.fixed_indices[row]=m_spec.fixed_indices[col]=0;
  QSignalBlocker r(m_row),c(m_col);m_row->setCurrentIndex(int(row));m_col->setCurrentIndex(int(col));rebuild_fixed();navigation_axis=0;
  const auto slot=std::find(m_fixed_axes.begin(),m_fixed_axes.end(),navigation);if(slot!=m_fixed_axes.end())navigation_axis=int(slot-m_fixed_axes.begin());
  m_canvas->fit();request_plane();emit layer_changed();
}
void HovmollerView::rebuild_fixed() {
  m_fixed_axes.clear();m_index_edits.clear();m_fixed_labels.clear();m_coordinate_buttons.clear();
  while(auto *item=m_fixed_layout->takeAt(0)){delete item->widget();delete item;}
  for(size_t axis=0;axis<m_info.shape.size();++axis)if(axis!=m_spec.row_axis&&axis!=m_spec.col_axis) {
    const int row=int(m_fixed_axes.size());m_fixed_axes.push_back(axis);auto *edit=new QLineEdit(QString::number(m_spec.fixed_indices[axis]));edit->setObjectName(QString("hovmollerFixedIndex_%1").arg(axis));edit->setAccessibleName(m_info.axes[axis].dimension_name+" index0");edit->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{1,20}"),edit));edit->setMaximumWidth(135);edit->setToolTip(QString("index0: 0..%1").arg(m_info.shape[axis]-1));
    auto *label=new QLabel;label->setWordWrap(true);auto *locate=new QPushButton(tr("By coordinate…"));locate->setObjectName(QString("hovmollerByCoordinate_%1").arg(axis));locate->setAccessibleName(tr("Locate %1 by coordinate").arg(m_info.axes[axis].dimension_name));locate->setEnabled(false);
    m_fixed_layout->addWidget(new QLabel(m_info.axes[axis].dimension_name+" index0"),row,0);m_fixed_layout->addWidget(edit,row,1);m_fixed_layout->addWidget(label,row,2);m_fixed_layout->addWidget(locate,row,3);m_index_edits[axis]=edit;m_fixed_labels[axis]=label;m_coordinate_buttons[axis]=locate;
    connect(edit,&QLineEdit::editingFinished,this,[this,axis,edit]{bool ok=false;const auto index=edit->text().toULongLong(&ok);emit stop_playback_requested();if(!ok||index>=m_info.shape[axis]){edit->setText(QString::number(m_spec.fixed_indices[axis]));m_status->setText(tr("Index outside source dimension"));return;}set_fixed_index(axis,size_t(index));});
    connect(locate,&QPushButton::clicked,this,[this,axis]{if(!m_data)return;auto coordinates=m_data->coordinates;auto found=coordinates->find(axis);if(found==coordinates->end())return;emit stop_playback_requested();bool ok=false;const auto &descriptor=m_info.axes[axis];QString text=QInputDialog::getText(this,tr("Nearest existing grid point"),QString("%1 [%2], source coordinate system; no longitude wrapping").arg(descriptor.dimension_name,descriptor.units),QLineEdit::Normal,{},&ok);if(!ok)return;bool parsed=false;double requested=text.toDouble(&parsed);size_t index=0;QString error=parsed?slice_nearest(found->second,requested,index):QString("Invalid finite coordinate");if(!error.isEmpty()){QMessageBox::information(this,tr("Coordinate"),error);return;}const double actual=found->second[index];set_fixed_index(axis,index);m_status->setText(QString("%1 → %2 (index0=%3)").arg(text,value_text(actual)).arg(index));});
  }
  if(m_fixed_axes.empty())m_fixed_layout->addWidget(new QLabel(tr("No fixed axis — pre-generated 2D matrix; navigation and playback disabled")),0,0);
}
void HovmollerView::refresh_fixed(){
  for(auto axis:m_fixed_axes) {
    m_index_edits[axis]->setText(QString::number(m_spec.fixed_indices[axis]));QString value="coordinate unavailable";bool enabled=false;
    if(m_data){const auto &labels=m_info.axes[axis].source_labels;if(m_spec.fixed_indices[axis]<size_t(labels.size()))value=labels[int(m_spec.fixed_indices[axis])];
      auto found=m_data->coordinates->find(axis);if(found!=m_data->coordinates->end()){if(m_spec.fixed_indices[axis]<found->second.size())value=value_text(found->second[m_spec.fixed_indices[axis]]);size_t index=0;enabled=!found->second.empty()&&slice_nearest(found->second,found->second.front(),index).isEmpty();if(m_info.axes[axis].role==SliceAxisRole::Latitude)for(double v:found->second)if(v< -90||v>90)enabled=false;}
      else for(const auto &fixed:m_data->metadata["fixed_coordinates"].toArray()){const auto object=fixed.toObject();if(object["dimension_index"].toInt()==int(axis))value=object["value"].isDouble()?value_text(object["value"].toDouble()):"coordinate missing";}}
    m_fixed_labels[axis]->setText(value+" "+m_info.axes[axis].units+(m_info.shape[axis]>INT_MAX?" — toolbar int limit":""));m_coordinate_buttons[axis]->setEnabled(enabled);
  }
}
void HovmollerView::set_fixed_index(size_t axis,size_t index) {
  if(axis>=m_info.shape.size()||axis==m_spec.row_axis||axis==m_spec.col_axis||index>=m_info.shape[axis]||m_spec.fixed_indices[axis]==index||m_closing)return;
  m_history[axis]=index;m_spec.fixed_indices[axis]=index;if(m_index_edits.count(axis))m_index_edits[axis]->setText(QString::number(index));request_plane();emit layer_changed();
}
int HovmollerView::layer_axis_count() const{return int(m_fixed_axes.size());}
QString HovmollerView::layer_axis_name(int slot) const{return slot>=0&&slot<int(m_fixed_axes.size())?m_info.axes[m_fixed_axes[slot]].dimension_name:QString();}
int HovmollerView::layer_count(int slot) const{if(slot<0||slot>=int(m_fixed_axes.size()))return 0;auto count=m_info.shape[m_fixed_axes[slot]];return count>INT_MAX?0:int(count);}
int HovmollerView::layer_index(int slot) const{if(!layer_count(slot))return 0;return int(m_spec.fixed_indices[m_fixed_axes[slot]]);}
void HovmollerView::set_layer_index(int slot,int index){if(slot<0||slot>=int(m_fixed_axes.size())||index<0||index>=layer_count(slot))return;set_fixed_index(m_fixed_axes[slot],size_t(index));}
void HovmollerView::request_plane() {
  if(m_closing||m_info.shape.empty())return;++m_request_id;++m_render_revision;m_request_timer.start();cancel_dates();m_data.reset();m_render_pending=false;set_state("Loading");refresh_fixed();
  if(slice_stamp(m_file)!=m_stamp){emit stop_playback_requested();m_jobs->cancel();set_state("Error","SourceChanged: reopen source file");return;}
  const auto validation=slice_validate(std::vector<size_t>(m_info.shape.begin(),m_info.shape.end()),m_spec);if(!validation.isEmpty()){m_jobs->cancel();set_state("Error",validation);return;}
  quint64 fixed=0;for(auto axis:m_fixed_axes)if(m_info.shape[axis]<=1048576)fixed+=m_info.shape[axis];
  const quint64 pixels=m_info.shape[m_spec.row_axis]*m_info.shape[m_spec.col_axis];const quint64 base=64*pixels+16*fixed+16*1024*1024;
  if(m_info.chunk_bytes>(std::numeric_limits<quint64>::max()-base)/2){set_state("Error","MemoryBudgetExceeded: chunk size overflow");return;}quint64 bytes=base+2*m_info.chunk_bytes;
  if(m_info.source_working_bytes>std::numeric_limits<quint64>::max()-bytes){set_state("Error","MemoryBudgetExceeded: source size overflow");return;}bytes+=m_info.source_working_bytes;
  if(m_grib&&bytes>512ull*1024*1024){m_jobs->cancel();set_state("Error","MemoryBudgetExceeded: section and GRIB source exceed 512 MiB");return;}
  ++m_reads;m_jobs->submit(request("read"),bytes);update_summary();
}
void HovmollerView::cancel(){emit stop_playback_requested();++m_request_id;++m_render_revision;m_render_pending=false;m_jobs->cancel();cancel_dates();m_data.reset();set_state("Cancelled");}
void HovmollerView::render(){
  if(!m_data||m_closing)return;++m_render_revision;m_render_pending=true;set_state("Rendering");pump_render();
}
void HovmollerView::pump_render(){
  if(m_render_running||!m_render_pending||!m_data||m_closing)return;m_render_pending=false;
  const auto source=m_data;const auto request_id=m_request_id,revision=m_render_revision;auto result=std::make_shared<SectionSnapshot>(*source);
  const bool mask=!m_mask_start->text().trimmed().isEmpty()||!m_mask_end->text().trimmed().isEmpty();double start=-INFINITY,end=INFINITY;
  if(!nc_mask_bound(m_mask_start->text(),-INFINITY,start)||!nc_mask_bound(m_mask_end->text(),INFINITY,end)||start>end){set_state("Error","Invalid Mask bounds");return;}
  if(m_palette->currentIndex()<0||m_palette->currentIndex()>=int(m_palettes.size())){set_state("Error","No color map available");return;}
  result->palette=m_palettes[m_palette->currentIndex()];result->mapping.reversed=m_reverse->isChecked();result->mapping.normalization=m_normalization->currentIndex()==1?HdfColorMap::Normalization::Centered:HdfColorMap::Normalization::Linear;
  bool ok=true;if(result->mapping.normalization==HdfColorMap::Normalization::Centered){result->mapping.center=m_center->text().toDouble(&ok);if(!ok||!std::isfinite(result->mapping.center)){set_state("Error","Invalid center");return;}}
  const bool automatic=m_range_mode->currentIndex()==0;
  if(!automatic){bool a,b;result->mapping.minimum=m_min->text().toDouble(&a);result->mapping.maximum=m_max->text().toDouble(&b);if(!a||!b||!std::isfinite(result->mapping.minimum)||!std::isfinite(result->mapping.maximum)){set_state("Error","Invalid fixed range");return;}}
  result->flip_h=m_flip_h->isChecked();result->flip_v=m_flip_v->isChecked();result->render_revision=revision;m_render_running=true;QPointer<HovmollerView> self=this;
  QThreadPool::globalInstance()->start([self,result,request_id,revision,mask,start,end,automatic]{
    QElapsedTimer render_timer;render_timer.start();
    QString error;
    try {
      std::shared_ptr<std::vector<double>> effective;if(mask){effective=std::make_shared<std::vector<double>>(*result->decoded);nc_mask_range(*effective,start,end);result->effective=effective;}else result->effective=result->decoded;
      double minimum=INFINITY,maximum=-INFINITY;
      for(size_t i=0;i<result->decoded->size();++i){const double value=(*result->effective)[i];if(!std::isfinite((*result->decoded)[i]))++result->missing;else if(!std::isfinite(value))++result->masked;else {++result->valid;minimum=std::min(minimum,value);maximum=std::max(maximum,value);}}
      result->has_range=result->valid>0;if(automatic&&result->has_range){result->mapping.minimum=minimum;result->mapping.maximum=maximum;}
      if(result->has_range||!automatic)error=HdfColorMap::validate(result->palette,result->mapping);
      if(error.isEmpty()) {
        const auto rows=result->info.shape[result->spec.row_axis],cols=result->info.shape[result->spec.col_axis];result->image=QImage(int(cols),int(rows),QImage::Format_ARGB32);
        if(result->image.isNull())error="Cannot allocate section image";
        else for(size_t row=0;row<rows;++row){auto *pixels=reinterpret_cast<QRgb*>(result->image.scanLine(int(row)));for(size_t col=0;col<cols;++col){const auto i=row*cols+col;pixels[col]=HdfColorMap::map_value(result->palette,result->mapping,(*result->effective)[i],std::isfinite((*result->decoded)[i])&&!std::isfinite((*result->effective)[i]));}}
      }
  QJsonObject mapping{{"palette",result->palette.id},{"reverse",result->mapping.reversed},{"normalization",int(result->mapping.normalization)},{"mode",automatic?"Auto":"Fixed"},{"minimum",result->has_range||!automatic?QJsonValue(result->mapping.minimum):QJsonValue(QJsonValue::Null)},{"maximum",result->has_range||!automatic?QJsonValue(result->mapping.maximum):QJsonValue(QJsonValue::Null)},{"center",result->mapping.center}};
      result->metadata["color_mapping"]=mapping;result->metadata["render_revision"]=QString::number(revision);result->metadata["flip_h"]=result->flip_h;result->metadata["flip_v"]=result->flip_v;
      result->metadata["mask"]=mask?QJsonValue(QJsonObject{{"start",std::isfinite(start)?QJsonValue(start):QJsonValue(QJsonValue::Null)},{"end",std::isfinite(end)?QJsonValue(end):QJsonValue(QJsonValue::Null)}}):QJsonValue(QJsonValue::Null);
      auto timing=result->metadata["timing_ms"].toObject();timing["render"]=double(render_timer.elapsed());result->metadata["timing_ms"]=timing;
    }catch(const std::exception &exception){error=QString::fromUtf8(exception.what());}
    QMetaObject::invokeMethod(qApp,[self,result,request_id,revision,error]{if(!self)return;self->m_render_running=false;
      if(!self->m_closing&&request_id==self->m_request_id&&revision==self->m_render_revision){
        if(!error.isEmpty())self->set_state("Error",error);
        else {auto timing=result->metadata["timing_ms"].toObject();timing["ready_total"]=double(self->m_request_timer.elapsed());result->metadata["timing_ms"]=timing;self->m_snapshot=result;self->m_state="Ready";self->m_error.clear();self->m_canvas->set_flips(result->flip_h,result->flip_v);self->m_canvas->set_snapshot(result);self->m_colorbar->set_mapping(result->palette,result->mapping,result->info.units,{},result->has_range);self->set_state("Ready");self->update_summary();self->start_dates();}
      }self->pump_render();},Qt::QueuedConnection);
  });
}
void HovmollerView::update_summary(){
  QStringList fixed;for(auto axis:m_fixed_axes){QString value=m_fixed_labels.count(axis)?m_fixed_labels[axis]->text():QString();fixed<<QString("%1 index0=%2 %3").arg(m_info.axes[axis].dimension_name).arg(m_spec.fixed_indices[axis]).arg(value);}
  QString description="Generic axis slice — coordinate semantics unverified";if(m_lat_time->isEnabled()||m_lon_time->isEnabled())description="Space-Time Section — source-index spacing";
  if(m_grib)description="GRIB section — source-grid / reference + step index spacing; original labels; smooth display interpolation";
  QString detail=QString("%1\n%2 — %3 [%4]\nY=%5 / X=%6; %7; shape=(%8,%9)").arg(description,QFileInfo(m_file).fileName(),m_variable,m_info.units,m_info.axes[m_spec.row_axis].dimension_name,m_info.axes[m_spec.col_axis].dimension_name,fixed.join("; ")).arg(m_info.shape[m_spec.row_axis]).arg(m_info.shape[m_spec.col_axis]);
  QStringList diagnostics;for(size_t axis:{m_spec.row_axis,m_spec.col_axis}){const auto &descriptor=m_info.axes[axis];if(!descriptor.diagnostic.isEmpty())diagnostics<<descriptor.dimension_name+": "+descriptor.diagnostic;if(!descriptor.calendar.isEmpty())diagnostics<<"calendar="+descriptor.calendar+" ("+descriptor.calendar_source+")";}if(!diagnostics.isEmpty())detail+="\n"+diagnostics.join("; ");
  if(m_snapshot){detail+=QString("\nValid=%1, missing=%2, masked=%3").arg(m_snapshot->valid).arg(m_snapshot->missing).arg(m_snapshot->masked);if(m_snapshot->metadata["precision_warning"].toBool())detail+="; float64 integer precision warning";}
  m_summary->setText(detail);m_summary->setAccessibleName(detail);
}
bool HovmollerView::safe_output(const QString &path) const {
  if(slice_same_file(path,m_file))return false;
  for(const QObject *root=this;root;root=root->parent())for(const auto &file:root->property("openSourceFiles").toStringList())if(slice_same_file(path,file))return false;return true;
}
QString HovmollerView::export_png(const QString &path,bool overwrite) const {return safe_output(path)?write_png(path,snapshot(),overwrite):QString("Cannot overwrite an open source dataset");}
QString HovmollerView::export_csv(const QString &path,bool overwrite) const {return safe_output(path)?write_csv(path,snapshot(),overwrite):QString("Cannot overwrite an open source dataset");}
void HovmollerView::save_dialog(bool png) {
  if(!ready()||m_exporting)return;emit stop_playback_requested();const auto captured=m_snapshot;
  const auto path=QFileDialog::getSaveFileName(this,png?tr("Export section PNG"):tr("Export section CSV"),{},png?"PNG image (*.png)":"CSV (*.csv)");if(path.isEmpty())return;
  if(!safe_output(path)){QMessageBox::warning(this,tr("Export"),tr("Cannot overwrite an open source dataset"));return;}
  auto lease=reserve_raster_bytes(captured->decoded->size()*16);if(!lease){QMessageBox::warning(this,tr("Export"),tr("MemoryBudgetExceeded"));return;}
  m_exporting=true;m_export_png->setEnabled(false);m_export_csv->setEnabled(false);QPointer<HovmollerView> self=this;
  QThreadPool::globalInstance()->start([self,captured,lease,path,png]{const auto error=png?write_png(path,captured,true):write_csv(path,captured,true);QMetaObject::invokeMethod(qApp,[self,error]{if(!self)return;self->m_exporting=false;self->m_export_png->setEnabled(self->ready());self->m_export_csv->setEnabled(self->ready());if(!error.isEmpty())QMessageBox::warning(self,"Export",error);emit self->export_finished(error);},Qt::QueuedConnection);});
}
void HovmollerView::cancel_dates(){m_date_debounce.stop();m_dates->cancel();}
void HovmollerView::start_dates(){
  if(!m_snapshot||!ready())return;std::vector<SectionDateValue> values;
  for(const auto &axis:m_snapshot->info.axes)if(axis.role==SliceAxisRole::Time&&axis.date_supported){
    const auto found=m_snapshot->coordinates->find(axis.dimension_index);if(found!=m_snapshot->coordinates->end()&&!found->second.empty()){
      std::vector<size_t> indices=m_canvas->visible_indices(axis.dimension_index);
      if(axis.dimension_index==m_snapshot->spec.row_axis)indices.push_back(std::min(m_probe_row,found->second.size()-1));
      if(axis.dimension_index==m_snapshot->spec.col_axis)indices.push_back(std::min(m_probe_col,found->second.size()-1));
      if(axis.dimension_index!=m_snapshot->spec.row_axis&&axis.dimension_index!=m_snapshot->spec.col_axis)indices={m_snapshot->spec.fixed_indices[axis.dimension_index]};
      for(auto index:indices){double value=found->second[index];if(std::isfinite(value))values.push_back({QString::number(axis.dimension_index)+":"+QString::number(value,'g',17),axis.units,axis.calendar,value});}
    }else for(const auto &fixed:m_snapshot->metadata["fixed_coordinates"].toArray()){const auto coordinate=fixed.toObject();if(coordinate["dimension_index"].toInt()==int(axis.dimension_index)&&coordinate["value"].isDouble()){const double value=coordinate["value"].toDouble();values.push_back({QString::number(axis.dimension_index)+":"+QString::number(value,'g',17),axis.units,axis.calendar,value});}}
  }
  if(values.empty()){m_dates_status->setText(tr("Numeric coordinates; calendar conversion unavailable or not applicable"));m_dates_status->setAccessibleName(m_dates_status->text());return;}
  m_dates->submit(m_render_revision,values);
}
