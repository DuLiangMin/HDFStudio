// Reusable read-only scientific grid: bounded content sizing and native cell selection.
// Input: a QAbstractItemModel; output: column widths and selected DisplayRole values as TSV.
#pragma once

#include <QTableView>
#include <QTimer>
#include <QVector>

class QAction;
class QKeyEvent;

class AnalysisTableView : public QTableView {
  Q_OBJECT
public:
  explicit AnalysisTableView(QWidget *parent = nullptr);
  void setModel(QAbstractItemModel *model) override;
  QAction *autoFitAction() const { return m_auto_fit; }
  QAction *copyAction() const { return m_copy; }
  QString selectionText(QString *error = nullptr) const;
  QString selectionSummary() const;
  // 输入实际显示文本、字体及绘制设备；输出不含单元格边距的逻辑像素宽度。
  static int estimateTextWidth(const QString &text, const QFont &font,
                               const QPaintDevice *device = nullptr);
  void fitVisibleColumns();

signals:
  void selectionSummaryChanged(const QString &summary);
  void copyFailed(const QString &message);

protected:
  void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
  void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void scrollContentsBy(int dx, int dy) override;
  void changeEvent(QEvent *event) override;

private:
  void scheduleFit();
  int contentWidth(int column) const;
  QAction *m_auto_fit = nullptr;
  QAction *m_copy = nullptr;
  QTimer m_fit_timer;
  QVector<QMetaObject::Connection> m_model_connections;
};
