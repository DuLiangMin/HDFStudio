// Calculate entropy in data space, then map the full-resolution entropy plane to colors.
#include "texture_entropy_view.hpp"
#include "raster_view.hpp"
#include "raster_budget.hpp"
#include "plane_reader.hpp"
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <climits>

namespace {
class TextureCanvas : public QGraphicsView {
public:
  using QGraphicsView::QGraphicsView;
  void zoom(double factor) {
    const double current = std::abs(transform().m11());
    if(current <= 0) return;
    const double target = std::clamp(current * factor, .002, 2048.);
    scale(target / current, target / current);
  }
protected:
  void wheelEvent(QWheelEvent *event) override {
    const double steps = double(event->angleDelta().y()) / 120;
    if(steps == 0) { event->ignore(); return; }
    zoom(std::pow(1.25, std::clamp(steps, -128., 128.))); event->accept();
  }
};
}

TextureEntropyView::TextureEntropyView(std::string file, std::string path, std::vector<hsize_t> dims)
  : m_file(std::move(file)), m_path(std::move(path)), m_dims(std::move(dims)) {
  setup();
}

TextureEntropyView::TextureEntropyView(RasterView *source) : m_following(true), m_source(source) {
  if(source) {
    const auto snapshot = source->plane_snapshot();
    m_file = snapshot.file_path.toLocal8Bit().toStdString();
    m_path = snapshot.variable_path.toUtf8().toStdString();
    connect(source, &RasterView::plane_snapshot_changed, this, [this] { refresh(); });
    connect(source, &RasterView::layer_changed, this, [this] { emit layer_changed(); });
    connect(source, &QObject::destroyed, this, [this] {
      m_source.clear(); clear_result(tr("Source image closed. Open Texture Entropy Image again from a variable."));
      emit layer_changed();
    });
  }
  setup();
}

void TextureEntropyView::setup() {
  setObjectName("textureEntropyView");
  setWindowTitle(QString::fromUtf8(m_path.c_str()) + tr(" - Texture Entropy Image"));
  auto *layout = new QVBoxLayout(this);
  auto *tools = new QToolBar(this); layout->addWidget(tools);
  if(m_following) {
    auto *label = new QLabel(tr("Following Image View: layer and Mask"), this);
    label->setObjectName("textureSourceLabel"); tools->addWidget(label);
  } else {
    const auto names = nc_dimension_names(QString::fromLocal8Bit(m_file.c_str()), m_path);
    for(size_t axis = 0; axis + 2 < m_dims.size(); ++axis) {
      const QString name = names.size() == m_dims.size() ? QString::fromStdString(names[axis]) : tr("axis %1").arg(axis + 1);
      m_axis_names << name;
      tools->addWidget(new QLabel(name + ": ", this));
      auto *spin = new QSpinBox(this);
      spin->setRange(0, int(std::min<hsize_t>(m_dims[axis] ? m_dims[axis] - 1 : 0, hsize_t(INT_MAX))));
      spin->setEnabled(m_dims[axis] > 0);
      spin->setAccessibleName(tr("Texture layer %1").arg(axis + 1));
      tools->addWidget(spin); m_layers.push_back(spin);
      connect(spin, &QSpinBox::valueChanged, this, [this] { refresh(); emit layer_changed(); });
    }
  }
  auto *canvas = new TextureCanvas(new QGraphicsScene(this), this); m_canvas = canvas;
  m_image = canvas->scene()->addPixmap(QPixmap());
  canvas->setAccessibleName(tr("Texture entropy 2D image"));
  canvas->setObjectName("textureEntropyCanvas");
  canvas->setDragMode(QGraphicsView::ScrollHandDrag);
  canvas->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  canvas->viewport()->setMouseTracking(true); canvas->viewport()->installEventFilter(this);
  auto *zoom_in = tools->addAction(tr("Zoom in"), this, [canvas] { canvas->zoom(1.25); });
  zoom_in->setObjectName("textureZoomInAction");
  tools->addAction(tr("Zoom out"), this, [canvas] { canvas->zoom(.8); });
  tools->addAction(tr("Fit"), this, [canvas] { canvas->fitInView(canvas->sceneRect(), Qt::KeepAspectRatio); });
  m_export_png = tools->addAction(tr("Export PNG"), this, [this] {
    const QString path = QFileDialog::getSaveFileName(this, tr("Export texture entropy PNG"), {}, tr("PNG (*.png)"));
    if(path.isEmpty()) return;
    const QString error = export_png(path);
    if(!error.isEmpty()) QMessageBox::warning(this, tr("Texture entropy export"), error);
  });
  m_export_png->setObjectName("textureExportPngAction");
  m_export_csv = tools->addAction(tr("Export CSV"), this, [this] {
    const QString path = QFileDialog::getSaveFileName(this, tr("Export texture entropy values"), {}, tr("CSV (*.csv)"));
    if(path.isEmpty()) return;
    const QString error = export_csv(path);
    if(!error.isEmpty()) QMessageBox::warning(this, tr("Texture entropy export"), error);
  });
  m_export_csv->setObjectName("textureExportCsvAction");
  if(!m_following) {
    auto *mask = new QHBoxLayout;
    mask->addWidget(new QLabel(tr("Mask decoded range (inclusive):"), this));
    m_mask_start = new QLineEdit(this); m_mask_end = new QLineEdit(this);
    m_mask_start->setObjectName("textureMaskStart"); m_mask_end->setObjectName("textureMaskEnd");
    m_mask_start->setAccessibleName(tr("Texture mask start")); m_mask_end->setAccessibleName(tr("Texture mask end"));
    m_mask_start->setPlaceholderText(tr("start (-inf)")); m_mask_end->setPlaceholderText(tr("end (+inf)"));
    mask->addWidget(m_mask_start); mask->addWidget(m_mask_end);
    auto *apply = new QPushButton(tr("Apply Mask"), this); apply->setObjectName("textureApplyMask");
    mask->addWidget(apply); layout->addLayout(mask);
    connect(apply, &QPushButton::clicked, this, &TextureEntropyView::refresh);
    connect(m_mask_start, &QLineEdit::editingFinished, this, &TextureEntropyView::refresh);
    connect(m_mask_end, &QLineEdit::editingFinished, this, &TextureEntropyView::refresh);
  }
  layout->addWidget(canvas, 1);
  m_bar = new ColorBar(this); m_bar->set_combined_missing_mask(true); layout->addWidget(m_bar);
  m_summary = new QLabel(this); m_summary->setObjectName("textureEntropySummary");
  m_summary->setWordWrap(true); m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_summary);
  m_summary->setToolTip(tr("Local gray-level distribution complexity, not a proof of useful signal. Noise can have high entropy; a smooth gradient can be nonzero. Equal local histograms have equal entropy regardless of pixel arrangement. Interpolation adds no new detail."));
  refresh();
  QTimer::singleShot(0, this, [this] { m_canvas->fitInView(m_canvas->sceneRect(), Qt::KeepAspectRatio); });
}

void TextureEntropyView::clear_result(const QString &error) {
  m_entropy = {}; m_snapshot = {}; m_decoded.reset(); m_cached_indices.clear();
  m_image->setPixmap(QPixmap()); m_canvas->scene()->setSceneRect(QRectF());
  m_canvas->viewport()->setToolTip({}); m_bar->clear(); m_summary->setText(error);
  m_export_png->setEnabled(false); m_export_csv->setEnabled(false);
  m_reservation.reset(); m_reserved_pixels = 0;
  setProperty("textureValid", false);
}

void TextureEntropyView::refresh() {
  PlaneSnapshot snapshot;
  if(m_following) {
    if(!m_source) { clear_result(tr("Source image closed")); return; }
    snapshot = m_source->plane_snapshot();
    if(!snapshot.values || snapshot.out_of_range) { clear_result(tr("Source plane unavailable or out of range")); return; }
    if(m_entropy.valid && snapshot.values == m_snapshot.values && snapshot.revision == m_snapshot.revision) return;
  } else {
    if(m_dims.size() < 2 || std::find(m_dims.begin(), m_dims.end(), 0) != m_dims.end()) {
      clear_result(tr("No frames or invalid plane dimensions")); return;
    }
    snapshot.rows = size_t(m_dims[m_dims.size() - 2]); snapshot.cols = size_t(m_dims.back());
    for(auto *spin : m_layers) snapshot.leading_indices.push_back(size_t(spin->value()));
  }
  if(!snapshot.rows || !snapshot.cols || snapshot.rows > INT_MAX || snapshot.cols > INT_MAX ||
     snapshot.rows > size_t(16 * 1024 * 1024) / snapshot.cols) {
    clear_result(tr("Texture entropy exceeds the 16 megapixel limit")); return;
  }
  const quint64 pixels = quint64(snapshot.rows) * snapshot.cols;
  if(!m_reservation || m_reserved_pixels != pixels) {
    m_entropy = {}; m_snapshot = {}; m_decoded.reset(); m_image->setPixmap(QPixmap()); m_reservation.reset();
    m_reservation = reserve_raster(pixels); m_reserved_pixels = pixels;
    if(!m_reservation) { clear_result(tr("Raster memory budget reached. Close another image.")); return; }
  }
  if(!m_following) {
    if(!m_decoded || m_cached_indices != snapshot.leading_indices) {
      m_snapshot = {}; m_decoded.reset(); m_entropy = {};
      auto plane = nc_read_plane_decoded(QString::fromLocal8Bit(m_file.c_str()), m_path, m_dims, snapshot.leading_indices);
      setProperty("planeReadCount", property("planeReadCount").toULongLong() + 1);
      if(!plane.error.isEmpty()) { clear_result(plane.error); return; }
      m_decoded = std::make_shared<const std::vector<double>>(std::move(plane.plane.values));
      m_cached_indices = snapshot.leading_indices;
    }
    snapshot.values = m_decoded;
    const bool masking = !m_mask_start->text().trimmed().isEmpty() || !m_mask_end->text().trimmed().isEmpty();
    double start = -INFINITY, end = INFINITY;
    if(!nc_mask_bound(m_mask_start->text(), -INFINITY, start) || !nc_mask_bound(m_mask_end->text(), INFINITY, end) || (masking && start > end)) {
      clear_result(tr("Invalid Mask range: enter increasing decoded values")); return;
    }
    if(masking) {
      auto masked = std::make_shared<std::vector<double>>(*m_decoded);
      nc_mask_range(*masked, start, end); snapshot.values = masked;
    }
  }
  m_entropy = {}; // Release the previous full-resolution result before allocating the next.
  m_entropy = nc_texture_entropy(*snapshot.values, snapshot.rows, snapshot.cols);
  if(!m_entropy.valid) { const QString error = m_entropy.error; clear_result(error); return; }
  QString error;
  const auto palettes = HdfColorMap::load_registry(error);
  const auto selected = std::find_if(palettes.begin(), palettes.end(), [](const HdfColorMap::Palette &palette) { return palette.id == "bids:viridis"; });
  if(selected == palettes.end()) { clear_result(tr("Texture entropy color map unavailable")); return; }
  HdfColorMap::Mapping mapping; mapping.minimum = 0; mapping.maximum = 4;
  QImage image(int(m_entropy.cols), int(m_entropy.rows), QImage::Format_RGB32);
  if(image.isNull()) { clear_result(tr("Cannot allocate texture entropy image")); return; }
  for(int row = 0; row < image.height(); ++row) {
    auto *line = reinterpret_cast<QRgb*>(image.scanLine(row));
    for(int col = 0; col < image.width(); ++col)
      line[col] = HdfColorMap::map_value(*selected, mapping, m_entropy.values[size_t(row) * m_entropy.cols + size_t(col)]);
  }
  const bool first = m_image->pixmap().isNull(); m_image->setPixmap(QPixmap::fromImage(image));
  m_canvas->scene()->setSceneRect(0, 0, image.width(), image.height());
  m_canvas->viewport()->setToolTip({});
  m_bar->set_mapping(*selected, mapping, tr("Texture entropy (bits), 0 low / 4 high"));
  QStringList indices; for(size_t index : snapshot.leading_indices) indices << QString::number(index);
  m_summary->setText(tr("2D texture entropy | %1 x %2 pixels | layer [%3]\n4x4 blocks: %4 x %5; 256 shared gray levels; bilinear to source size; 0..4 bits\nValid samples: %6; excluded: %7. Missing/Mask stay gray. Higher = richer local gray distribution (noise may also be high).")
    .arg(m_entropy.rows).arg(m_entropy.cols).arg(indices.join(","))
    .arg(m_entropy.block_rows).arg(m_entropy.block_cols).arg(m_entropy.valid_count).arg(m_entropy.excluded_count));
  m_snapshot = std::move(snapshot);
  m_export_png->setEnabled(true); m_export_csv->setEnabled(true);
  setProperty("textureValid", true); setProperty("sourceRevision", m_snapshot.revision);
  setProperty("textureComputeCount", property("textureComputeCount").toULongLong() + 1);
  if(first) m_canvas->fitInView(m_canvas->sceneRect(), Qt::KeepAspectRatio);
}

RasterView *TextureEntropyView::source_view() const { return m_source.data(); }
int TextureEntropyView::layer_axis_count() const { return m_following ? (m_source ? m_source->layer_axis_count() : 0) : int(m_layers.size()); }
QString TextureEntropyView::layer_axis_name(int axis) const { return m_following ? (m_source ? m_source->layer_axis_name(axis) : QString()) : m_axis_names.value(axis); }
int TextureEntropyView::layer_count(int axis) const {
  if(m_following) return m_source ? m_source->layer_count(axis) : 0;
  return axis >= 0 && axis < int(m_layers.size()) ? int(std::min<hsize_t>(m_dims[size_t(axis)], INT_MAX)) : 0;
}
int TextureEntropyView::layer_index(int axis) const {
  if(m_following) return m_source ? m_source->layer_index(axis) : 0;
  return axis >= 0 && axis < int(m_layers.size()) ? m_layers[size_t(axis)]->value() : 0;
}
void TextureEntropyView::set_layer_index(int axis, int index) {
  if(m_following) { if(m_source) m_source->set_layer_index(axis, index); return; }
  if(axis >= 0 && axis < int(m_layers.size()) && layer_count(axis) > 0) m_layers[size_t(axis)]->setValue(index);
}
QImage TextureEntropyView::image_for_export() const { return m_entropy.valid ? m_image->pixmap().toImage() : QImage(); }
QString TextureEntropyView::export_png(const QString &path) const {
  if(!m_entropy.valid) return tr("No valid texture entropy image");
  QSaveFile output(path);
  if(!output.open(QIODevice::WriteOnly) || !image_for_export().save(&output, "PNG") || !output.commit()) return output.errorString();
  return {};
}
QString TextureEntropyView::export_csv(const QString &path) const {
  if(!m_entropy.valid) return tr("No valid texture entropy image");
  QSaveFile output(path); if(!output.open(QIODevice::WriteOnly)) return output.errorString();
  QTextStream text(&output); text.setRealNumberPrecision(17);
  text << "row,col,entropy_bits,block_row,block_col,block_entropy_bits\n";
  for(size_t row = 0; row < m_entropy.rows; ++row) for(size_t col = 0; col < m_entropy.cols; ++col)
    text << row << ',' << col << ',' << m_entropy.values[row * m_entropy.cols + col] << ',' << row / 4 << ',' << col / 4 << ','
         << m_entropy.block_entropy[(row / 4) * m_entropy.block_cols + col / 4] << '\n';
  text.flush();
  if(text.status() != QTextStream::Ok || !output.commit()) return output.errorString();
  return {};
}
bool TextureEntropyView::eventFilter(QObject *watched, QEvent *event) {
  if(watched == m_canvas->viewport() && event->type() == QEvent::MouseMove && m_entropy.valid) {
    const auto *mouse = static_cast<QMouseEvent*>(event); const QPointF point = m_canvas->mapToScene(mouse->pos());
    QString text = tr("Outside texture entropy image");
    if(point.x() >= 0 && point.y() >= 0 && point.x() < m_entropy.cols && point.y() < m_entropy.rows) {
      const size_t row = size_t(point.y()), col = size_t(point.x());
      const double value = m_entropy.values[row * m_entropy.cols + col];
      text = std::isfinite(value) ? tr("Index [%1, %2]; texture entropy %3 bits; block [%4, %5]").arg(row).arg(col).arg(value, 0, 'g', 10).arg(row / 4).arg(col / 4)
                                  : tr("Index [%1, %2]; missing or masked source").arg(row).arg(col);
    }
    m_canvas->viewport()->setToolTip(text);
  }
  return QWidget::eventFilter(watched, event);
}
