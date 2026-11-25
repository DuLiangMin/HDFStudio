// 查找面板只拥有结果与文件系统任务，文件/ItemData继续归主窗口。
#pragma once
#include "browser_search.hpp"
#include <QDockWidget>
#include <QPointer>
class FileSearchThread;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QStandardItemModel;
class QTableView;
class QTabWidget;
class QTimer;

class SearchPanel : public QDockWidget {
  Q_OBJECT
public:
  explicit SearchPanel(QWidget *parent=nullptr);
  ~SearchPanel() override;
  void set_context(const QString &file, const QVector<BrowserSearch::Entry> &entries);
  void show_files();
  void show_variables();
signals:
  void open_file_requested(const QString &file);
  void variable_requested(const QPersistentModelIndex &index, int action);
private:
  void start_search();
  void stop_search();
  void refresh_variables();
  void activate_variable(int action);
  void open_selected_file();
  QString selected_file() const;
  QTabWidget *m_tabs;
  QLineEdit *m_directory, *m_filename, *m_query;
  QCheckBox *m_recursive, *m_exact;
  QLabel *m_file_status, *m_variable_status, *m_context;
  QPushButton *m_start, *m_stop;
  QStandardItemModel *m_files, *m_variables;
  QTableView *m_file_table, *m_variable_table;
  QVector<BrowserSearch::Entry> m_entries;
  QPointer<FileSearchThread> m_worker;
  QTimer *m_progress;
  quint64 m_generation=0;
  QString m_context_file;
};
