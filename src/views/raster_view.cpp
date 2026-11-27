// Read one scalar plane as doubles, mask missing values, render in index order.
#include "raster_view.hpp"
#include "profile_view.hpp"
#include "color_map_picker.hpp"
#include "nc_reader.hpp"
#include "nc_classic.hpp"
#include "plane_reader.hpp"
#include "image_metrics.hpp"
#include "raster_budget.hpp"
#include "data_read_controller.hpp"
#include "map_display.hpp"
#include "geo_grid_controller.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>

RasterColorMap::Range RasterColorMap::resolveRange(bool automatic,
    double dataMinimum, double dataMaximum, double fixedMinimum,
    double fixedMaximum, bool hasValidValues) {
  if(!automatic) {
    bool valid=std::isfinite(fixedMinimum) && std::isfinite(fixedMaximum) &&
               fixedMinimum < fixedMaximum;
    return {fixedMinimum, fixedMaximum, valid};
  }
  return {dataMinimum, dataMaximum,
          hasValidValues && std::isfinite(dataMinimum) && std::isfinite(dataMaximum)};
}

QColor RasterColorMap::color(Palette palette, double fraction, bool reversed) {
  struct Stop { double position; QRgb color; };
  static const Stop viridis[] = {{0.0,qRgb(68,1,84)},{0.25,qRgb(59,82,139)},{0.5,qRgb(33,145,140)},{0.75,qRgb(94,201,98)},{1.0,qRgb(253,231,37)}};
  static const Stop plasma[] = {{0.0,qRgb(13,8,135)},{0.25,qRgb(126,3,168)},{0.5,qRgb(204,71,120)},{0.75,qRgb(248,149,64)},{1.0,qRgb(240,249,33)}};
  static const Stop inferno[] = {{0.0,qRgb(0,0,4)},{0.25,qRgb(87,15,109)},{0.5,qRgb(188,55,84)},{0.75,qRgb(249,142,9)},{1.0,qRgb(252,255,164)}};
  static const Stop turbo[] = {{0.0,qRgb(48,18,59)},{0.25,qRgb(50,131,246)},{0.5,qRgb(35,213,112)},{0.75,qRgb(250,185,40)},{1.0,qRgb(122,4,3)}};
  static const Stop grayscale[] = {{0.0,qRgb(0,0,0)},{1.0,qRgb(255,255,255)}};
  const Stop *stops = viridis;
  size_t count = std::size(viridis);
  switch(palette) {
    case Palette::Plasma: stops=plasma; count=std::size(plasma); break;
    case Palette::Inferno: stops=inferno; count=std::size(inferno); break;
    case Palette::Turbo: stops=turbo; count=std::size(turbo); break;
    case Palette::Grayscale: stops=grayscale; count=std::size(grayscale); break;
    case Palette::Viridis: break;
  }
  double t = std::clamp(std::isfinite(fraction) ? fraction : 0.5, 0.0, 1.0);
  if(reversed) t = 1.0-t;
  size_t right=1;
  while(right+1<count && t>stops[right].position) ++right;
  double local=(t-stops[right-1].position)/(stops[right].position-stops[right-1].position);
  QColor a=QColor::fromRgb(stops[right-1].color), b=QColor::fromRgb(stops[right].color);
  return QColor(qRound(a.red()+(b.red()-a.red())*local),
                qRound(a.green()+(b.green()-a.green())*local),
                qRound(a.blue()+(b.blue()-a.blue())*local));
}

QColor RasterColorMap::colorValue(Palette palette, double value, double minimum,
                                  double maximum, bool reversed) {
  if(!std::isfinite(value)) return QColor(110,110,110);
  const double fraction = maximum > minimum
    ? (value - minimum) / (maximum - minimum) : 0.5;
  return color(palette, fraction, reversed);
}

QColor RasterColorMap::categoryColor(Palette palette, size_t categoryIndex,
                                     size_t categoryCount, bool reversed) {
  if(categoryCount==0) return QColor(255,0,255);
  const double center=(double(categoryIndex)+0.5)/categoryCount;
  return color(palette,center,reversed);
}

QStringList RasterColorMap::categoryLabels(const std::vector<double> &values,
                                           const QStringList &meanings) {
  QStringList labels;
  labels.reserve(int(values.size()));
  for(size_t i=0;i<values.size();++i) {
    const QString value=QString::number(values[i],'g',12);
    labels << (int(i)<meanings.size()
      ? QString("%1 = %2").arg(value,meanings[int(i)]) : value);
  }
  return labels;
}

QColor RasterColorMap::discreteValueColor(Palette palette, double value,
    const std::vector<double> &categories, bool reversed, const QColor &unknownColor) {
  if(!std::isfinite(value)) return QColor(110,110,110);
  auto found=std::find(categories.begin(),categories.end(),value);
  if(found==categories.end()) return unknownColor;
  return categoryColor(palette,size_t(found-categories.begin()),categories.size(),reversed);
}

namespace {
// A status message may shrink horizontally, but must never add another toolbar row.
class ProjectionStatusLabel final : public QLabel {
public:
  explicit ProjectionStatusLabel(const QString &text,QWidget *parent):QLabel(text,parent){
    setWordWrap(false);setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);setMinimumWidth(0);
  }
protected:
  void paintEvent(QPaintEvent*)override{
    QPainter painter(this);painter.setPen(palette().color(foregroundRole()));
    painter.drawText(contentsRect(),Qt::AlignLeft|Qt::AlignVCenter,fontMetrics().elidedText(text(),Qt::ElideRight,contentsRect().width()));
  }
};
struct Handle {
  hid_t id;
  herr_t (*close)(hid_t);
  ~Handle() { if(id >= 0) close(id); }
};

bool scalar_attr(hid_t dataset, const char *name, double &out) {
  if(H5Aexists(dataset, name) <= 0) return false;
  Handle attr{H5Aopen(dataset, name, H5P_DEFAULT), H5Aclose};
  if(attr.id < 0) return false;
  Handle space{H5Aget_space(attr.id), H5Sclose};
  return space.id >= 0 && H5Sget_simple_extent_npoints(space.id) == 1 &&
         H5Aread(attr.id, H5T_NATIVE_DOUBLE, &out) >= 0;
}

std::vector<double> numeric_array_attr(hid_t object, const char *name) {
  if(H5Aexists(object,name)<=0) return {};
  Handle attr{H5Aopen(object,name,H5P_DEFAULT),H5Aclose};
  if(attr.id<0) return {};
  Handle space{H5Aget_space(attr.id),H5Sclose};
  if(space.id<0) return {};
  hssize_t points=H5Sget_simple_extent_npoints(space.id);
  if(points<=0 || points>12) return {};
  std::vector<double> values(static_cast<size_t>(points), 0.0);
  if(H5Aread(attr.id,H5T_NATIVE_DOUBLE,values.data())<0) return {};
  return values;
}

QString text_array_attr(hid_t object, const char *name) {
  if(H5Aexists(object,name)<=0) return {};
  Handle attr{H5Aopen(object,name,H5P_DEFAULT),H5Aclose};
  if(attr.id<0) return {};
  Handle type{H5Aget_type(attr.id),H5Tclose};
  if(type.id<0 || H5Tget_class(type.id)!=H5T_STRING) return {};
  if(H5Tis_variable_str(type.id)>0) {
    Handle space{H5Aget_space(attr.id),H5Sclose};
    if(space.id<0||H5Sget_simple_extent_npoints(space.id)!=1) return {};
    char *value=nullptr;
    if(H5Aread(attr.id,type.id,&value)<0) return {};
    const QString text=value?QString::fromUtf8(value).trimmed():QString();
    if(value) H5free_memory(value);
    return text;
  }
  size_t size=H5Tget_size(type.id);
  if(size==0 || size>65536) return {};
  Handle space{H5Aget_space(attr.id),H5Sclose};
  if(space.id<0 || H5Sget_simple_extent_npoints(space.id)!=1) return {};
  std::vector<char> text(size+1,'\0');
  if(H5Aread(attr.id,type.id,text.data())<0) return {};
  return QString::fromUtf8(text.data()).trimmed();
}

QIcon zoom_icon(bool zoom_in) {
  QPixmap image(24,24);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(43,90,160),2.0,Qt::SolidLine,Qt::RoundCap));
  painter.setBrush(Qt::NoBrush);
  painter.drawEllipse(QRectF(3,3,13,13));
  painter.drawLine(QPointF(14.5,14.5),QPointF(21,21));
  painter.drawLine(QPointF(6.5,9.5),QPointF(12.5,9.5));
  if(zoom_in) painter.drawLine(QPointF(9.5,6.5),QPointF(9.5,12.5));
  return QIcon(image);
}

class Canvas : public QGraphicsView {
public:
  using QGraphicsView::QGraphicsView;
  std::function<void()> viewport_changed_callback;
  void zoom(double factor) {
    zoom_at(factor,QPointF(viewport()->rect().center()),true);
  }
  void wheelEvent(QWheelEvent *event) override {
    double steps=double(event->angleDelta().y())/120.0;
    if(steps==0) steps=double(event->pixelDelta().y())/120.0;
    if(steps==0) {event->ignore();return;}
    zoom_at(std::pow(1.25,std::clamp(steps,-128.0,128.0)),event->position(),false);
    event->accept();
  }
  void resizeEvent(QResizeEvent *event) override {
    QGraphicsView::resizeEvent(event);
    if(viewport_changed_callback) viewport_changed_callback();
  }
private:
  void zoom_at(double factor,const QPointF &position,bool around_center) {
    // The scale magnitude changes, but the signs still encode Flip H / Flip V.
    const double current=std::abs(transform().m11());
    if(!std::isfinite(current)||current<=0||!std::isfinite(factor)||factor<=0) return;
    const double target=std::clamp(current*factor,0.002,2048.0);
    if(target==current) return;
    const QPointF scene_point=viewportTransform().inverted().map(position);
    const auto previous_anchor=transformationAnchor();
    setTransformationAnchor(QGraphicsView::NoAnchor);
    scale(target/current,target/current);
    // Explicit viewport coordinates avoid anchoring a button click at the mouse
    // position outside the image. Scrollbars may appear after scaling.
    const QPointF desired=around_center?QPointF(viewport()->rect().center()):position;
    const QPointF offset=viewportTransform().map(scene_point)-desired;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value()+qRound(offset.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value()+qRound(offset.y()));
    setTransformationAnchor(previous_anchor);
    if(viewport_changed_callback) viewport_changed_callback();
  }
};
}

RasterView::RasterView(std::string filename, std::string varpath, std::vector<hsize_t> shape)
  :RasterView(std::move(filename),std::move(varpath),std::move(shape),std::nullopt) {}

RasterView::RasterView(QString filename,QString target,const DataSource::ArrayInfo &info)
  :RasterView(filename.toUtf8().toStdString(),target.toUtf8().toStdString(),
              std::vector<hsize_t>(info.shape.begin(),info.shape.end()),info) {}

RasterView::RasterView(std::string filename,std::string varpath,std::vector<hsize_t> shape,
                       std::optional<DataSource::ArrayInfo> descriptor)
  :file(std::move(filename)),path(std::move(varpath)),grib_info(std::move(descriptor)),dims(std::move(shape)) {
  if(grib_info)grib_file=QString::fromUtf8(file.c_str());
  setWindowTitle(QString::fromUtf8(path.c_str()) + tr(" - Image"));
  auto layout = new QVBoxLayout(this);
  auto tools = new QToolBar(this);
  layout->addWidget(tools);
  // Keep projection selection outside overflowing palette/mask toolbars.
  auto *projection_bar=new QFrame(this);projection_bar->setObjectName("projectionBar");
  projection_bar->setStyleSheet("QFrame#projectionBar { background: #edf5fc; border: 1px solid #8baecb; border-radius: 4px; } QComboBox#mapProjectionSelector { background: white; padding: 4px 8px; border: 1px solid #557f9f; border-radius: 3px; }");
  auto *projection_row=new QHBoxLayout(projection_bar);projection_row->setContentsMargins(8,3,8,3);projection_row->setSpacing(6);
  auto *projection_label=new QLabel(tr("Map projection"),projection_bar);auto projection_font=projection_label->font();projection_font.setBold(true);projection_label->setFont(projection_font);projection_row->addWidget(projection_label);
  projection_selector=new QComboBox(projection_bar);projection_selector->setObjectName("mapProjectionSelector");projection_selector->setAccessibleName(tr("Map projection — select to redraw"));projection_selector->setMinimumContentsLength(22);projection_selector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);projection_selector->setMinimumWidth(190);projection_selector->setMinimumHeight(30);
  for(auto mode:{MapProjection::Mode::Source,MapProjection::Mode::LonLat,MapProjection::Mode::NorthPolar,MapProjection::Mode::SouthPolar,MapProjection::Mode::Lambert,MapProjection::Mode::Mercator})projection_selector->addItem(MapProjection::name(mode),int(mode));
  projection_label->setBuddy(projection_selector);projection_selector->setToolTip(tr("Select a projection to redraw immediately; no Apply click is needed."));projection_row->addWidget(projection_selector);
  auto geo_action=new QAction(tr("Options…"),this);
  projection_action=geo_action;
  geo_action->setObjectName("projsAction");
  if(!grib_info)classic=is_classic_netcdf_file(QString::fromLocal8Bit(file.c_str()));
  geo_action->setToolTip(tr("Edit projection parameters, extent and reference coastlines"));
  auto *projection_settings=new QToolButton(projection_bar);projection_settings->setObjectName("projectionSettings");projection_settings->setDefaultAction(geo_action);projection_row->addWidget(projection_settings);
  projection_status=new ProjectionStatusLabel(tr("Select to redraw automatically"),projection_bar);projection_status->setObjectName("projectionStatus");projection_row->addWidget(projection_status,1);layout->insertWidget(0,projection_bar);
  connect(geo_action,&QAction::triggered,this,[this]{projection_dialog_pending=true;prepare_map_coordinates();});
  palette_selector = new QComboBox(tools);
  palette_selector->setObjectName("colorMapSelector");
  palette_selector->setAccessibleName(tr("CMaps"));
  palette_selector->setIconSize(QSize(80,14));
  palette_selector->setMinimumContentsLength(16);
  palette_selector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  QString palette_error;
  color_maps=HdfColorMap::load_registry(palette_error);
  fill_palette_selector();
  const int default_palette=palette_selector->findData(HdfColorMap::default_palette_id);
  if(default_palette>=0)palette_selector->setCurrentIndex(default_palette);
  if(!palette_error.isEmpty()) QTimer::singleShot(0,this,[this,palette_error]{QMessageBox::warning(this,tr("Color map resources"),palette_error);});
  palette_selector->setToolTip(tr("Continuous raster color palette"));
  auto *palette_label = new QLabel(tr("CMaps"), tools);
  palette_label->setObjectName("colorMapsLabel");
  palette_label->setToolTip(tr("Color map for the image and colorbar"));
  palette_label->setBuddy(palette_selector);
  tools->addWidget(palette_label);
  tools->addWidget(palette_selector);
  reverse_palette = new QCheckBox(tr("Reverse"), tools);
  reverse_palette->setObjectName("reverseColorMap");
  reverse_palette->setToolTip(tr("Reverse the selected color palette"));
  tools->addWidget(reverse_palette);
  connect(palette_selector, &QComboBox::currentIndexChanged, this, [this] {
    const auto &selected=color_maps[size_t(palette_selector->currentIndex())];
    color_mapping.mode=selected.stepped?HdfColorMap::Mode::Stepped:HdfColorMap::Mode::Continuous;
    color_mapping.boundaries.clear();
    // Choosing colors alone never guesses a center, units, period or business threshold.
    color_mapping.normalization=HdfColorMap::Normalization::Linear;
    refresh();
  });
  connect(reverse_palette, &QCheckBox::toggled, this, [this] { refresh(); });
  range_mode = new QComboBox(tools);
  range_mode->setObjectName("colorRangeMode");
  range_mode->addItem(tr("Automatic range"), true);
  range_mode->addItem(tr("Fixed range"), false);
  tools->addWidget(range_mode);
  fixed_minimum = new QDoubleSpinBox(tools);
  fixed_minimum->setObjectName("colorMinimum");
  fixed_minimum->setRange(-1.0e300, 1.0e300);
  fixed_minimum->setDecimals(8);
  fixed_minimum->setPrefix(tr("Min "));
  tools->addWidget(fixed_minimum);
  fixed_maximum = new QDoubleSpinBox(tools);
  fixed_maximum->setObjectName("colorMaximum");
  fixed_maximum->setRange(-1.0e300, 1.0e300);
  fixed_maximum->setDecimals(8);
  fixed_maximum->setPrefix(tr("Max "));
  tools->addWidget(fixed_maximum);
  fixed_minimum->setEnabled(false);
  fixed_maximum->setEnabled(false);
  connect(range_mode, &QComboBox::currentIndexChanged, this, [this] {
    synced_color_range.reset();
    bool fixed=!range_mode->currentData().toBool();
    fixed_minimum->setEnabled(fixed);
    fixed_maximum->setEnabled(fixed);
    refresh();
  });
  connect(fixed_minimum, &QDoubleSpinBox::valueChanged, this, [this] { synced_color_range.reset();refresh(); });
  connect(fixed_maximum, &QDoubleSpinBox::valueChanged, this, [this] { synced_color_range.reset();refresh(); });
  auto discrete_toggle=new QCheckBox(tr("Discrete"),tools);
  discrete_toggle->setObjectName("discreteColors");
  discrete_toggle->setToolTip(tr("Render each distinct value as a category"));
  tools->addWidget(discrete_toggle);
  connect(discrete_toggle,&QCheckBox::toggled,this,[this](bool enabled){discrete_mode=enabled;refresh();});

  // Mask Range works on decoded values, the numbers shown in the legend; the file's valid range
  // attributes are in stored units, so both forms are shown in the tooltip to avoid mixing them up
  const NcPacking packing=grib_info?NcPacking{}:nc_packing(QString::fromLocal8Bit(file.c_str()),path);
  QString mask_tip=tr("Mask Range: decoded values from start to end (inclusive) become NaN and are drawn white.\n"
                      "Empty start = -infinity, empty end = +infinity; both empty = no mask.\n"
                       "Decoded value = stored x scale_factor + add_offset, the value shown in the legend.");
  if(grib_info)mask_tip=tr("Mask Range applies to ecCodes decoded values, inclusive; masked values are white. Empty bounds are open. No CF scale/offset is applied to GRIB.");
  if(packing.packed())
    mask_tip+=tr("\nThis variable: decoded = stored x %1 + %2.").arg(packing.scale,0,'g',15).arg(packing.offset,0,'g',15);
  if(packing.has_valid_min && packing.has_valid_max)
    mask_tip+=tr("\n%1 in the file: stored [%2, %3] = decoded [%4, %5].").arg(packing.valid_source)
        .arg(packing.valid_min,0,'g',15).arg(packing.valid_max,0,'g',15)
        .arg(packing.decode(packing.valid_min),0,'g',15).arg(packing.decode(packing.valid_max),0,'g',15);
  // a second row: on the first one the fields would end up behind the toolbar's overflow button
  auto mask_bar=new QToolBar(this);
  mask_bar->setObjectName("maskBar");
  layout->addWidget(mask_bar);
  // Put zoom before the long palette/range controls can push it into overflow.
  auto *zoom_in_action=mask_bar->addAction(zoom_icon(true),tr("Zoom in"));
  zoom_in_action->setObjectName("zoomInAction");
  zoom_in_action->setToolTip(tr("Zoom in 1.25x around the view center; mouse wheel zooms at the pointer"));
  auto *zoom_out_action=mask_bar->addAction(zoom_icon(false),tr("Zoom out"));
  zoom_out_action->setObjectName("zoomOutAction");
  zoom_out_action->setToolTip(tr("Zoom out 0.8x around the view center; mouse wheel zooms at the pointer"));
  auto *fit_action=mask_bar->addAction(tr("Fit"));
  fit_action->setObjectName("fitImageAction");
  fit_action->setToolTip(tr("Fit the complete image in the view, keeping flips"));
  mask_bar->addSeparator();
  // Flip V / Flip H keep their pressed state; Reset always restores the original view.
  flip_v_action = mask_bar->addAction(QIcon(":/images/flip_vertical.png"), tr("Flip V"));
  flip_v_action->setObjectName("flipVerticalAction");
  flip_v_action->setCheckable(true);
  flip_v_action->setToolTip(tr("Flip the image top to bottom"));
  flip_h_action = mask_bar->addAction(QIcon(":/images/flip_horizontal.png"), tr("Flip H"));
  flip_h_action->setObjectName("flipHorizontalAction");
  flip_h_action->setCheckable(true);
  flip_h_action->setToolTip(tr("Flip the image left to right"));
  auto *reset_action = mask_bar->addAction(QIcon(":/images/reset_home.png"), tr("Reset"));
  reset_action->setObjectName("resetViewAction");
  reset_action->setToolTip(tr("Go home and reset: restore original orientation, zoom, and position"));
  mask_bar->addSeparator();
  profile_mode_action = mask_bar->addAction(tr("Profile"));
  profile_mode_action->setObjectName("profileModeAction");
  profile_mode_action->setCheckable(true);
  profile_mode_action->setToolTip(tr("Pick control points on the source grid to draw a curve profile; projected views return to Source grid"));
  show_profile_action = mask_bar->addAction(tr("Show Profile"));
  show_profile_action->setObjectName("showProfileAction");
  show_profile_action->setEnabled(false);
  show_profile_action->setToolTip(tr("Extract and show the curve profile for the picked points"));
  clear_profile_action = mask_bar->addAction(tr("Clear"));
  clear_profile_action->setObjectName("clearProfileAction");
  clear_profile_action->setEnabled(false);
  clear_profile_action->setToolTip(tr("Clear picked profile points"));
  mask_bar->addSeparator();
  mask_bar->addWidget(new QLabel(tr("Mask Range (decoded values): "),mask_bar));
  mask_start=new QLineEdit(mask_bar);
  mask_end=new QLineEdit(mask_bar);
  mask_start->setObjectName("maskStart");
  mask_end->setObjectName("maskEnd");
  mask_start->setPlaceholderText(tr("-inf"));
  mask_end->setPlaceholderText(tr("+inf"));
  mask_start->setAccessibleName(tr("Mask start"));
  mask_end->setAccessibleName(tr("Mask end"));
  for(QLineEdit *edit:{mask_start,mask_end}) {
    edit->setToolTip(mask_tip);
    edit->setClearButtonEnabled(true);
    edit->setMaximumWidth(110);
    connect(edit,&QLineEdit::editingFinished,this,[this]{ refresh(); });
  }
  mask_bar->addWidget(mask_start);
  mask_bar->addWidget(new QLabel(tr(" ~ "),mask_bar));
  mask_bar->addWidget(mask_end);
  QLabel *mask_hint=new QLabel(tr("  inclusive; empty = -inf / +inf; masked values are drawn white"),mask_bar);
  mask_hint->setObjectName("maskHint");
  mask_bar->addWidget(mask_hint);
  if(packing.packed() && packing.has_valid_min && packing.has_valid_max)
    mask_hint->setText(mask_hint->text()+tr("   (%1 decoded [%2, %3], stored [%4, %5])").arg(packing.valid_source)
        .arg(packing.decode(packing.valid_min),0,'g',15).arg(packing.decode(packing.valid_max),0,'g',15)
        .arg(packing.valid_min,0,'g',15).arg(packing.valid_max,0,'g',15));
  std::vector<std::string> dimension_names;
  if(grib_info)for(const auto &axis:grib_info->axes)dimension_names.push_back(axis.name.toStdString());
  else dimension_names=nc_dimension_names(QString::fromLocal8Bit(this->file.c_str()),this->path);
  for(size_t i = 0; i + 2 < dims.size(); ++i) {
    axis_names << (dimension_names.size() == dims.size() ? QString::fromStdString(dimension_names[i])
                                                         : tr("axis %1").arg(i));
    tools->addWidget(new QLabel(tr("Axis %1: ").arg(i), tools));
    auto spin = new QSpinBox(tools);
    spin->setRange(0, dims[i] ? int(std::min<hsize_t>(dims[i]-1, INT_MAX)) : 0);
    spin->setKeyboardTracking(false);
    spin->setToolTip(tr("0-based index of %1").arg(axis_names.last()));
    layers.push_back(spin);
    tools->addWidget(spin);
    connect(spin, &QSpinBox::valueChanged, this, [this] { refresh(); emit layer_changed(); });
  }
  scene = new QGraphicsScene(this);
  image_item=scene->addPixmap(QPixmap());
  auto view = new Canvas(scene, this);
  canvas = view;
  canvas->setAccessibleName(tr("Raster image"));
  canvas->setAcceptDrops(false);
  canvas->viewport()->setAcceptDrops(false);
  canvas->setDragMode(QGraphicsView::ScrollHandDrag);
  canvas->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  canvas->viewport()->setMouseTracking(true);
  canvas->viewport()->installEventFilter(this);
  view->viewport_changed_callback=[this]{notify_viewport_changed();};
  connect(canvas->horizontalScrollBar(),&QScrollBar::valueChanged,this,[this]{notify_viewport_changed();});
  connect(canvas->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{notify_viewport_changed();});
  canvas->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(canvas,&QWidget::customContextMenuRequested,this,[this](const QPoint &position){
    if(profile_mode_action->isChecked()) return;
    QMenu menu(this);
    menu.addAction(tr("CMaps…"),this,&RasterView::choose_color_mapping);
    menu.addAction(tr("Import RGB color map…"),this,&RasterView::choose_rgb_file);
    menu.exec(canvas->mapToGlobal(position));
  });
  // Compact button beside CMaps; detailed options are kept out of the toolbar row.
  auto *options=new QToolButton(tools);options->setText(tr("…"));options->setObjectName("colorMapOptions");
  options->setAccessibleName(tr("Color map options"));options->setToolTip(tr("Color cards, mapping and RGB import"));
  auto *options_menu=new QMenu(options);
  options_menu->addAction(tr("Color cards and mapping…"),this,&RasterView::choose_color_mapping);
  options_menu->addAction(tr("Import RGB…"),this,&RasterView::choose_rgb_file);
  options->setMenu(options_menu);options->setPopupMode(QToolButton::InstantPopup);
  auto actions=tools->actions();
  QAction *reverse_action=nullptr;
  for(auto *action:actions) if(auto *widget_action=qobject_cast<QWidgetAction*>(action))
    if(widget_action->defaultWidget()==reverse_palette) reverse_action=action;
  tools->insertWidget(reverse_action,options);
  canvas_stack=new QStackedWidget(this);canvas_stack->addWidget(canvas);
  map_display=new MapDisplay(this);map_display->setProperty("externalStatus",true);canvas_stack->addWidget(map_display);layout->addWidget(canvas_stack,1);
  connect(map_display,&MapDisplay::mode_changed,this,[this](bool active){
    canvas_stack->setCurrentWidget(active?static_cast<QWidget*>(map_display):static_cast<QWidget*>(canvas));
    flip_h_action->setEnabled(!active);flip_v_action->setEnabled(!active);
    // Profile owns source-index points; keep its entry available from every projection.
    if(active)profile_mode_action->setChecked(false);
    show_profile_action->setEnabled(!active&&display_valid&&profile_points.size()>=2);
    clear_profile_action->setEnabled(!profile_points.empty());
  });
  connect(map_display,&MapDisplay::mode_changed,this,[this]{QSignalBlocker block(projection_selector);projection_selector->setCurrentIndex(int(map_display->mode()));});
  connect(map_display,&MapDisplay::status_changed,projection_status,&QLabel::setText);
  connect(map_display,&MapDisplay::status_changed,projection_status,&QWidget::setToolTip);
  auto *preference_error=new ProjectionStatusLabel(map_display->property("settingsError").toString(),projection_bar);preference_error->setObjectName("projectionPreferenceError");preference_error->setToolTip(preference_error->text());preference_error->setVisible(!preference_error->text().isEmpty());projection_row->addWidget(preference_error,1);projection_status->setVisible(preference_error->text().isEmpty());
  projection_bar->setToolTip(tr("Preferences: %1").arg(map_display->property("settingsFile").toString()));
  connect(map_display,&MapDisplay::preferences_error,this,[this,preference_error](const QString &message){preference_error->setText(message);preference_error->setToolTip(message);preference_error->setVisible(!message.isEmpty());projection_status->setVisible(message.isEmpty());});
  connect(projection_selector,&QComboBox::currentIndexChanged,this,[this]{
    restore_projection_pending=false;
    const auto mode=MapProjection::Mode(projection_selector->currentData().toInt());
    if(mode==MapProjection::Mode::Source){pending_projection.reset();projection_dialog_pending=false;map_display->select_projection(mode);return;}
    pending_projection=mode;prepare_map_coordinates();
  });
  auto apply_flip = [this] {
    QTransform matrix = canvas->transform();
    const double sx = std::abs(matrix.m11()) * (flip_h_action->isChecked() ? -1.0 : 1.0);
    const double sy = std::abs(matrix.m22()) * (flip_v_action->isChecked() ? -1.0 : 1.0);
    matrix.setMatrix(sx, matrix.m12(), matrix.m13(),
                     matrix.m21(), sy, matrix.m23(),
                     matrix.dx(), matrix.dy(), matrix.m33());
    canvas->setTransform(matrix);
    notify_viewport_changed();
  };
  connect(flip_v_action, &QAction::toggled, this, [apply_flip](bool) { apply_flip(); });
  connect(flip_h_action, &QAction::toggled, this, [apply_flip](bool) { apply_flip(); });
  connect(reset_action, &QAction::triggered, this, [this] {
    if(map_display->active()){map_display->fit_view();return;}
    QSignalBlocker block_v(flip_v_action), block_h(flip_h_action);
    flip_v_action->setChecked(false);
    flip_h_action->setChecked(false);
    canvas->resetTransform();
    canvas->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
  });
  connect(profile_mode_action, &QAction::toggled, this, [this](bool enabled) {
    if(enabled){
      // A late coordinate result must not restore a projection after selecting Profile.
      restore_projection_pending=false;pending_projection.reset();projection_dialog_pending=false;
      if(map_display->active()||projection_selector->currentData().toInt()!=int(MapProjection::Mode::Source))
        map_display->select_projection(MapProjection::Mode::Source);
    }
    canvas->setDragMode(enabled ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
    canvas->viewport()->setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    dragging_point_index = -1;
    // Toggling Profile off hides the overlay while keeping points; toggling it back on restores them.
    redraw_profile_overlay();
  });
  connect(show_profile_action, &QAction::triggered, this, [this] { show_profile_window(); });
  connect(clear_profile_action, &QAction::triggered, this, [this] {
    profile_points.clear();
    show_profile_action->setEnabled(false);
    clear_profile_action->setEnabled(false);
    redraw_profile_overlay();
    if(profile_window) if(auto *view=qobject_cast<ProfileView*>(profile_window.data()))
      view->update_data(last_values, last_rows, last_cols, {}, CurvePathMode::SmoothSpline, raster_reservation);
  });
  connect(fit_action,&QAction::triggered,this,[this,apply_flip] {
    if(map_display->active()){map_display->fit_view();return;}
    canvas->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
    notify_viewport_changed();
    apply_flip();
  });
  connect(zoom_in_action,&QAction::triggered,this,[this,view]{if(map_display->active())map_display->zoom(1.25);else view->zoom(1.25);});
  connect(zoom_out_action,&QAction::triggered,this,[this,view]{if(map_display->active())map_display->zoom(.8);else view->zoom(.8);});
  legend = new QLabel(this);
  legend->setObjectName("rasterLegend");
  legend->setWordWrap(true);
  color_bar=new ColorBar(this);
  color_bar->set_compact(true);
  layout->addWidget(color_bar,0,Qt::AlignHCenter);
  layout->addWidget(legend);
  entropy_summary=new QLabel(this);
  entropy_summary->setObjectName("rasterEntropySummary");
  entropy_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(entropy_summary);
  connect(map_display,&MapDisplay::frame_ready,this,[this]{if(pending_map_legend){auto publish=std::move(pending_map_legend);pending_map_legend={};publish();}});
  connect(map_display,&MapDisplay::mode_changed,this,[this](bool active){if(!active&&pending_map_legend){auto publish=std::move(pending_map_legend);pending_map_legend={};publish();}});
  if(grib_info) {
    grib_reader=new DataReadController;
    connect(grib_reader,&DataReadController::completed,this,[this](auto data,auto lease){
      grib_read_pending=false;
      if(sync_out_of_range)return;
      const auto current=GribData::describe(grib_file,QString::fromUtf8(path.c_str()));
      if(!current.error.isEmpty()||current.object.source_session_id!=grib_info->object.source_session_id){clear_plane(tr("SourceChanged: close and reopen source"));return;}
      const auto rows=dims[dims.size()-2],cols=dims.back();
      if(data->rows!=rows||data->cols!=cols||data->values.size()!=size_t(rows*cols)){clear_plane(tr("Incomplete GRIB plane."));return;}
      grib_plane=data;raster_reservation=lease;
      decoded_values=std::shared_ptr<const std::vector<double>>(data,&data->values);
      if(coordinates_requested){auto grid=std::make_shared<GeoGrid::Grid>(GeoGrid::from_grib(*data,data->geographic_keys,grib_info->grid_kind));accept_geographic_grid(grid,lease);}
      cached_indices=grib_requested_indices;last_rows=data->rows;last_cols=data->cols;
      cached_units=data->units;declared_categories.clear();cached_meanings.clear();reading_valid=true;mask_valid=false;
      setProperty("reader",data->reader);setProperty("decodeProfile",data->decode_profile);
      setProperty("planeReadCount",property("planeReadCount").toULongLong()+1);
      refresh();
    });
    connect(grib_reader,&DataReadController::failed,this,[this](const QString &error){grib_read_pending=false;clear_plane(error);});
  }
  original_source_stamp=GeoGrid::stamp(grib_info?grib_file:QString::fromLocal8Bit(file.c_str()));
  QTimer::singleShot(0, this, [this]{
    refresh();
    if(map_display->has_saved_preferences()&&map_display->saved_mode()!=MapProjection::Mode::Source){
      restore_projection_pending=true;{QSignalBlocker block(projection_selector);projection_selector->setCurrentIndex(int(map_display->saved_mode()));}
      prepare_map_coordinates();
    }
  });
}

bool RasterView::eventFilter(QObject *watched,QEvent *event) {
  if(watched==canvas->viewport() && event->type()==QEvent::Resize)
    QTimer::singleShot(0,this,[this]{notify_viewport_changed();});
  if(watched==canvas->viewport() && display_valid && profile_mode_action && profile_mode_action->isChecked()) {
    auto nearest_point_index = [this](const QPoint &view_pos) -> int {
      int best = -1;
      double best_dist = 12.0;   // 12 px hit radius in viewport coordinates
      for(size_t i = 0; i < profile_points.size(); ++i) {
        const QPoint p = canvas->mapFromScene(profile_points[i]);
        const double d = std::hypot(double(p.x() - view_pos.x()), double(p.y() - view_pos.y()));
        if(d <= best_dist) {
          best_dist = d;
          best = int(i);
        }
      }
      return best;
    };
    if(event->type()==QEvent::MouseButtonPress) {
      auto *mouse=static_cast<QMouseEvent*>(event);
      if(mouse->button()==Qt::LeftButton) {
        const QPointF pt=canvas->mapToScene(mouse->pos());
        profile_points.push_back(pt);
        show_profile_action->setEnabled(profile_points.size()>=2);
        clear_profile_action->setEnabled(!profile_points.empty());
        redraw_profile_overlay();
        return true;
      }
      if(mouse->button()==Qt::RightButton) {
        const int hit = nearest_point_index(mouse->pos());
        if(hit >= 0) {
          dragging_point_index = hit;
          canvas->viewport()->setCursor(Qt::ClosedHandCursor);
          return true;
        }
        if(profile_points.size()>=2) {
          show_profile_window();
          return true;
        }
      }
    } else if(event->type()==QEvent::MouseMove && dragging_point_index >= 0) {
      auto *mouse=static_cast<QMouseEvent*>(event);
      if(size_t(dragging_point_index) < profile_points.size()) {
        profile_points[size_t(dragging_point_index)] = canvas->mapToScene(mouse->pos());
        redraw_profile_overlay();
        if(profile_window && profile_window->isVisible()) show_profile_window();
      }
      return true;
    } else if(event->type()==QEvent::MouseButtonRelease) {
      auto *mouse=static_cast<QMouseEvent*>(event);
      if(mouse->button()==Qt::RightButton && dragging_point_index >= 0) {
        dragging_point_index = -1;
        canvas->viewport()->setCursor(Qt::CrossCursor);
        return true;
      }
    } else if(event->type()==QEvent::MouseButtonDblClick) {
      auto *mouse=static_cast<QMouseEvent*>(event);
      const int hit = nearest_point_index(mouse->pos());
      if(hit >= 0) {
        profile_points.erase(profile_points.begin() + hit);
        show_profile_action->setEnabled(profile_points.size()>=2);
        clear_profile_action->setEnabled(!profile_points.empty());
        redraw_profile_overlay();
        if(profile_window && profile_window->isVisible() && profile_points.size()>=2) show_profile_window();
        return true;
      }
      if(profile_points.size()>=2) {
        show_profile_window();
        return true;
      }
    }
  }
  if(watched==canvas->viewport() && event->type()==QEvent::MouseMove && display_valid&&last_values&&decoded_values) {
    auto *mouse=static_cast<QMouseEvent*>(event);
    QPointF point=canvas->mapToScene(mouse->pos());
    QString text=tr("Pointer is outside the image.");
    if(point.x()>=0&&point.y()>=0&&point.x()<last_cols&&point.y()<last_rows) {
      const size_t row=size_t(point.y()),column=size_t(point.x()),index=row*last_cols+column;
      const double original=(*decoded_values)[index];
      QString state;
      if(!std::isfinite(original)) state=tr("Missing in source");
      else if(!std::isfinite((*last_values)[index])) state=tr("Mask; original decoded value %1").arg(original,0,'g',15);
      else state=tr("Decoded value %1 %2").arg(original,0,'g',15).arg(cached_units);
      text=tr("Index [%1, %2]; %3").arg(row).arg(column).arg(state);
      if(geographic&&geo_grid.valid) {
        const auto coordinate=geographic_coordinate(geo_grid,row,column);
        if(coordinate.valid) text+=tr("; latitude %1°, longitude %2°").arg(coordinate.latitude,0,'g',10).arg(coordinate.longitude,0,'g',10);
      }
    }
    canvas->viewport()->setToolTip(text);legend->setToolTip(text);
  }
  return QWidget::eventFilter(watched,event);
}

void RasterView::refresh() {
  if(!map_source_stamp.isEmpty()&&map_source_stamp!=GeoGrid::stamp(QString::fromLocal8Bit(file.c_str()))){if(map_display)map_display->invalidate("SourceChanged: reopen source");clear_plane("SourceChanged: close and reopen source");return;}
  if(sync_out_of_range) { clear_plane(sync_range_message);return; }
  canvas->viewport()->setToolTip({});legend->setToolTip({});
  const quint64 request=++render_revision;
  render_generation->store(request);
  if(dims.size()<2 || std::find(dims.begin(),dims.end(),0)!=dims.end()) {
    clear_plane(tr("Invalid plane dimensions."));return;
  }
  const hsize_t rows = dims[dims.size()-2], cols = dims.back();
  if(rows>INT_MAX||cols>INT_MAX||rows>16*1024*1024/cols) {
    clear_plane(tr("Plane exceeds the 16 megapixel preview limit."));return;
  }
  const QString filename=QString::fromLocal8Bit(file.c_str());
  std::vector<size_t> indices;for(auto *spin:layers) indices.push_back(size_t(spin->value()));
  if(grib_info&&(!reading_valid||indices!=cached_indices)) {
    if(grib_read_pending&&indices==grib_requested_indices&&grib_requested_coordinates==coordinates_requested)return;
    hold_presented_frame(tr("Reading GRIB plane... (previous image; values unavailable)"));reading_valid=false;mask_valid=false;
    decoded_values.reset();last_values.reset();grib_plane.reset();raster_reservation.reset();
    grib_requested_indices=indices;grib_requested_coordinates=coordinates_requested;grib_read_pending=true;
    if(!grib_info->can_image){grib_read_pending=false;clear_plane(grib_info->disabled_reasons.value("image",tr("Unsupported GRIB image grid")));return;}
    grib_reader->read(grib_file,QString::fromUtf8(path.c_str()),indices,coordinates_requested,false);return;
  }
  if(!grib_info&&!raster_reservation) {
    raster_reservation=reserve_raster(quint64(rows)*cols);
    if(!raster_reservation) {clear_plane(tr("Raster memory budget reached. Close another image or select a smaller plane."));return;}
  }
  if(!grib_info&&(!reading_valid||indices!=cached_indices)) {
    reading_valid=false;mask_valid=false;decoded_values.reset();last_values.reset();
    auto read=nc_read_plane_decoded(filename,path,dims,indices);
    setProperty("planeReadCount",property("planeReadCount").toULongLong()+1);
    if(!read.error.isEmpty()){clear_plane(read.error);return;}
    decoded_values=std::make_shared<const std::vector<double>>(std::move(read.plane.values));
    if(decoded_values->size()!=size_t(rows*cols)){clear_plane(tr("Incomplete plane."));return;}
    setProperty("reader",read.reader);cached_indices=indices;last_rows=size_t(rows);last_cols=size_t(cols);
    // Metadata follows the same cache lifetime; palette changes never reopen data files.
    if(classic) {
      declared_categories=nc_classic_numeric_attribute(filename,path,"flag_values");
      cached_meanings=nc_classic_text_attribute(filename,path,"flag_meanings");
      cached_units=nc_classic_text_attribute(filename,path,"units");
    } else {
      Handle fid{H5Fopen(file.c_str(),H5F_ACC_RDONLY,H5P_DEFAULT),H5Fclose};
      Handle dataset{fid.id>=0?H5Dopen2(fid.id,path.c_str(),H5P_DEFAULT):-1,H5Dclose};
      declared_categories.clear();cached_meanings.clear();cached_units.clear();
      if(dataset.id>=0) {
        declared_categories=numeric_array_attr(dataset.id,"flag_values");
        cached_meanings=text_array_attr(dataset.id,"flag_meanings");cached_units=text_array_attr(dataset.id,"units");
      }
    }
    reading_valid=true;
  }
  double mask_from=-INFINITY, mask_to=INFINITY;
  const bool masking=!mask_start->text().trimmed().isEmpty() || !mask_end->text().trimmed().isEmpty();
  if(!nc_mask_bound(mask_start->text(),-INFINITY,mask_from) ||
     !nc_mask_bound(mask_end->text(),INFINITY,mask_to)) {
    clear_plane(tr("Mask Range: start and end must be numbers (empty = -inf / +inf)."));return;
  }
  if(masking && mask_from>mask_to) {
    clear_plane(tr("Mask Range: start must not be greater than end."));return;
  }
  bool values_changed=false;
  if(!mask_valid||cached_mask_start!=mask_start->text()||cached_mask_end!=mask_end->text()) {
    last_values=decoded_values;masked_count=0;
    if(masking) {
      auto masked_values=std::make_shared<std::vector<double>>(*decoded_values);
      masked_count=nc_mask_range(*masked_values,mask_from,mask_to);last_values=masked_values;
    }
    cached_mask_start=mask_start->text();cached_mask_end=mask_end->text();mask_valid=true;values_changed=true;
    data_low=INFINITY;data_high=-INFINITY;
    for(double value:*last_values) if(std::isfinite(value)){data_low=std::min(data_low,value);data_high=std::max(data_high,value);}
  }
  const bool publish_snapshot=values_changed || !snapshot_available;
  QString entropy_text=entropy_summary->text();
  QVariant entropy_bits=property("entropyBits");
  if(publish_snapshot) {
    const auto entropy=nc_gray_entropy(*last_values);
    entropy_text=entropy.valid
      ? tr("Whole-plane histogram entropy (256 bins): %1 bits   Normalized: %2").arg(entropy.entropy_bits,0,'f',4).arg(entropy.normalized_entropy,0,'g',9)
      : tr("Whole-plane histogram entropy (256 bins): N/A");
    entropy_bits=entropy.valid ? QVariant(entropy.entropy_bits) : QVariant();
  }
  setProperty("masked",qulonglong(masked_count));
  double low=data_low,high=data_high;
  category_values.clear();
  if(discrete_mode) {
    if(!declared_categories.empty()) category_values=declared_categories;
    else for(double value:*last_values) {
      if(std::isfinite(value)&&std::find(category_values.begin(),category_values.end(),value)==category_values.end()) {
        category_values.push_back(value);if(category_values.size()>12) break;
      }
    }
  }
  // flag_meanings follows the file's flag_values order, including non-sorted values.
  if(declared_categories.empty()) std::sort(category_values.begin(),category_values.end());
  auto meanings=cached_meanings.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
  category_labels=RasterColorMap::categoryLabels(category_values,meanings);
  bool automatic=range_mode->currentData().toBool();
  const bool has_valid_values=std::isfinite(low);
  auto range=RasterColorMap::resolveRange(automatic,low,high,
      fixed_minimum->value(),fixed_maximum->value(),std::isfinite(low));
  if(!automatic && synced_color_range) range={synced_color_range->first,synced_color_range->second,true};
  if(discrete_mode) range={low,high,std::isfinite(low)};
  // With no finite data, keep the missing/Mask image visible without inventing a range.
  const bool no_auto_range=!has_valid_values&&(automatic||discrete_mode);
  if(no_auto_range) range={0,1,true};
  if(discrete_mode && category_values.size()>12) {
    clear_plane(tr("Discrete preview supports at most 12 categories; found more."));return;
  }
  if(!range.valid) {
    clear_plane(automatic?tr("No valid values"):tr("Invalid fixed range: minimum must be less than maximum."));return;
  }
  low=range.minimum;
  high=range.maximum;
  color_mapping.minimum=low;color_mapping.maximum=high;color_mapping.reversed=reverse_palette->isChecked();
  if(!discrete_mode&&!color_mapping.boundaries.empty()) {
    color_mapping.minimum=low=color_mapping.boundaries.front();color_mapping.maximum=high=color_mapping.boundaries.back();
  }
  const auto selected=color_maps[size_t(palette_selector->currentIndex())];
  auto settings=color_mapping;
  if(discrete_mode) {settings.normalization=HdfColorMap::Normalization::Linear;settings.boundaries.clear();}
  if(no_auto_range) {settings.normalization=HdfColorMap::Normalization::Linear;settings.boundaries.clear();}
  const auto error=HdfColorMap::validate(selected,settings);
  if(!error.isEmpty()){clear_plane(error);return;}
  setProperty("paletteId",selected.id);
  auto values=last_values;auto original=decoded_values;const auto categories=category_values;
  const bool categorical=discrete_mode;
  auto generation=render_generation;
  const auto reservation=raster_reservation;
  auto render=[values,original,selected,settings,categories,categorical,rows,cols,generation,request,reservation] {
    QImage image(int(cols),int(rows),QImage::Format_RGB32);
    if(image.isNull()) return image;
    for(int row=0;row<image.height();++row) {
      if(generation->load()!=request) return QImage();
      auto line=reinterpret_cast<QRgb*>(image.scanLine(row));
      for(int column=0;column<image.width();++column) {
        const size_t index=size_t(row)*cols+column;const double value=(*values)[index];
        const bool masked=!std::isfinite(value)&&std::isfinite((*original)[index]);
        if(categorical&&std::isfinite(value)) {
          auto found=std::find(categories.begin(),categories.end(),value);
          line[column]=found==categories.end()?qRgb(255,0,255):HdfColorMap::sample(selected,(double(found-categories.begin())+.5)/categories.size(),settings.reversed);
        } else line[column]=HdfColorMap::map_value(selected,settings,value,masked);
      }
    }
    return image;
  };
  const QStringList bar_labels=discrete_mode?category_labels:QStringList();
  const QString unit_text=cached_units;
  QString frame_legend=discrete_mode
    ? tr("%1 x %2   Categories: %3   Unknown: magenta   Missing: grey").arg(cols).arg(rows).arg(category_labels.join("; "))
    : tr("%1 x %2   Min %3   Max %4   Missing: grey%5")
      .arg(cols).arg(rows).arg(low,0,'g',9).arg(high,0,'g',9)
      .arg(!color_mapping.boundaries.empty()?tr(" (custom boundaries)"):
        automatic ? tr(" (automatic)") : tr(" (fixed)"));
  if(no_auto_range) frame_legend=tr("%1 x %2   No valid values; missing: grey; Mask: white").arg(cols).arg(rows);
  if(masking) {
    auto bound=[](double value) { return std::isinf(value) ? (value<0 ? QString("-inf") : QString("+inf")) : QString::number(value,'g',9); };
    frame_legend+=tr("   Masked [%1, %2] (decoded): %3, white")
      .arg(bound(mask_from),bound(mask_to)).arg(masked_count);
  }
  if(geographic && geo_grid.valid)
    frame_legend+=tr("   Geographic lookup: %1 / %2").arg(QString::fromUtf8(geo_grid.latitudePath.c_str()),QString::fromUtf8(geo_grid.longitudePath.c_str()));
  const bool asynchronous=rows*cols>65536&&!qApp->property("synchronousRasterPreview").toBool();
  if(asynchronous) hold_presented_frame(tr("Rendering requested layer... (previous image; values unavailable)"));
  auto apply_image=[this,request,selected,settings,unit_text,bar_labels,no_auto_range,reservation,
                    frame_legend,entropy_text,entropy_bits,publish_snapshot,low,high,cols,rows](const QImage &image) {
    if(request!=render_revision) return;
    if(image.isNull()){clear_plane(tr("Cannot allocate image."));return;}
    image_item->setPixmap(QPixmap::fromImage(image));setProperty("renderedRevision",request);
    map_display->set_source(image,last_values,decoded_values,reservation);
    presented_reservation=reservation;
    // The colour scale belongs to the visible bitmap, not the next source frame still projecting.
    auto publish_legend=[this,selected,settings,unit_text,bar_labels,no_auto_range,frame_legend]{color_bar->set_mapping(selected,settings,unit_text,bar_labels,!no_auto_range);legend->setText(frame_legend);};
    if(map_display->active()&&!map_display->result())pending_map_legend=std::move(publish_legend);
    else {pending_map_legend={};publish_legend();}
    entropy_summary->setText(entropy_text);setProperty("entropyBits",entropy_bits);
    display_valid=true;
    show_profile_action->setEnabled(!map_display->active()&&profile_points.size()>=2);
    setProperty("minimum",no_auto_range?QVariant():QVariant(low));
    setProperty("maximum",no_auto_range?QVariant():QVariant(high));
    scene->setSceneRect(0,0,double(cols),double(rows));
    if(first_frame_fit) {
      first_frame_fit=false;
      canvas->fitInView(scene->sceneRect(),Qt::KeepAspectRatio);
      notify_viewport_changed();
    }
    if(profile_window) if(auto *view=qobject_cast<ProfileView*>(profile_window.data()))
      view->update_data(last_values,last_rows,last_cols,profile_points,CurvePathMode::SmoothSpline,raster_reservation);
    if(publish_snapshot || !snapshot_available) {
      snapshot_available=true;
      emit plane_snapshot_changed(++snapshot_revision);
    }
    emit color_mapping_changed();
  };
  // Small deterministic fixtures stay synchronous; larger recoloring runs off the GUI thread.
  pending_render={};pending_apply={};
  if(!asynchronous) {
    const QImage image=render();
    if(image.isNull()) {clear_plane(tr("Cannot allocate image."));return;}
    apply_image(image);
  }
  else {
    pending_render=render;pending_apply=apply_image;pending_revision=request;start_pending_render();
  }
}

void RasterView::hold_presented_frame(const QString &message) {
  pending_map_legend={};
  if(map_display&&map_display->active())map_display->hold_frame(message);
  // 请求与呈现分离：仅保留上一张位图/色条；旧数值不参与新层分析或探针。
  display_valid=false;show_profile_action->setEnabled(false);
  canvas->viewport()->setToolTip({});legend->setToolTip({});color_bar->setToolTip({});
  legend->setText(message);
  entropy_summary->setText(tr("Whole-plane histogram entropy (256 bins): N/A"));setProperty("entropyBits",QVariant());
  setProperty("minimum",QVariant());setProperty("maximum",QVariant());
  if(profile_window) if(auto *view=qobject_cast<ProfileView*>(profile_window.data()))
    view->update_data({},0,0,{},CurvePathMode::SmoothSpline);
  if(snapshot_available) {
    snapshot_available=false;
    emit plane_snapshot_changed(++snapshot_revision);
  }
}

void RasterView::clear_plane(const QString &error) {
  pending_map_legend={};
  if(map_display)map_display->invalidate(error);
  ++render_revision;render_generation->store(render_revision);pending_render={};pending_apply={};
  display_valid=false;mask_valid=false;show_profile_action->setEnabled(false);
  image_item->setPixmap(QPixmap());color_bar->clear();legend->setText(error);
  presented_reservation.reset();
  canvas->viewport()->setToolTip({});legend->setToolTip({});
  snapshot_available=false;
  entropy_summary->setText(tr("Whole-plane histogram entropy (256 bins): N/A"));setProperty("entropyBits",QVariant());
  emit plane_snapshot_changed(++snapshot_revision);
  setProperty("minimum",QVariant());setProperty("maximum",QVariant());
  if(profile_window) if(auto *view=qobject_cast<ProfileView*>(profile_window.data()))
    view->update_data({},0,0,{},CurvePathMode::SmoothSpline);
}
RasterView::~RasterView() {
  // QWidget析构会调整子画布尺寸；停止回调，避免通知已析构的派生成员。
  static_cast<Canvas*>(canvas)->viewport_changed_callback={};
  canvas->horizontalScrollBar()->disconnect(this);canvas->verticalScrollBar()->disconnect(this);
  color_bar=nullptr;
  if(grib_reader){grib_reader->disconnect(this);grib_reader->dispose();}
  if(geo_reader){geo_reader->disconnect(this);geo_reader->dispose();}
  if(map_display){map_display->disconnect(this);delete map_display;map_display=nullptr;}
  render_generation->fetch_add(1);pending_render={};
}
bool RasterView::render_pending() const {return grib_read_pending||render_running || bool(pending_render)||(map_display&&map_display->active()&&map_display->busy());}
PlaneSnapshot RasterView::plane_snapshot() const {
  PlaneSnapshot snapshot;
  snapshot.file_path=grib_info?grib_file:QString::fromLocal8Bit(file.c_str());
  snapshot.variable_path=QString::fromUtf8(path.c_str());
  for(auto *spin:layers) snapshot.leading_indices.push_back(size_t(spin->value()));
  snapshot.rows=dims.size()<2 ? 0 : size_t(dims[dims.size()-2]);
  snapshot.cols=dims.empty() ? 0 : size_t(dims.back());
  snapshot.units=cached_units.isEmpty() ? (grib_info?QString("unknown"):QString("unspecified")) : cached_units;
  snapshot.revision=snapshot_revision;
  snapshot.out_of_range=sync_out_of_range;
  if(grib_plane && snapshot_available) {
    snapshot.grid_identity=grib_plane->grid_identity;
    snapshot.time_identity=grib_plane->time_identity;
  }
  if(snapshot_available) {snapshot.values=last_values;snapshot.storage_lease=raster_reservation;}
  return snapshot;
}

void RasterView::notify_viewport_changed() {
  if(color_bar){
    // 色标辅助读图：跟随可见图像宽度，避免铺满窗口挤占主体。
    const int available=std::max(100,canvas->viewport()->width()-16);
    const int image_width=image_item->pixmap().isNull()?available:std::min(available,canvas->mapFromScene(image_item->boundingRect()).boundingRect().width());
    const int target=std::min(available,std::clamp(qRound(.75*image_width),240,640));
    if(color_bar->width()!=target)color_bar->setFixedWidth(target);
  }
  if(!applying_viewport&&(!map_display||!map_display->active())) emit viewport_changed();
}

ViewportState RasterView::viewport_state() const {
  ViewportState state;
  state.transform=canvas->transform();
  auto ratio=[](QScrollBar *bar) {
    return bar->maximum()==bar->minimum() ? .5 : double(bar->value()-bar->minimum())/double(bar->maximum()-bar->minimum());
  };
  state.h_ratio=ratio(canvas->horizontalScrollBar());state.v_ratio=ratio(canvas->verticalScrollBar());
  state.flip_h=flip_h_action->isChecked();state.flip_v=flip_v_action->isChecked();
  return state;
}

void RasterView::apply_viewport_state(const ViewportState &state) {
  if(map_display&&map_display->active())return;
  if(!state.transform.isInvertible() || !std::isfinite(state.h_ratio) || !std::isfinite(state.v_ratio)) return;
  const auto previous=viewport_state();
  if(previous.transform==state.transform && previous.flip_h==state.flip_h && previous.flip_v==state.flip_v &&
     std::abs(previous.h_ratio-state.h_ratio)<1e-12 && std::abs(previous.v_ratio-state.v_ratio)<1e-12) return;
  applying_viewport=true;
  {
    QSignalBlocker horizontal(flip_h_action),vertical(flip_v_action);
    flip_h_action->setChecked(state.flip_h);flip_v_action->setChecked(state.flip_v);
    canvas->setTransform(state.transform);
    auto set_ratio=[](QScrollBar *bar,double ratio) {
      bar->setValue(bar->minimum()+qRound(std::clamp(ratio,0.0,1.0)*double(bar->maximum()-bar->minimum())));
    };
    set_ratio(canvas->horizontalScrollBar(),state.h_ratio);set_ratio(canvas->verticalScrollBar(),state.v_ratio);
  }
  applying_viewport=false;
  notify_viewport_changed();
}

RasterColorState RasterView::color_state() const {
  return {color_maps[size_t(palette_selector->currentIndex())],color_mapping,discrete_mode};
}

void RasterView::apply_color_state(const RasterColorState &state) {
  if(!HdfColorMap::validate(state.palette,state.mapping).isEmpty()) return;
  int selected=-1;
  for(size_t i=0;i<color_maps.size();++i) if(color_maps[i].id==state.palette.id && color_maps[i].source_hash==state.palette.source_hash) selected=int(i);
  if(selected<0) {color_maps.push_back(state.palette);fill_palette_selector();selected=int(color_maps.size()-1);}
  auto *discrete_toggle=findChild<QCheckBox*>("discreteColors");
  QSignalBlocker palette(palette_selector),reverse(reverse_palette),mode(range_mode),low(fixed_minimum),high(fixed_maximum),discrete(discrete_toggle);
  palette_selector->setCurrentIndex(selected);reverse_palette->setChecked(state.mapping.reversed);
  color_mapping=state.mapping;discrete_mode=state.discrete;discrete_toggle->setChecked(state.discrete);
  // Optional color sync deliberately shares the anchor's resolved decoded range.
  range_mode->setCurrentIndex(1);fixed_minimum->setEnabled(true);fixed_maximum->setEnabled(true);
  fixed_minimum->setValue(state.mapping.minimum);fixed_maximum->setValue(state.mapping.maximum);
  synced_color_range=std::make_pair(state.mapping.minimum,state.mapping.maximum);
  refresh();
}

void RasterView::set_sync_out_of_range(bool out_of_range,const QString &message) {
  if(sync_out_of_range==out_of_range && (!out_of_range || sync_range_message==message)) return;
  sync_out_of_range=out_of_range;sync_range_message=message;
  setProperty("syncOutOfRange",out_of_range);
  if(out_of_range&&grib_reader){grib_reader->cancel();grib_read_pending=false;}
  if(out_of_range) clear_plane(tr("Out of range: %1").arg(message));
  else refresh();
}

bool RasterView::apply_layer_indices(const std::vector<size_t> &indices) {
  if(indices.size()!=layers.size()) return false;
  bool changed=false;
  for(size_t i=0;i<indices.size();++i) {
    if(indices[i]>=size_t(layer_count(int(i)))) return false;
    changed=changed || indices[i]!=size_t(layers[i]->value());
  }
  if(!changed) return true;
  for(size_t i=0;i<indices.size();++i) {QSignalBlocker blocker(layers[i]);layers[i]->setValue(int(indices[i]));}
  if(!sync_out_of_range) refresh();
  emit layer_changed();
  return true;
}
QImage RasterView::image_for_export() {
  // A foreground export must not save an older asynchronous revision. A synchronous refresh uses
  // the same immutable plane/cache and mapping, and invalidates queued older workers.
  const QVariant previous=qApp->property("synchronousRasterPreview");
  qApp->setProperty("synchronousRasterPreview",true);
  refresh();
  if(grib_read_pending) {
    QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
    connect(grib_reader,&DataReadController::drained,&loop,&QEventLoop::quit);
    connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
    timeout.start(30000);loop.exec(QEventLoop::ExcludeUserInputEvents);
  }
  if(grib_info) {
    const auto current=GribData::describe(grib_file,QString::fromUtf8(path.c_str()));
    if(!current.error.isEmpty()||current.object.source_session_id!=grib_info->object.source_session_id)clear_plane(tr("SourceChanged: close and reopen source"));
  }
  qApp->setProperty("synchronousRasterPreview",previous);
  if(grib_read_pending)return {};
  if(!display_valid || image_item->pixmap().isNull()) return {};
  return image_item->pixmap().toImage().mirrored(flip_h_action->isChecked(),flip_v_action->isChecked());
}
void RasterView::start_pending_render() {
  if(render_running||!pending_render) return;
  static QThreadPool render_pool;
  render_pool.setMaxThreadCount(2);
  auto render=std::move(pending_render);pending_render={};const auto request=pending_revision;
  auto apply=std::move(pending_apply);pending_apply={};
  render_running=true;const QPointer<RasterView> guard(this);
  render_pool.start([guard,request,render,apply] {
    const QImage image=render();
    QMetaObject::invokeMethod(QCoreApplication::instance(),[guard,request,image,apply] {
      if(!guard) return;
      guard->render_running=false;
      if(guard->render_revision==request) {
        apply(image);
      }
      guard->start_pending_render();
    },Qt::QueuedConnection);
  });
}
void RasterView::prepare_map_coordinates(){
  if(coordinates_ready){apply_pending_projection();return;}
  projection_status->setText(tr("Checking coordinates… projection will redraw automatically"));projection_status->setToolTip(projection_status->text());
  if(coordinates_requested)return;
  coordinates_requested=true;
  map_source_stamp=GeoGrid::stamp(QString::fromLocal8Bit(file.c_str()));
  if(grib_info){if(grib_plane&&grib_plane->coordinates.latitudes.size()==grib_plane->values.size()){auto grid=std::make_shared<GeoGrid::Grid>(GeoGrid::from_grib(*grib_plane,grib_plane->geographic_keys,grib_info->grid_kind));accept_geographic_grid(grid,raster_reservation);}
    else {reading_valid=false;refresh();}return;}
  if(!geo_reader){geo_reader=new GeoGridController;connect(geo_reader,&GeoGridController::completed,this,[this](auto grid,auto lease){accept_geographic_grid(grid,lease);});connect(geo_reader,&GeoGridController::failed,this,[this](const QString &error){auto grid=std::make_shared<GeoGrid::Grid>();grid->error=error;accept_geographic_grid(grid,{});});}
  if(dims.size()<2)return;geo_reader->read(QString::fromLocal8Bit(file.c_str()),QString::fromUtf8(path.c_str()),int(dims[dims.size()-2]),int(dims.back()));
}
void RasterView::request_geographic_grid(){
  if(GeoGrid::stamp(grib_info?grib_file:QString::fromLocal8Bit(file.c_str()))!=original_source_stamp){auto error=std::make_shared<GeoGrid::Grid>();error->error="SourceChanged: close and reopen the source image";emit geographic_grid_ready(error,{});return;}
  if(coordinates_ready&&verified_geo_grid){emit geographic_grid_ready(verified_geo_grid,verified_geo_lease);return;}
  prepare_map_coordinates();
}
void RasterView::accept_geographic_grid(std::shared_ptr<const GeoGrid::Grid> grid,std::shared_ptr<void> lease){
  verified_geo_grid=std::move(grid);verified_geo_lease=std::move(lease);
  map_display->set_grid(verified_geo_grid,verified_geo_lease);coordinates_ready=true;apply_pending_projection();
  emit geographic_grid_ready(verified_geo_grid,verified_geo_lease);
}
void RasterView::apply_pending_projection(){
  if(restore_projection_pending){restore_projection_pending=false;const auto error=map_display->restore_preferences();
    if(!error.isEmpty()){QSignalBlocker block(projection_selector);projection_selector->setCurrentIndex(int(map_display->mode()));projection_status->setText(tr("Saved projection is unavailable for this source: %1").arg(error));projection_status->setToolTip(projection_status->text());}}
  if(pending_projection){const auto selected=*pending_projection;pending_projection.reset();const auto error=map_display->select_projection(selected);
    if(!error.isEmpty()){QSignalBlocker block(projection_selector);projection_selector->setCurrentIndex(int(map_display->mode()));projection_status->setText(tr("Cannot display %1: %2").arg(MapProjection::name(selected),error));projection_status->setToolTip(projection_status->text());}}
  if(projection_dialog_pending){projection_dialog_pending=false;map_display->show_settings();}
}
void RasterView::fill_palette_selector() {
  QSignalBlocker blocker(palette_selector);palette_selector->clear();
  for(size_t index=0;index<color_maps.size();++index) {
    const auto &palette=color_maps[index];
    palette_selector->addItem(color_map_icon(palette),palette.name,palette.id);
    palette_selector->setItemData(int(index),palette.group+" | "+palette.kind+" | "+palette.license+"\n"+palette.source,Qt::ToolTipRole);
  }
}
void RasterView::choose_color_mapping() {
  ColorMapPicker dialog(color_maps,palette_selector->currentIndex(),color_mapping,this);
  if(dialog.exec()!=QDialog::Accepted) return;
  QSignalBlocker block_palette(palette_selector),block_reverse(reverse_palette);
  palette_selector->setCurrentIndex(dialog.selected_index());color_mapping=dialog.selected_mapping();
  reverse_palette->setChecked(color_mapping.reversed);refresh();
}
QString RasterView::import_color_map(const QString &filename,HdfColorMap::RgbScale scale) {
  HdfColorMap::Palette palette;const auto error=HdfColorMap::import_rgb(filename,scale,palette);
  if(!error.isEmpty()) return error;
  int index=-1;
  for(size_t item=0;item<color_maps.size();++item) if(color_maps[item].id==palette.id) index=int(item);
  if(index<0){color_maps.push_back(palette);index=int(color_maps.size()-1);}
  fill_palette_selector();palette_selector->setCurrentIndex(index);return {};
}
void RasterView::choose_rgb_file() {
  const QString filename=QFileDialog::getOpenFileName(this,tr("Import RGB color map"),{},tr("NCL RGB (*.rgb);;All files (*)"));
  if(filename.isEmpty()) return;
  bool accepted=false;
  const QStringList choices={tr("0..255"),tr("0..1")};
  const QString choice=QInputDialog::getItem(this,tr("RGB range"),tr("Select the encoding used in this file"),choices,0,false,&accepted);
  if(!accepted) return;
  const auto error=import_color_map(filename,choice==choices[1]?HdfColorMap::RgbScale::Unit:HdfColorMap::RgbScale::Byte);
  if(!error.isEmpty()) QMessageBox::warning(this,tr("Cannot import color map"),error);
}

void RasterView::redraw_profile_overlay() {
  for(QGraphicsItem *item : scene->items()) {
    if(item->data(0).toString() == "profileOverlay") {
      scene->removeItem(item);
      delete item;
    }
  }
  if(profile_points.empty() || !profile_mode_action || !profile_mode_action->isChecked()) return;

  const std::vector<QPointF> sampled = curve_sample_path(profile_points, CurvePathMode::SmoothSpline, 0.5);
  if(sampled.size() >= 2) {
    QPainterPath path;
    path.moveTo(sampled.front());
    for(size_t i = 1; i < sampled.size(); ++i) path.lineTo(sampled[i]);
    QPen pen(QColor(0, 220, 255), 0);
    pen.setCosmetic(true);
    pen.setWidthF(2.0);
    auto *curve_item = scene->addPath(path, pen);
    curve_item->setData(0, "profileOverlay");
    curve_item->setZValue(10.0);
  }
  const double radius = std::max(1.5, std::min(last_rows, last_cols) * 0.004);
  QPen dot_pen(QColor(255, 255, 255), 0);
  dot_pen.setCosmetic(true);
  dot_pen.setWidthF(1.0);
  QBrush dot_brush(QColor(220, 40, 40));
  for(const QPointF &pt : profile_points) {
    auto *dot = scene->addEllipse(pt.x() - radius, pt.y() - radius, radius * 2.0, radius * 2.0,
                                  dot_pen, dot_brush);
    dot->setData(0, "profileOverlay");
    dot->setZValue(11.0);
  }
}

void RasterView::show_profile_window() {
  if(!display_valid || profile_points.size() < 2 || !last_values || last_values->empty()) return;
  if(!profile_window) {
    profile_window = new ProfileView(QString::fromUtf8(path.c_str()),
                                     last_values, last_rows, last_cols,
                                      profile_points, CurvePathMode::SmoothSpline, this, raster_reservation);
  } else if(auto *view = qobject_cast<ProfileView*>(profile_window.data())) {
    view->update_data(last_values, last_rows, last_cols, profile_points, CurvePathMode::SmoothSpline, raster_reservation);
  }
  profile_window->show();
  profile_window->raise();
  profile_window->activateWindow();
}


int RasterView::layer_axis_count() const {
  return int(layers.size());
}

QString RasterView::layer_axis_name(int axis) const {
  return axis_names.value(axis);
}

int RasterView::layer_count(int axis) const {
  if(axis < 0 || axis >= layer_axis_count()) return 0;
  if(dims[size_t(axis)]==0)return 0;
  return layers[size_t(axis)]->maximum() + 1;
}

int RasterView::layer_index(int axis) const {
  if(axis < 0 || axis >= layer_axis_count()) return 0;
  return layers[size_t(axis)]->value();
}

void RasterView::set_layer_index(int axis, int index) {
  if(axis < 0 || axis >= layer_axis_count()) return;
  // setValue() -> valueChanged: refresh() reads the plane and layer_changed() is emitted
  layers[size_t(axis)]->setValue(layer_clamp(index, layer_count(axis)));
}
