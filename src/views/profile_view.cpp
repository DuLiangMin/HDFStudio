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

  const QRectF area = QRectF(rect()).adjusted(60, 20, -20, -34);
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
  painter.drawText(QRectF(4, area.top() - 8, 52, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(max_v, 'g', 6));
  painter.drawText(QRectF(4, area.bottom() - 8, 52, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(min_v, 'g', 6));
  painter.drawText(QRectF(area.left(), area.bottom() + 4, 80, 18), Qt::AlignLeft | Qt::AlignTop, "0");
  painter.drawText(QRectF(area.right() - 120, area.bottom() + 4, 120, 18), Qt::AlignRight | Qt::AlignTop,
                   QString("%1 px").arg(m_profile.total_length, 0, 'f', 1));
}

ProfileView::ProfileView(const QString &title,
                         std::vector<double> decoded_values,
                         size_t rows, size_t cols,
                         std::vector<QPointF> control_points,
                         CurvePathMode path_mode,
                         QWidget *parent)
  : QWidget(parent, Qt::Window),
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

  auto *export_action = tools->addAction(tr("Export CSV..."));
  export_action->setObjectName("exportProfileCsvAction");
  connect(export_action, &QAction::triggered, this, [this] { export_csv(); });

  m_summary = new QLabel(this);
  m_summary->setObjectName("profileSummary");
  m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_summary);

  m_canvas = new ProfileCanvas(this);
  layout->addWidget(m_canvas, 1);

  recompute();
}

void ProfileView::update_data(std::vector<double> decoded_values,
                              size_t rows, size_t cols,
                              std::vector<QPointF> control_points,
                              CurvePathMode path_mode) {
  m_values = std::move(decoded_values);
  m_rows = rows;
  m_cols = cols;
  m_controls = std::move(control_points);
  m_path_mode = path_mode;
  recompute();
}

void ProfileView::recompute() {
  const auto interp = static_cast<ProfileInterpMode>(m_interp->currentData().toInt());
  const std::vector<QPointF> sampled = curve_sample_path(m_controls, m_path_mode, 0.5);
  const CurveProfileResult result = curve_extract_profile(m_values, m_rows, m_cols, sampled, interp);
  m_canvas->set_profile(result);

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
