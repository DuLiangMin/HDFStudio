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
#include "layer_navigation.hpp"

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
  // Plain-text Properties report of a tree item: file and object path first, then the same
  // detail lines as `hdf_cli props`; shown in the Properties dialog and copied by Copy All.
  QString item_properties_text(QTreeWidgetItem *item) const;

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
  // Child widgets are deleted after this destructor; the MDI area still emits subWindowActivated
  // while its views close, so its connections to this window are cut here first.
  ~MainWindow() override;
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
  void go_to_layer();
  // keeps the Layer toolbar in step with the current view (activation or its own selectors)
  void update_layer_controls();
  void watch_current_view(QMdiSubWindow *sub);
  // Window menu: Cascade, Tile, Close All Views, then one numbered entry per open view
  void update_window_menu();
  // Windows MDI cascade: creation order, each view 2/3 of the area, offset by one title bar;
  // QMdiArea::cascadeSubWindows() sizes views from their size hints and squashes the image view
  void cascade_windows();

public:
  // Steps the current view like the old HDF Explorer toolbar buttons (no-op without layers).
  void step_layer(LayerStep step);
  // The view whose layers the Layer toolbar and menu act on, or nullptr.
  LayerNavigation *current_layer_view() const;

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
  QAction *m_action_cascade;
  QAction *m_action_close_all;

  ///////////////////////////////////////////////////////////////////////////////////////
  //layer navigation (old HDF Explorer main-toolbar buttons 32776-32779 and 32831)
  ///////////////////////////////////////////////////////////////////////////////////////

  QMenu *m_menu_layer;
  QAction *m_action_layer_next;
  QAction *m_action_layer_previous;
  QAction *m_action_layer_last;
  QAction *m_action_layer_first;
  QAction *m_action_layer_goto;
  QComboBox *m_layer_axis_combo;   // which leading dimension is stepped (time, lev, ...)
  QComboBox *m_layer_index_combo;  // selected layer of that dimension, 1-based text
  QLabel *m_layer_label;           // "Layer n of N", as in the old status bar
  QPointer<QWidget> m_layer_view;  // view whose layer_changed() is connected
  // what the index combo was filled for; it is refilled only when view, axis or length change
  const void *m_layer_combo_view = nullptr;
  int m_layer_combo_axis = -1;
  int m_layer_combo_count = -1;

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

class ChildWindow : public QMainWindow, public LayerNavigation
{
  Q_OBJECT
public:
  ChildWindow(QWidget *parent, ItemData *item_data);
  std::vector<int> m_layer;  // current selected layer of a dimension > 2 

  // LayerNavigation: the main window's Layer toolbar/menu drive this grid through its own combos
  int layer_axis_count() const override;
  QString layer_axis_name(int axis) const override;
  int layer_count(int axis) const override;
  int layer_index(int axis) const override;
  void set_layer_index(int axis, int index) override;

signals:
  void layer_changed();

  private slots:
  void previous_layer(int);
  void next_layer(int);
  void combo_layer(int);

private:
  QToolBar *m_tool_bar;
  std::vector<QComboBox *> m_vec_combo;
  QStringList m_axis_names;  // names of the leading dimensions, e.g. time, lev

protected:
  TableModel *m_model;
  hdf_dataset_t *m_dataset; // HDF variable to display (convenience pointer to data in ItemData)
};

#endif
