// Histogram window: one layer of a variable, sampled at random (default one value in twenty).
#include "histogram_view.hpp"
#include "data_read_controller.hpp"
#include <climits>
#include <algorithm>
#include <cmath>

HistogramCanvas::HistogramCanvas(QWidget *parent) : QWidget(parent) {
  setObjectName("histogramCanvas");
  setMinimumHeight(220);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void HistogramCanvas::set_histogram(const NcHistogram &histogram) {
  m_histogram = histogram;
  setProperty("bins", histogram.bins);
  setProperty("sampled", qulonglong(histogram.sampled));
  update();
}

void HistogramCanvas::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRect area = rect().adjusted(56, 12, -12, -30);
  painter.fillRect(rect(), Qt::white);

  const int bins = m_histogram.valid ? m_histogram.bins : 0;
  if(bins < 1 || area.width() < 20 || area.height() < 20) {
    painter.setPen(Qt::black);
    painter.drawText(rect(), Qt::AlignCenter,
                     m_histogram.error.isEmpty() ? tr("No values") : m_histogram.error);
    return;
  }

  // axes
  painter.setPen(QPen(QColor(120, 120, 120), 1));
  painter.drawLine(area.bottomLeft(), area.bottomRight());
  painter.drawLine(area.bottomLeft(), area.topLeft());

  const size_t peak = *std::max_element(m_histogram.counts.begin(), m_histogram.counts.end());
  const double slot = double(area.width()) / double(bins);
  for(int bin = 0; bin < bins; ++bin) {
    const size_t count = m_histogram.counts[size_t(bin)];
    if(count == 0) continue;
    const int height = peak == 0 ? 0 : int(double(count) / double(peak) * double(area.height()) + 0.5);
    QRect bar(int(area.left() + slot * bin) + 1, area.bottom() - height,
              std::max(1, int(slot) - 1), height);
    painter.fillRect(bar, QColor(70, 120, 180));
  }

  // tick labels: first, middle and last bin bound, plus the peak count
  painter.setPen(Qt::black);
  const QFontMetrics metrics(painter.font());
  auto label = [&](const QString &text, int x) {
    const int width=metrics.horizontalAdvance(text)+4;
    const int left=std::clamp(x-width/2,2,std::max(2,this->width()-width-2));
    painter.drawText(QRect(left,area.bottom()+4,width,20),Qt::AlignHCenter|Qt::AlignVCenter,text);
  };
  const QString low = QString::number(m_histogram.edges.front(), 'g', 6);
  const QString high = QString::number(m_histogram.edges.back(), 'g', 6);
  const QString middle = QString::number((m_histogram.edges.front() + m_histogram.edges.back()) / 2.0, 'g', 6);
  label(low, area.left());
  if(metrics.horizontalAdvance(low) + metrics.horizontalAdvance(high) + 40 < area.width())
    label(middle, area.center().x());
  label(high, area.right());
  painter.drawText(QRect(2, area.top() - 8, 52, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(qulonglong(peak)));
}

HistogramView::HistogramView(std::string filename, std::string varpath, std::vector<hsize_t> shape)
  :HistogramView(std::move(filename),std::move(varpath),std::move(shape),std::nullopt) {}

HistogramView::HistogramView(QString filename,QString target,const DataSource::ArrayInfo &info)
  :HistogramView(filename.toUtf8().toStdString(),target.toUtf8().toStdString(),
                 std::vector<hsize_t>(info.shape.begin(),info.shape.end()),info) {}

HistogramView::HistogramView(std::string filename,std::string varpath,std::vector<hsize_t> shape,
                             std::optional<DataSource::ArrayInfo> info)
  :m_file(std::move(filename)),m_path(std::move(varpath)),m_dims(std::move(shape)),m_grib_info(std::move(info)) {
  if(m_grib_info)m_grib_file=QString::fromUtf8(m_file.c_str());
  setObjectName("histogramView");
  setWindowTitle(QString::fromStdString(m_path) + tr(" - Histogram"));

  auto layout = new QVBoxLayout(this);
  auto tools = new QToolBar(this);
  layout->addWidget(tools);

  // The plane to look at: one index per axis before the last two.
  for(size_t axis = 0; axis + 2 < m_dims.size(); ++axis) {
    auto spin = new QSpinBox(this);
    spin->setRange(0, int(std::min<hsize_t>(m_dims[axis] ? m_dims[axis] - 1 : 0, hsize_t(INT_MAX))));
    spin->setToolTip(tr("Layer of axis %1").arg(int(axis) + 1));
    spin->setAccessibleName(tr("Histogram layer %1").arg(int(axis) + 1));
    tools->addWidget(new QLabel(tr(" Layer %1: ").arg(int(axis) + 1), this));
    tools->addWidget(spin);
    m_layers.push_back(spin);
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
      read_plane();          // another layer: its values must be read again
      update_histogram();
      emit layer_changed();
    });
  }
  std::vector<std::string> names;
  if(m_grib_info)for(const auto &axis:m_grib_info->axes)names.push_back(axis.name.toStdString());
  else names=nc_dimension_names(QString::fromLocal8Bit(m_file.c_str()),m_path);
  for(size_t axis = 0; axis < m_layers.size(); ++axis)
    m_axis_names << (names.size() == m_dims.size() ? QString::fromStdString(names[axis])
                                                   : tr("axis %1").arg(int(axis) + 1));

  tools->addSeparator();
  tools->addWidget(new QLabel(tr(" Sample 1 in "), this));
  m_every_nth = new QSpinBox(this);
  m_every_nth->setRange(1, 10000);
  m_every_nth->setValue(20);           // 1/20 = 0.05 of the values
  m_every_nth->setAccessibleName(tr("Sample 1 in N"));
  m_every_nth->setToolTip(tr("Random sample: keep one value in N (20 = 5% of the plane). "
                             "1 uses every value."));
  tools->addWidget(m_every_nth);

  tools->addWidget(new QLabel(tr("  Bins: "), this));
  m_bins = new QSpinBox(this);
  m_bins->setRange(1, 500);
  m_bins->setValue(32);
  m_bins->setAccessibleName(tr("Bins"));
  m_bins->setToolTip(tr("Number of bins between the smallest and the largest kept value"));
  tools->addWidget(m_bins);

  auto resample = new QAction(tr("Draw a new sample"), this);
  resample->setToolTip(tr("Keep a different random subset of the same size"));
  tools->addAction(resample);
  connect(resample, &QAction::triggered, this, [this] {
    ++m_seed;                          // another subset, same fraction and bins
    update_histogram();
  });

  m_summary = new QLabel(this);
  m_summary->setObjectName("histogramSummary");
  m_summary->setWordWrap(true);
  m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_summary);
  m_canvas = new HistogramCanvas(this);
  layout->addWidget(m_canvas, 1);

  // The plane stays in memory, so both options only re-sample and re-bin: changing the step or the
  // number of bins never reads the file again.
  connect(m_every_nth, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { update_histogram(); });
  connect(m_bins, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { update_histogram(); });

  if(m_grib_info) {
    m_grib_reader=new DataReadController;
    connect(m_grib_reader,&DataReadController::completed,this,[this](auto plane,auto budget){
      const auto current=GribData::describe(m_grib_file,QString::fromUtf8(m_path.c_str()));
      if(!current.error.isEmpty()||current.object.source_session_id!=m_grib_info->object.source_session_id){clear_plane(tr("SourceChanged: close and reopen source"));return;}
      if(m_dims.size()<2||plane->rows!=m_dims[m_dims.size()-2]||plane->cols!=m_dims.back()||plane->values.size()!=plane->rows*plane->cols){clear_plane(tr("Incomplete GRIB plane"));return;}
      m_values=std::shared_ptr<const std::vector<double>>(plane,&plane->values);m_budget=std::move(budget);
      setProperty("reading",false);setProperty("readError",QString());setProperty("reader",plane->reader);
      setProperty("decodeProfile",plane->decode_profile);setProperty("units",plane->units);
      setProperty("planeReadCount",property("planeReadCount").toULongLong()+1);
      setProperty("planeValues",qulonglong(m_values->size()));
      m_entropy=nc_gray_entropy(*m_values);update_histogram();
    });
    connect(m_grib_reader,&DataReadController::failed,this,[this](const QString &error){clear_plane(error);});
  }
  read_plane();
  update_histogram();
}

HistogramView::~HistogramView(){if(m_grib_reader){m_grib_reader->disconnect(this);m_grib_reader->dispose();}}

void HistogramView::clear_plane(const QString &error) {
  m_values.reset();m_budget.reset();m_entropy={};
  setProperty("reading",false);setProperty("readError",error);setProperty("planeValues",qulonglong(0));
  update_histogram();
}

double HistogramView::keep_fraction() const {
  return 1.0 / double(std::max(1, m_every_nth->value()));
}

void HistogramView::read_plane() {
  std::vector<size_t> start;
  for(QSpinBox *spin : m_layers) start.push_back(size_t(std::max(0, spin->value())));
  const quint64 cells=m_dims.size()<2?0:quint64(m_dims[m_dims.size()-2])*quint64(m_dims.back());
  setProperty("planeCells",qulonglong(cells));
  if(m_grib_info) {
    clear_plane(tr("Reading GRIB plane..."));
    if(!m_grib_info->error.isEmpty()){clear_plane(m_grib_info->error);return;}
    if(!m_grib_info->can_image){clear_plane(m_grib_info->disabled_reasons.value("image",tr("Histogram requires a rectangular GRIB plane")));return;}
    setProperty("reading",true);
    m_grib_reader->read(m_grib_file,QString::fromUtf8(m_path.c_str()),start,false,false);
    return;
  }
  // The whole plane is decoded once and kept: the file is read once, then every step and bin change
  // is a memory pass over values that are already there.
  NcPlaneResult result = nc_read_plane_decoded(QString::fromLocal8Bit(m_file.c_str()),
                                                     m_path, m_dims, start);
  setProperty("reader", result.reader);
  setProperty("readError", result.error);
  m_values=result.error.isEmpty()?std::make_shared<const std::vector<double>>(std::move(result.plane.values)):nullptr;
  setProperty("planeReadCount",property("planeReadCount").toULongLong()+1);
  m_entropy=m_values?nc_gray_entropy(*m_values):NcGrayEntropy{};
  setProperty("entropyBits",m_entropy.valid ? QVariant(m_entropy.entropy_bits) : QVariant());
  setProperty("planeValues",qulonglong(m_values?m_values->size():0));
  // how many positions the whole plane has, so the window can say "of how many"
  quint64 plane = m_dims.size() >= 2 ? quint64(m_dims[m_dims.size() - 2]) * quint64(m_dims.back()) : 0;
  setProperty("planeCells", qulonglong(plane));
}

void HistogramView::update_histogram() {
  setProperty("entropyBits",m_entropy.valid?QVariant(m_entropy.entropy_bits):QVariant());
  const QString entropy_text=m_entropy.valid
    ? tr("Whole-plane histogram entropy (256 bins): %1 bits   Normalized: %2").arg(m_entropy.entropy_bits,0,'f',4).arg(m_entropy.normalized_entropy,0,'g',9)
    : tr("Whole-plane histogram entropy (256 bins): N/A");
  if(!m_values||m_values->empty()) {
    for(const auto key:{"sampled","total","missing","bins","minimum","maximum","mean","stddev"})setProperty(key,QVariant());
    m_summary->setText(tr("Cannot read this plane: %1\n%2").arg(property("readError").toString(),entropy_text));
    NcHistogram empty;empty.error=property("readError").toString();m_canvas->set_histogram(empty);
    return;
  }
  // A uniform random sample of the plane: 1 in N keeps exactly round(finite / N) values.
  const NcHistogram histogram=nc_histogram(*m_values,keep_fraction(),m_bins->value(),m_seed);
  m_canvas->set_histogram(histogram);
  // the same numbers a test and hdf_cli hist see
  setProperty("sampled", qulonglong(histogram.sampled));
  setProperty("total", qulonglong(histogram.total));
  setProperty("missing", qulonglong(histogram.missing));
  setProperty("bins", histogram.bins);
  setProperty("minimum", histogram.minimum);
  setProperty("maximum", histogram.maximum);
  setProperty("mean", histogram.mean);
  setProperty("stddev", histogram.stddev);
  setProperty("everyNth", m_every_nth->value());
  setProperty("keepFraction", histogram.keep_fraction);
  if(!histogram.valid) {
    m_summary->setText(histogram.error+"\n"+entropy_text);
    return;
  }
  m_summary->setText(tr("%1 of %2 values sampled at random (1 in %3)   bins %4   min %5   max %6   "
                        "mean %7   stddev %8   NaN among them %9")
      .arg(histogram.sampled).arg(histogram.total).arg(m_every_nth->value())
      .arg(histogram.bins)
      .arg(histogram.minimum, 0, 'g', 9).arg(histogram.maximum, 0, 'g', 9)
       .arg(histogram.mean, 0, 'g', 9).arg(histogram.stddev, 0, 'g', 9).arg(histogram.missing)+"\n"+entropy_text);
  if(m_grib_info)m_summary->setText(m_summary->text()+tr("\nGRIB decoded values | units: %1").arg(property("units").toString()));
}

int HistogramView::layer_axis_count() const { return int(m_layers.size()); }

QString HistogramView::layer_axis_name(int axis) const { return m_axis_names.value(axis); }

int HistogramView::layer_count(int axis) const {
  if(axis < 0 || axis >= layer_axis_count()) return 0;
  return m_layers[size_t(axis)]->maximum() + 1;
}

int HistogramView::layer_index(int axis) const {
  if(axis < 0 || axis >= layer_axis_count()) return 0;
  return m_layers[size_t(axis)]->value();
}

void HistogramView::set_layer_index(int axis, int index) {
  if(axis < 0 || axis >= layer_axis_count()) return;
  m_layers[size_t(axis)]->setValue(layer_clamp(index, layer_count(axis)));
}
