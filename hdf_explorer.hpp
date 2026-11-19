#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#if QT_VERSION >= 0x050000
#include <QtWidgets>
#endif
#include <QtGui>
#include <QIcon>
#include <QMdiArea>
#include <string>
#include <vector>
#include "hdf5.h"
#include "visit.hpp"

class MainWindow;
class ItemData;
class hdf_dataset_t;
class TableModel;

// Startup waiting box, defined in hdf_explorer.cpp and shared with the regression test.
QWidget *create_startup_splash();

/////////////////////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget
/////////////////////////////////////////////////////////////////////////////////////////////////////

class FileTreeWidget : public QTreeWidget
{
  Q_OBJECT
public:
  FileTreeWidget(QWidget *parent = 0);
  ~FileTreeWidget();
protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  private slots:
  void show_context_menu(const QPoint &);
  void add_grid();
  void show_item_properties();
  void export_item_as_text();
  void export_item_as_image();

public:
  void set_main_window(MainWindow *p)
  {
    m_main_window = p;
  }

private:
  MainWindow *m_main_window;
  void load_item(QTreeWidgetItem *);
  void load_item_attribute(QTreeWidgetItem *);
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow
/////////////////////////////////////////////////////////////////////////////////////////////////////

class MainWindow : public QMainWindow
{
  Q_OBJECT
  friend class FileTreeWidget;
public:
  MainWindow();
  void add_table(ItemData *item_data);
  void add_image(ItemData *item_data);
  void export_image(ItemData *item_data, QWidget *parent);
  int read_file(QString file_name);
  // Closes the file that owns `item` (any node below a top-level tree root): its views are
  // destroyed first, then the tree nodes and their ItemData, then the info panels are refreshed.
  void close_file_of(QTreeWidgetItem *item);
  void close_all_files();
  bool accepts_file_drop(const QMimeData *mime_data) const;
  void handle_file_drop(const QMimeData *mime_data, QDropEvent *event);
protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

  private slots:
  void open_recent_file();
  void open_file();
  void close_current_file();
  void about();
  void show_cf_diagnostics();
  void update_file_info(const QString &file_name);
  void show_time_values();
  void load_report_tab(int index);

private:

  ///////////////////////////////////////////////////////////////////////////////////////
  //widgets
  ///////////////////////////////////////////////////////////////////////////////////////

  QMenu *m_menu_file;
  QMenu *m_menu_help;
  QMenu *m_menu_windows;
  QToolBar *m_tool_bar;
  QMdiArea *m_mdi_area;
  FileTreeWidget *m_tree;
  QDockWidget *m_tree_dock;
  QSplitter *m_main_splitter;
  QTabWidget *m_info_tabs;
  QPlainTextEdit *m_file_info_text;
  QPlainTextEdit *m_netcdf4_text;
  QPlainTextEdit *m_cf_check_text;

  ///////////////////////////////////////////////////////////////////////////////////////
  //actions
  ///////////////////////////////////////////////////////////////////////////////////////

  QAction *m_action_open;
  QAction *m_action_close_file;
  QAction *m_action_close_all_files;
  QAction *m_action_exit;
  QAction *m_action_about;
  QAction *m_action_tile;
  QAction *m_action_close_all;

  ///////////////////////////////////////////////////////////////////////////////////////
  //icons
  ///////////////////////////////////////////////////////////////////////////////////////

  QIcon m_icon_main;
  QIcon m_icon_group;
  QIcon m_icon_dataset;
  QIcon m_icon_attribute;
  QIcon m_icon_image_indexed;
  QIcon m_icon_image_true;

  ///////////////////////////////////////////////////////////////////////////////////////
  //recent files
  ///////////////////////////////////////////////////////////////////////////////////////

  enum { max_recent_files = 5 };
  QAction *m_action_recent_file[max_recent_files];
  QAction *m_action_separator_recent;
  QStringList m_sl_recent_files;
  QString m_str_current_file;
  void update_recent_file_actions();
  QTreeWidgetItem *add_classic_tree(const QString &file_name, const std::string &stored_name,
                                    const QString &display_name);
  void update_close_actions();
  // A view remembers which tree item produced it, so closing a file can find exactly its views.
  void tag_view(QMdiSubWindow *sub, const ItemData *source);
  void set_current_file(const QString &file_name);
  void closeEvent(QCloseEvent *eve);

  ///////////////////////////////////////////////////////////////////////////////////////
  //HDF5
  ///////////////////////////////////////////////////////////////////////////////////////

private:

  h5visit_t m_visit;

  int iterate(const std::string& file_name, const std::string& grp_name, const hid_t loc_id, QTreeWidgetItem *tree_item_parent);
  int get_attributes(const std::string& file_name, const std::string& grp_name, const hid_t loc_id, QTreeWidgetItem *tree_item_parent);

  //find object in list 
  H5O_info_added_t* find_object(haddr_t addr);
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ChildWindow
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ChildWindow : public QMainWindow
{
  Q_OBJECT
public:
  ChildWindow(QWidget *parent, ItemData *item_data);
  std::vector<int> m_layer;  // current selected layer of a dimension > 2 

  private slots:
  void previous_layer(int);
  void next_layer(int);
  void combo_layer(int);

private:
  QToolBar *m_tool_bar;
  std::vector<QComboBox *> m_vec_combo;

protected:
  TableModel *m_model;
  hdf_dataset_t *m_dataset; // HDF variable to display (convenience pointer to data in ItemData)
};

#endif
