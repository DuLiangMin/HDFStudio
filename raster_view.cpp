// Read one scalar plane as doubles, mask missing values, render in index order.
#include "raster_view.hpp"
#include "nc_reader.hpp"
#include "nc_classic.hpp"
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
  size_t size=H5Tget_size(type.id);
  if(size==0 || size>65536) return {};
  Handle space{H5Aget_space(attr.id),H5Sclose};
  if(space.id<0 || H5Sget_simple_extent_npoints(space.id)!=1) return {};
  std::vector<char> text(size+1,'\0');
  if(H5Aread(attr.id,type.id,text.data())<0) return {};
  return QString::fromUtf8(text.data()).trimmed();
}

class Canvas : public QGraphicsView {
public:
  using QGraphicsView::QGraphicsView;
  void zoom(double factor) {
    double size = transform().m11() * factor;
    if(size >= 0.002 && size <= 2048) scale(factor, factor);
  }
  void wheelEvent(QWheelEvent *event) override {
    zoom(event->angleDelta().y() > 0 ? 1.25 : 0.8);
    event->accept();
  }
};
}

RasterView::RasterView(std::string filename, std::string varpath, std::vector<hsize_t> shape)
  : file(std::move(filename)), path(std::move(varpath)), dims(std::move(shape)) {
  setWindowTitle(QString::fromUtf8(path.c_str()) + tr(" - Image"));
  auto layout = new QVBoxLayout(this);
  auto tools = new QToolBar(this);
  layout->addWidget(tools);
  auto geo_action=tools->addAction(tr("Geographic coordinates"));
  geo_action->setCheckable(true);
  classic=is_classic_netcdf_file(QString::fromLocal8Bit(file.c_str()));
  NcReader coordinate_reader(QString::fromLocal8Bit(file.c_str()));
  std::vector<size_t> coordinate_shape;
  coordinate_shape.reserve(dims.size());
  for(hsize_t dim:dims) coordinate_shape.push_back(static_cast<size_t>(dim));
  geo_grid=coordinate_reader.geographic_grid(path,coordinate_shape);
  geo_action->setEnabled(geo_grid.valid);
  geo_action->setToolTip(geo_grid.valid
      ? tr("Use verified one-dimensional CF latitude/longitude coordinates")
      : geo_grid.error);
  connect(geo_action,&QAction::toggled,this,[this](bool enabled){geographic=enabled;refresh();});
  palette_selector = new QComboBox(tools);
  palette_selector->addItem(tr("Viridis"), int(RasterColorMap::Palette::Viridis));
  palette_selector->addItem(tr("Plasma"), int(RasterColorMap::Palette::Plasma));
  palette_selector->addItem(tr("Inferno"), int(RasterColorMap::Palette::Inferno));
  palette_selector->addItem(tr("Turbo"), int(RasterColorMap::Palette::Turbo));
  palette_selector->addItem(tr("Grayscale"), int(RasterColorMap::Palette::Grayscale));
  palette_selector->setToolTip(tr("Continuous raster color palette"));
  tools->addWidget(palette_selector);
  reverse_palette = new QCheckBox(tr("Reverse"), tools);
  reverse_palette->setToolTip(tr("Reverse the selected color palette"));
  tools->addWidget(reverse_palette);
  connect(palette_selector, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
  connect(reverse_palette, &QCheckBox::toggled, this, [this] { refresh(); });
  range_mode = new QComboBox(tools);
  range_mode->addItem(tr("Automatic range"), true);
  range_mode->addItem(tr("Fixed range"), false);
  tools->addWidget(range_mode);
  fixed_minimum = new QDoubleSpinBox(tools);
  fixed_minimum->setRange(-1.0e300, 1.0e300);
  fixed_minimum->setDecimals(8);
  fixed_minimum->setPrefix(tr("Min "));
  tools->addWidget(fixed_minimum);
  fixed_maximum = new QDoubleSpinBox(tools);
  fixed_maximum->setRange(-1.0e300, 1.0e300);
  fixed_maximum->setDecimals(8);
  fixed_maximum->setPrefix(tr("Max "));
  tools->addWidget(fixed_maximum);
  fixed_minimum->setEnabled(false);
  fixed_maximum->setEnabled(false);
  connect(range_mode, &QComboBox::currentIndexChanged, this, [this] {
    bool fixed=!range_mode->currentData().toBool();
    fixed_minimum->setEnabled(fixed);
    fixed_maximum->setEnabled(fixed);
    refresh();
  });
  connect(fixed_minimum, &QDoubleSpinBox::valueChanged, this, [this] { refresh(); });
  connect(fixed_maximum, &QDoubleSpinBox::valueChanged, this, [this] { refresh(); });
  auto discrete_toggle=new QCheckBox(tr("Discrete"),tools);
  discrete_toggle->setToolTip(tr("Render each distinct value as a category"));
  tools->addWidget(discrete_toggle);
  connect(discrete_toggle,&QCheckBox::toggled,this,[this](bool enabled){discrete_mode=enabled;refresh();});
  const std::vector<std::string> dimension_names = nc_dimension_names(QString::fromLocal8Bit(this->file.c_str()), this->path);
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
  auto view = new Canvas(scene, this);
  canvas = view;
  canvas->setAcceptDrops(false);
  canvas->viewport()->setAcceptDrops(false);
  canvas->setDragMode(QGraphicsView::ScrollHandDrag);
  canvas->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  canvas->viewport()->setMouseTracking(true);
  canvas->viewport()->installEventFilter(this);
  layout->addWidget(canvas, 1);
  tools->addAction(tr("Fit"), this, [this]{ canvas->fitInView(scene->sceneRect(), Qt::KeepAspectRatio); });
  tools->addAction("+", this, [view]{ view->zoom(1.5); });
  tools->addAction("-", this, [view]{ view->zoom(1/1.5); });
  legend = new QLabel(this);
  layout->addWidget(legend);
  QTimer::singleShot(0, this, [this]{ refresh(); canvas->fitInView(scene->sceneRect(), Qt::KeepAspectRatio); });
}

bool RasterView::eventFilter(QObject *watched,QEvent *event) {
  if(watched==canvas->viewport() && event->type()==QEvent::MouseMove && geographic && geo_grid.valid) {
    auto *mouse=static_cast<QMouseEvent*>(event);
    QPointF point=canvas->mapToScene(mouse->pos());
    const auto row=static_cast<size_t>(std::max(0,int(std::floor(point.y()))));
    const auto column=static_cast<size_t>(std::max(0,int(std::floor(point.x()))));
    const auto coordinate=geographic_coordinate(geo_grid,row,column);
    if(coordinate.valid)
      legend->setToolTip(tr("Index [%1, %2]; latitude %3°, longitude %4°")
        .arg(row).arg(column).arg(coordinate.latitude,0,'g',10).arg(coordinate.longitude,0,'g',10));
    else legend->setToolTip(tr("Pointer is outside the verified coordinate grid."));
  }
  return QWidget::eventFilter(watched,event);
}

void RasterView::refresh() {
  scene->clear();
  legend->setText(tr("Cannot read this plane. Check the dataset and its compression filter."));
  if(dims.size()<2 || std::find(dims.begin(), dims.end(), 0) != dims.end()) return;
  const hsize_t rows = dims[dims.size()-2], cols = dims.back();
  if(rows > INT_MAX || cols > INT_MAX || rows*cols > 16*1024*1024) {
    legend->setText(tr("Plane exceeds the 16 megapixel preview limit.")); return;
  }
  const QString filename=QString::fromLocal8Bit(file.c_str());
  std::vector<hsize_t> start(dims.size(),0), count(dims.size(),1);
  for(size_t i=0; i<layers.size(); ++i) start[i] = layers[i]->value();
  count[count.size()-2] = rows; count.back() = cols;
  // The HDF5 handles stay at -1 for a NetCDF-3 file; Handle ignores negative ids on destruction.
  Handle fid{-1, H5Fclose}, ds{-1, H5Dclose}, space{-1, H5Sclose}, mem{-1, H5Sclose};
  if(!classic) {
    fid.id = H5Fopen(file.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if(fid.id < 0) return;
    ds.id = H5Dopen2(fid.id, path.c_str(), H5P_DEFAULT);
    if(ds.id < 0) return;
    space.id = H5Dget_space(ds.id);
    if(space.id < 0 || H5Sget_simple_extent_ndims(space.id) != int(dims.size())) return;
    if(H5Sselect_hyperslab(space.id,H5S_SELECT_SET,start.data(),nullptr,count.data(),nullptr)<0) return;
    mem.id = H5Screate_simple(2,&count[count.size()-2],nullptr);
  }
  std::vector<double> values;
  bool use_nc=classic || filename.endsWith(".nc",Qt::CaseInsensitive) || filename.endsWith(".nc4",Qt::CaseInsensitive);
  if(use_nc) {
    NcReader nc(filename); int group, variable;
    if(nc.locate(path,group,variable)) {
      NcPlane plane;
      auto error=nc.read_plane(path,{start.begin(),start.end()-2},plane);
      if(!error.isEmpty()) { legend->setText(error); return; }
      if(plane.rows!=rows || plane.cols!=cols) return;
      values=std::move(plane.values);
    } else if(classic) {
      legend->setText(tr("Variable %1 not found in this NetCDF file.").arg(QString::fromStdString(path))); return;
    } else use_nc=false;
  }
  if(!use_nc) {
    values.resize(rows*cols);
    if(mem.id<0 || H5Dread(ds.id,H5T_NATIVE_DOUBLE,mem.id,space.id,H5P_DEFAULT,values.data())<0) return;
  }
  setProperty("reader",use_nc?"netCDF-C":"HDF5");
  double fill=NAN, missing=NAN, scale=1, offset=0;
  if(!use_nc) {
    scalar_attr(ds.id,"_FillValue",fill); scalar_attr(ds.id,"missing_value",missing);
    scalar_attr(ds.id,"scale_factor",scale); scalar_attr(ds.id,"add_offset",offset);
  }
  double low=INFINITY, high=-INFINITY;
  category_values.clear();
  for(double &v: values) {
    v = (v==fill || v==missing) ? NAN : v*scale+offset;
    if(std::isfinite(v)) {
      low=std::min(low,v); high=std::max(high,v);
      if(discrete_mode && std::find(category_values.begin(),category_values.end(),v)==category_values.end())
        category_values.push_back(v);
    }
  }
  std::sort(category_values.begin(),category_values.end());
  auto declared_categories=classic ? nc_classic_numeric_attribute(filename,path,"flag_values")
                                   : numeric_array_attr(ds.id,"flag_values");
  if(!declared_categories.empty()) category_values=std::move(declared_categories);
  const QString meaning_text=classic ? nc_classic_text_attribute(filename,path,"flag_meanings")
                                     : text_array_attr(ds.id,"flag_meanings");
  auto meanings=meaning_text.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
  category_labels=RasterColorMap::categoryLabels(category_values,meanings);
  bool automatic=range_mode->currentData().toBool();
  auto range=RasterColorMap::resolveRange(automatic,low,high,
      fixed_minimum->value(),fixed_maximum->value(),std::isfinite(low));
  if(discrete_mode) range={low,high,std::isfinite(low)};
  if(discrete_mode && category_values.size()>12) {
    legend->setText(tr("Discrete preview supports at most 12 categories; found %1.").arg(category_values.size()));
    return;
  }
  if(!range.valid) {
    legend->setText(automatic ? tr("No valid values") : tr("Invalid fixed range: minimum must be less than maximum."));
    return;
  }
  low=range.minimum;
  high=range.maximum;
  QImage image(int(cols),int(rows),QImage::Format_RGB32);
  if(image.isNull()) return;
  for(int y=0;y<image.height();++y) {
    auto line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for(int x=0;x<image.width();++x) {
      double v=values[size_t(y)*cols+x];
      const auto palette=static_cast<RasterColorMap::Palette>(palette_selector->currentData().toInt());
      line[x]=(discrete_mode
        ? RasterColorMap::discreteValueColor(palette,v,category_values,reverse_palette->isChecked(),QColor(255,0,255))
        : RasterColorMap::colorValue(palette,v,low,high,reverse_palette->isChecked())).rgb();
    }
  }
  scene->addPixmap(QPixmap::fromImage(image));
  setProperty("minimum",low); setProperty("maximum",high);
  scene->setSceneRect(image.rect());
  QString coordinate_note=geographic && geo_grid.valid
    ? tr("   Geographic lookup: %1 / %2").arg(QString::fromUtf8(geo_grid.latitudePath.c_str()),QString::fromUtf8(geo_grid.longitudePath.c_str()))
    : QString();
  legend->setText(discrete_mode
    ? tr("%1 x %2   Categories: %3   Unknown: magenta   Missing: grey").arg(cols).arg(rows).arg(category_labels.join("; "))
    : tr("%1 x %2   Min %3   Max %4   Missing: grey%5")
    .arg(cols).arg(rows).arg(low,0,'g',9).arg(high,0,'g',9)
    .arg(automatic ? tr(" (automatic)") : tr(" (fixed)")));
  if(!coordinate_note.isEmpty()) legend->setText(legend->text()+coordinate_note);
}

int RasterView::layer_axis_count() const {
  return int(layers.size());
}

QString RasterView::layer_axis_name(int axis) const {
  return axis_names.value(axis);
}

int RasterView::layer_count(int axis) const {
  if(axis < 0 || axis >= layer_axis_count()) return 0;
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
