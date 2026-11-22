#include "analysis_table_view.hpp"
#include <QAction>
#include <QAccessible>
#include <QAccessibleWidget>
#include <QApplication>
#include <QClipboard>
#include <QFontMetricsF>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QStyleOptionHeader>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr qint64 copy_cell_limit = 1000000;
constexpr qsizetype copy_character_limit = 16 * 1024 * 1024; // UTF-16: at most 32 MiB.

int textPixelWidth(const QString &text, const QFontMetricsF &metrics) {
  qreal width = 0;
  // 多行表头逐行测量；同时包含斜体等字形越过排版起止点的部分。
  for(const auto &line : text.split('\n')) {
    const QRectF ink = metrics.boundingRect(line);
    const qreal left = qMin(qreal(0), ink.left());
    const qreal right = qMax(metrics.horizontalAdvance(line), ink.right());
    width = qMax(width, right - left);
  }
  return int(std::ceil(width));
}

QAccessibleInterface *gridAccessibility(const QString &, QObject *object) {
  auto *table = qobject_cast<AnalysisTableView*>(object);
  // The Windows bridge eagerly enumerates the default table's full child tree on Show.
  // Extreme arrays expose the widget and its keyboard controls instead of millions of cells.
  if(table && table->property("boundedGridAccessibility").toBool())
    return new QAccessibleWidget(table, QAccessible::Pane);
  return nullptr;
}

QString tsvCell(QString text) {
  if(text.contains('\t') || text.contains('\n') || text.contains('\r') || text.contains('"')) {
    text.replace("\"", "\"\"");
    return '"' + text + '"';
  }
  return text;
}
}

AnalysisTableView::AnalysisTableView(QWidget *parent) : QTableView(parent) {
  static const bool factory_installed = [] { QAccessible::installFactory(gridAccessibility); return true; }();
  Q_UNUSED(factory_installed);
  setSelectionBehavior(QAbstractItemView::SelectItems);
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setDragDropMode(QAbstractItemView::NoDragDrop);
  setWordWrap(false);
  setAlternatingRowColors(true);
  setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  horizontalHeader()->setStretchLastSection(false);
  horizontalHeader()->setMinimumSectionSize(24);
  horizontalHeader()->setDefaultSectionSize(80);
  verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  verticalHeader()->setDefaultSectionSize(qMax(24, fontMetrics().height() + 6));
  setToolTip(tr("Click or drag to select cells. Ctrl adds selections; Shift extends them. "
                "Click row/column headers to select a whole row/column. Ctrl+C copies selected values."));

  m_auto_fit = new QAction(tr("Auto-fit columns"), this);
  m_auto_fit->setObjectName("actionGridAutoFit");
  m_auto_fit->setCheckable(true);
  m_auto_fit->setChecked(true);
  m_auto_fit->setToolTip(tr("Fit visible columns to their headers and values. Turn off to keep manual widths."));
  connect(m_auto_fit, &QAction::toggled, this, [this](bool enabled) {
    if(enabled) scheduleFit();
    else m_fit_timer.stop();
  });
  m_copy = new QAction(tr("Copy selection"), this);
  m_copy->setObjectName("actionGridCopySelection");
  m_copy->setShortcut(QKeySequence::Copy);
  m_copy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  m_copy->setEnabled(false);
  connect(m_copy, &QAction::triggered, this, [this] {
    QString error;
    const QString text = selectionText(&error);
    if(!error.isEmpty()) { emit copyFailed(error); return; }
    if(selectionModel() && selectionModel()->hasSelection()) QApplication::clipboard()->setText(text);
  });
  addAction(m_auto_fit);
  addAction(m_copy);
  setContextMenuPolicy(Qt::ActionsContextMenu);
  m_fit_timer.setSingleShot(true);
  m_fit_timer.setInterval(20);
  connect(&m_fit_timer, &QTimer::timeout, this, &AnalysisTableView::fitVisibleColumns);
  connect(horizontalHeader(), &QHeaderView::sectionHandleDoubleClicked, this, [this](int column) {
    horizontalHeader()->resizeSection(column, contentWidth(column));
  });
}

void AnalysisTableView::setModel(QAbstractItemModel *source) {
  for(const auto &connection : m_model_connections) disconnect(connection);
  m_model_connections.clear();
  const bool bounded = source && qint64(source->rowCount()) * source->columnCount() > 1000000;
  if(bounded != property("boundedGridAccessibility").toBool()) {
    if(auto *accessible = QAccessible::queryAccessibleInterface(this))
      QAccessible::deleteAccessibleInterface(QAccessible::uniqueId(accessible));
    setProperty("boundedGridAccessibility", bounded);
  }
  setAccessibleDescription(bounded ? tr("Large scientific grid. Use arrow keys, Shift and Ctrl to select cells; Ctrl+C copies the selection. Per-cell accessibility enumeration is limited for arrays over one million cells.") : QString());
  QTableView::setModel(source);
  if(source) {
    m_model_connections.append(connect(source, &QAbstractItemModel::dataChanged, this, [this] { scheduleFit(); }));
    m_model_connections.append(connect(source, &QAbstractItemModel::modelReset, this, [this] { scheduleFit(); }));
    m_model_connections.append(connect(source, &QAbstractItemModel::layoutChanged, this, [this] { scheduleFit(); }));
    m_model_connections.append(connect(source, &QAbstractItemModel::columnsInserted, this, [this] { scheduleFit(); }));
    m_model_connections.append(connect(source, &QAbstractItemModel::columnsRemoved, this, [this] { scheduleFit(); }));
    m_model_connections.append(connect(source, &QAbstractItemModel::rowsInserted, this, [this] { scheduleFit(); }));
    m_model_connections.append(connect(source, &QAbstractItemModel::rowsRemoved, this, [this] { scheduleFit(); }));
    m_model_connections.append(connect(source, &QAbstractItemModel::headerDataChanged, this, [this] { scheduleFit(); }));
    m_model_connections.append(connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
      m_copy->setEnabled(selectionModel()->hasSelection());
      emit selectionSummaryChanged(selectionSummary());
    }));
  }
  m_copy->setEnabled(false);
  emit selectionSummaryChanged(selectionSummary());
  scheduleFit();
}

void AnalysisTableView::scheduleFit() {
  if(m_auto_fit && m_auto_fit->isChecked() && !m_fit_timer.isActive()) m_fit_timer.start();
}

int AnalysisTableView::contentWidth(int column) const {
  if(!model() || column < 0 || column >= model()->columnCount()) return 80;
  const QFontMetricsF metrics(font(), viewport());
  const QVariant header_font_value = model()->headerData(column, Qt::Horizontal, Qt::FontRole);
  QFont header_font = header_font_value.isValid() ? qvariant_cast<QFont>(header_font_value) : horizontalHeader()->font();
  // Qt高亮已选列时会加粗表头；预留该字形宽度，避免选中后截字或列宽跳动。
  if(horizontalHeader()->highlightSections()) header_font.setBold(true);
  QStyleOptionHeader header_option;
  header_option.initFrom(horizontalHeader());
  header_option.fontMetrics = QFontMetrics(header_font, horizontalHeader());
  const QString header = model()->headerData(column, Qt::Horizontal).toString();
  header_option.orientation = Qt::Horizontal;
  header_option.section = column;
  int header_width = 0;
  for(const auto &line : header.split('\n')) {
    const int line_width = estimateTextWidth(line, header_font, horizontalHeader());
    if(line_width >= header_width) { header_width = line_width; header_option.text = line; }
  }
  if(horizontalHeader()->isSortIndicatorShown() && horizontalHeader()->sortIndicatorSection() == column)
    header_option.sortIndicator = horizontalHeader()->sortIndicatorOrder() == Qt::AscendingOrder
        ? QStyleOptionHeader::SortDown : QStyleOptionHeader::SortUp;
  const QSize header_text(header_width,
                         header_option.fontMetrics.height() * (header.count('\n') + 1));
  const int header_margin = 2 * horizontalHeader()->style()->pixelMetric(QStyle::PM_HeaderMargin,
      &header_option, horizontalHeader()) + 4;
  int width = qMax(header_width + header_margin, horizontalHeader()->style()->sizeFromContents(QStyle::CT_HeaderSection,
      &header_option, header_text, horizontalHeader()).width());
  // 与 Qt 单元格文本留白一致，并为网格线与像素取整留少量余量。
  const int cell_margin = 2 * (style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, this) + 1)
                          + (showGrid() ? 1 : 0) + 2;
  auto measure = [&](int row) {
    const QModelIndex index = model()->index(row, column);
    const QString text = model()->data(index, Qt::DisplayRole).toString();
    const QVariant cell_font = model()->data(index, Qt::FontRole);
    const int text_width = cell_font.isValid()
        ? estimateTextWidth(text, qvariant_cast<QFont>(cell_font), viewport())
        : textPixelWidth(text, metrics);
    width = qMax(width, text_width + cell_margin);
  };
  const int rows = model()->rowCount();
  // Visible rows plus evenly distributed source rows: bounded even for million-row models.
  const int visible_first = qMax(0, rowAt(0));
  const int visible_last = rowAt(viewport()->height() - 1);
  const int visible_end = qMin(rows, visible_last < 0 ? visible_first + 128 : visible_last + 1);
  for(int row = visible_first; row < visible_end && row < visible_first + 128; ++row)
    measure(row);
  const int samples = qMin(rows, 128);
  for(int sample = 0; sample < samples; ++sample) {
    const int row = samples == 1 ? 0 : int(qint64(sample) * (rows - 1) / (samples - 1));
    measure(row);
  }
  return qBound(24, width, 320);
}

int AnalysisTableView::estimateTextWidth(const QString &text, const QFont &font,
                                        const QPaintDevice *device) {
  return textPixelWidth(text, QFontMetricsF(font, device));
}

void AnalysisTableView::fitVisibleColumns() {
  if(!m_auto_fit->isChecked() || !model() || !isVisible()) return;
  // Visit only the viewport and a small look-ahead; scrolling fits newly exposed columns.
  int first = columnAt(0);
  if(first < 0) first = 0;
  const int max_columns = qMin(model()->columnCount(), first + viewport()->width() / horizontalHeader()->minimumSectionSize() + 3);
  for(int column = first; column < max_columns; ++column) {
    if(isColumnHidden(column)) continue;
    horizontalHeader()->resizeSection(column, contentWidth(column));
    if(columnViewportPosition(column) > viewport()->width() + 320) break;
  }
}

QString AnalysisTableView::selectionSummary() const {
  qint64 count = 0;
  if(selectionModel())
    for(const auto &range : selectionModel()->selection()) count += qint64(range.width()) * range.height();
  return tr("Selected: %1 cells").arg(count);
}

void AnalysisTableView::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) {
  // Qt 6.8 QTableView expands selected.indexes() just to obtain its first accessibility item.
  // Keep the actual selection as ranges, and notify with its first cell only. Repaint the
  // viewport rather than constructing per-cell lists for a whole scientific array.
  QItemSelection selected_head, deselected_head;
  if(!selected.isEmpty()) selected_head.select(selected.first().topLeft(), selected.first().topLeft());
  if(!deselected.isEmpty()) deselected_head.select(deselected.first().topLeft(), deselected.first().topLeft());
  qint64 changed_cells = 0;
  for(const auto &range : selected) changed_cells += qint64(range.width()) * range.height();
  for(const auto &range : deselected) changed_cells += qint64(range.width()) * range.height();
  if(selectionModel())
    for(const auto &range : selectionModel()->selection()) changed_cells += qint64(range.width()) * range.height();
  // Windows' accessibility bridge may query the entire selection in response to an add/remove
  // event as well. For bulk ranges use the abstract repaint path, preserving native selection.
  if(changed_cells > 4096 || property("boundedGridAccessibility").toBool()) QAbstractItemView::selectionChanged(selected_head, deselected_head);
  else QTableView::selectionChanged(selected_head, deselected_head);
  viewport()->update();
}

void AnalysisTableView::currentChanged(const QModelIndex &current, const QModelIndex &previous) {
  if(property("boundedGridAccessibility").toBool()) QAbstractItemView::currentChanged(current, previous);
  else QTableView::currentChanged(current, previous);
}

QString AnalysisTableView::selectionText(QString *error) const {
  if(error) error->clear();
  if(!model() || !selectionModel() || !selectionModel()->hasSelection()) return {};
  int first_row = std::numeric_limits<int>::max(), first_column = first_row;
  int last_row = -1, last_column = -1;
  const auto ranges = selectionModel()->selection();
  for(const auto &range : ranges) {
    first_row = qMin(first_row, range.top()); first_column = qMin(first_column, range.left());
    last_row = qMax(last_row, range.bottom()); last_column = qMax(last_column, range.right());
  }
  const qint64 cells = qint64(last_row - first_row + 1) * (last_column - first_column + 1);
  if(cells > copy_cell_limit) {
    if(error) *error = tr("Selection spans more than 1,000,000 cells. Please select a smaller range to copy.");
    return {};
  }
  QString text;
  for(int row = first_row; row <= last_row; ++row) {
    if(row != first_row) text += '\n';
    for(int column = first_column; column <= last_column; ++column) {
      if(column != first_column) text += '\t';
      const QModelIndex index = model()->index(row, column);
      if(selectionModel()->isSelected(index)) text += tsvCell(model()->data(index).toString());
      if(text.size() > copy_character_limit) {
        if(error) *error = tr("Selection text exceeds 32 MiB. Please select a smaller range to copy.");
        return {};
      }
    }
  }
  return text;
}

void AnalysisTableView::showEvent(QShowEvent *event) { QTableView::showEvent(event); scheduleFit(); }
void AnalysisTableView::resizeEvent(QResizeEvent *event) { QTableView::resizeEvent(event); scheduleFit(); }
void AnalysisTableView::scrollContentsBy(int dx, int dy) { QTableView::scrollContentsBy(dx, dy); scheduleFit(); }
void AnalysisTableView::changeEvent(QEvent *event) {
  QTableView::changeEvent(event);
  if(event->type() == QEvent::FontChange)
    verticalHeader()->setDefaultSectionSize(qMax(24, fontMetrics().height() + 6));
  if(event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) scheduleFit();
}
