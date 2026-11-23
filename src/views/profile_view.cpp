#include "profile_view.hpp"
#include <QFileDialog>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <algorithm>
#include <cmath>

ProfileCanvas::ProfileCanvas(QWidget *parent) : QWidget(parent) {
  setObjectName("profileCanvas");
  setMinimumSize(460, 260);
}

void ProfileCanvas::set_profile(CurveProfileResult profile) {
  m_profile = std::move(profile);
  update();
}

void ProfileCanvas::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), palette().base());
  painter.setRenderHint(QPainter::Antialiasing, true);

  const QString high_label = QString::number(m_profile.maximum, 'g', 6);
  const QString low_label = QString::number(m_profile.minimum, 'g', 6);
  const QFontMetricsF metrics(painter.font());
  const qreal label_width = std::max(metrics.horizontalAdvance(high_label), metrics.horizontalAdvance(low_label));
  const qreal label_height = metrics.height();
  const QRectF area = QRectF(rect()).adjusted(std::max(qreal(60), label_width + 16), 20, -20,
                                             -std::max(qreal(34), label_height + 12));
  if(area.width() <= 10 || area.height() <= 10) return;
  painter.setPen(QPen(palette().mid().color(), 1));
  painter.drawRect(area);

  if(!m_profile.valid || m_profile.finite_count == 0 || m_profile.points.empty()) {
    painter.setPen(palette().text().color());
    painter.drawText(area, Qt::AlignCenter,
                     m_profile.error.isEmpty() ? tr("No finite values along curve") : m_profile.error);
    return;
  }

  const double min_v = m_profile.minimum;
  const double max_v = m_profile.maximum;
  const double span_v = (max_v > min_v) ? (max_v - min_v) : 1.0;
  const double total_d = m_profile.total_length > 0.0 ? m_profile.total_length : 1.0;

  auto to_screen = [&](const ProfilePoint &pt) -> QPointF {
    const double fx = pt.distance / total_d;
    const double fy = (max_v > min_v) ? ((pt.value - min_v) / span_v) : 0.5;
    return QPointF(area.left() + fx * area.width(),
                   area.bottom() - fy * area.height());
  };

  painter.setPen(QPen(QColor(25, 110, 200), 2));
  QPainterPath path;
  bool pen_down = false;
  for(const ProfilePoint &pt : m_profile.points) {
    if(!std::isfinite(pt.value)) {
      pen_down = false;
      continue;
    }
    const QPointF p = to_screen(pt);
    if(!pen_down) {
      path.moveTo(p);
      pen_down = true;
    } else {
      path.lineTo(p);
    }
  }
  painter.drawPath(path);

  painter.setPen(palette().text().color());
  painter.drawText(QRectF(4, area.top() - label_height / 2, area.left() - 12, label_height),
                   Qt::AlignRight | Qt::AlignVCenter, high_label);
  painter.drawText(QRectF(4, area.bottom() - label_height / 2, area.left() - 12, label_height),
                   Qt::AlignRight | Qt::AlignVCenter, low_label);
  painter.drawText(QRectF(area.left(), area.bottom() + 4, 80, 18), Qt::AlignLeft | Qt::AlignTop, "0");
  painter.drawText(QRectF(area.right() - 120, area.bottom() + 4, 120, 18), Qt::AlignRight | Qt::AlignTop,
                   QString("%1 px").arg(m_profile.total_length, 0, 'f', 1));
}

ProfileView::ProfileView(const QString &title,
                         std::shared_ptr<const std::vector<double>> decoded_values,
                         size_t rows, size_t cols,
                          std::vector<QPointF> control_points,
                          CurvePathMode path_mode,
                          QWidget *parent,
                          std::shared_ptr<void> storage_lease)
  : QWidget(parent, Qt::Window),
    m_storage_lease(std::move(storage_lease)),
    m_values(std::move(decoded_values)),
    m_rows(rows),
    m_cols(cols),
    m_controls(std::move(control_points)),
    m_path_mode(path_mode) {
  setObjectName("profileView");
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowTitle(title + tr(" - Curve Profile"));
  resize(680, 420);

  auto *layout = new QVBoxLayout(this);
  auto *tools = new QToolBar(this);
  layout->addWidget(tools);

  tools->addWidget(new QLabel(tr(" Interpolation: "), tools));
  m_interp = new QComboBox(tools);
  m_interp->addItem(tr("Bilinear"), int(ProfileInterpMode::Bilinear));
  m_interp->addItem(tr("Nearest"), int(ProfileInterpMode::Nearest));
  tools->addWidget(m_interp);
  connect(m_interp, &QComboBox::currentIndexChanged, this, [this] { recompute(); });

  m_export_action = tools->addAction(tr("Export CSV..."));
  m_export_action->setObjectName("exportProfileCsvAction");
  connect(m_export_action, &QAction::triggered, this, [this] { export_csv(); });

  m_summary = new QLabel(this);
  m_summary->setObjectName("profileSummary");
  m_summary->setWordWrap(true);
  m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_summary);

  m_canvas = new ProfileCanvas(this);
  layout->addWidget(m_canvas, 1);

  recompute();
}

void ProfileView::update_data(std::shared_ptr<const std::vector<double>> decoded_values,
                               size_t rows, size_t cols,
                               std::vector<QPointF> control_points,
                               CurvePathMode path_mode,
                               std::shared_ptr<void> storage_lease) {
  m_values = std::move(decoded_values);
  m_storage_lease = std::move(storage_lease);
  m_rows = rows;
  m_cols = cols;
  m_controls = std::move(control_points);
  m_path_mode = path_mode;
  recompute();
}

void ProfileView::recompute() {
  const auto interp = static_cast<ProfileInterpMode>(m_interp->currentData().toInt());
  const std::vector<QPointF> sampled = curve_sample_path(m_controls, m_path_mode, 0.5);
  static const std::vector<double> empty;
  const CurveProfileResult result = curve_extract_profile(m_values?*m_values:empty, m_rows, m_cols, sampled, interp);
  m_canvas->set_profile(result);
  m_export_action->setEnabled(result.valid && result.finite_count > 0);

  setProperty("pointCount", qulonglong(result.points.size()));
  setProperty("finiteCount", qulonglong(result.finite_count));
  setProperty("missingCount", qulonglong(result.missing_count));
  setProperty("minimum", result.minimum);
  setProperty("maximum", result.maximum);
  setProperty("mean", result.mean);

  if(!result.valid) {
    m_summary->setText(result.error);
    return;
  }
  m_summary->setText(tr("Points: %1 (%2 finite, %3 missing)   Length: %4 px   Min: %5   Max: %6   Mean: %7")
      .arg(result.points.size())
      .arg(result.finite_count)
      .arg(result.missing_count)
      .arg(result.total_length, 0, 'f', 1)
      .arg(result.minimum, 0, 'g', 9)
      .arg(result.maximum, 0, 'g', 9)
      .arg(result.mean, 0, 'g', 9));
}

void ProfileView::export_csv() {
  const QString path = QFileDialog::getSaveFileName(this, tr("Export Profile CSV"),
                                                    "profile.csv", tr("CSV Files (*.csv)"));
  if(path.isEmpty()) return;
  QFile file(path);
  if(!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
  QTextStream out(&file);
  out << "index,distance,row,col,value\n";
  const auto &pts = m_canvas->profile().points;
  for(size_t i = 0; i < pts.size(); ++i) {
    out << i << ',' << QString::number(pts[i].distance, 'g', 12) << ','
        << QString::number(pts[i].y, 'g', 12) << ','
        << QString::number(pts[i].x, 'g', 12) << ','
        << QString::number(pts[i].value, 'g', 15) << '\n';
  }
}
