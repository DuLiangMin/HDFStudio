//Copyright (C) 2016 Pedro Vicente
//GNU General Public License (GPL) Version 3 described in the LICENSE file 

#include <QApplication>
#include <QFontDatabase>
#include <QMetaType>
#include <QtGui>
#include <QIcon>
#include <QMdiArea>
#include <QtDebug>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QSaveFile>
#include <QPainter>
#include <string>
#include <cassert>
#include <cmath>
#include <vector>
#include <algorithm>
#include "hdf_explorer.hpp"
#include "raster_view.hpp"
#include "browser_item.hpp"
#include "legacy_grid_data.hpp"
#include "legacy_grid.hpp"
#include "hdf_filters.hpp"
#include "texture_entropy_view.hpp"
#include "hovmoller_view.hpp"
#include "compare_panel.hpp"
#include "layer_playback.hpp"
#include "histogram_view.hpp"
#include "nc_reader.hpp"
#include "nc_classic.hpp"
#include "properties_inspector.hpp"
#include "text_export.hpp"
#include "vector_view.hpp"
#include "mesh_view.hpp"
#include "grib/grib_data.hpp"
#include "data_read_controller.hpp"
#include "grib_table_view.hpp"
#include "slice_io.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static const char app_name[] = "HDFStudio";

// true when HDFStudio runs with an elevated (administrator) token
bool process_is_elevated()
{
#ifdef _WIN32
  HANDLE token = nullptr;
  if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation{};
  DWORD size = 0;
  const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
  CloseHandle(token);
  return ok && elevation.TokenIsElevated != 0;
#else
  return false;
#endif
}

static QString item_file(const ItemData *item)
{
  return item->m_grib ? item->m_source_path : QString::fromLocal8Bit(item->m_file_name.c_str());
}

class GribTreeSession : public QObject {
public:
  explicit GribTreeSession(QString path) : m_path(std::move(path)) {}
  ~GribTreeSession() override { GribData::release(m_path); }
private:
  QString m_path;
};

static QIcon dataset_icon(size_t rank)
{
  QPixmap pixmap(20, 20);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(42, 71, 82), 1.2));
  painter.setBrush(QColor(61, 153, 164));

  if(rank == 1) {
    painter.drawRoundedRect(QRectF(7, 2, 6, 16), 1.2, 1.2);
  } else if(rank == 2) {
    painter.drawRect(QRectF(3, 3, 14, 14));
  } else {
    const QPointF top(10, 2), left(3, 6), right(17, 6);
    const QPointF middle(10, 10), bottomLeft(3, 14), bottomRight(17, 14), bottom(10, 18);
    painter.drawPolygon(QPolygonF() << top << right << middle << left);
    painter.drawPolygon(QPolygonF() << left << middle << bottom << bottomLeft);
    painter.drawPolygon(QPolygonF() << middle << right << bottomRight << bottom);
    painter.setBrush(QColor(81, 176, 183));
    painter.drawPolygon(QPolygonF() << top << middle << bottomLeft << left);
  }
  painter.end();
  return QIcon(pixmap);
}

// Layer buttons in the shapes of the old HDF Explorer toolbar (bitmap 128, images 6-9):
// up = next, down = previous, double up = last, double down = first. Drawn here rather than
// copied from the old executable; 32 px so they stay sharp on high-DPI screens.
static QIcon layer_icon(LayerStep step)
{
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(32, 32, 32));
  const bool up = step == LayerStep::Next || step == LayerStep::Last;
  const bool twice = step == LayerStep::Last || step == LayerStep::First;
  // one triangle centred, or two stacked triangles
  QList<double> rows;
  if(twice) rows << 3 << 17; else rows << 9;
  for(double top : rows)
  {
    const double height = twice ? 12 : 14;
    QPolygonF triangle;
    if(up) triangle << QPointF(16, top) << QPointF(29, top + height) << QPointF(3, top + height);
    else   triangle << QPointF(3, top) << QPointF(29, top) << QPointF(16, top + height);
    painter.drawPolygon(triangle);
  }
  painter.end();
  return QIcon(pixmap);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//get_item_data
/////////////////////////////////////////////////////////////////////////////////////////////////////

ItemData* get_item_data(QTreeWidgetItem *item)
{
  QVariant data = item->data(0, Qt::UserRole);
  ItemData *item_data = data.value<ItemData*>();
  return item_data;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//NetCDF-3 (CDF-1/2/5) support: type mapping and buffer loading through netCDF-C
//The tree, grid and views share ItemData/hdf_dataset_t, so a classic type is described with the same
//HDF5 class/size/sign fields the HDF5 path fills.
/////////////////////////////////////////////////////////////////////////////////////////////////////

static void classic_describe_type(nc_type type, size_t &size, H5T_sign_t &sign, H5T_class_t &cls)
{
  const NcTypeInfo info = nc_type_info(type);
  size = info.size;
  sign = info.is_signed ? H5T_SGN_2 : H5T_SGN_NONE;
  switch(info.kind)
  {
  case NcTypeInfo::Integer: cls = H5T_INTEGER; break;
  case NcTypeInfo::Float:   cls = H5T_FLOAT;   break;
  case NcTypeInfo::Text:    cls = H5T_STRING;  break;
  default:                  cls = H5T_NO_CLASS; break;
  }
}

///////////////////////////////////////////////////////////////////////////////////////
//name_type_t
//struct to get name and type of an object
///////////////////////////////////////////////////////////////////////////////////////

struct name_type_t
{
  char *name;
  int type;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::MainWindow
/////////////////////////////////////////////////////////////////////////////////////////////////////

MainWindow::MainWindow()
{
  setAcceptDrops(true);
  ///////////////////////////////////////////////////////////////////////////////////////
  //mdi area
  ///////////////////////////////////////////////////////////////////////////////////////

  m_mdi_area = new QMdiArea;
  m_mdi_area->setAcceptDrops(true);
  m_mdi_area->viewport()->setAcceptDrops(true);
  //file drops anywhere in this window: see MainWindow::eventFilter
  qApp->installEventFilter(this);

  ///////////////////////////////////////////////////////////////////////////////////////
  //bottom information tabs
  //Splitter page rather than a dock: the handle is a wide, always-visible resize grip,
  //so the panel height follows a normal drag instead of depending on dock layout rules.
  //Tab 1 is the file summary, tab 2 the netCDF4-style structure, tab 3 the CF verdict.
  ///////////////////////////////////////////////////////////////////////////////////////

  auto *info_panel=new QWidget(this);
  info_panel->setObjectName("fileInfoPanel");
  auto *info_panel_layout=new QVBoxLayout(info_panel);
  info_panel_layout->setContentsMargins(4,4,4,4);
  info_panel_layout->setSpacing(0);
  m_info_tabs=new QTabWidget(info_panel);
  m_info_tabs->setObjectName("infoTabs");
  m_info_tabs->setDocumentMode(true);

  auto make_info_page=[this](const char *pageName,const char *textName,QPlainTextEdit *&text) {
    auto *page=new QWidget(m_info_tabs);
    page->setObjectName(QString::fromLatin1(pageName));
    auto *layout=new QVBoxLayout(page);
    layout->setContentsMargins(8,4,8,4);
    text=new QPlainTextEdit(page);
    text->setObjectName(QString::fromLatin1(textName));
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setMinimumHeight(0);
    layout->addWidget(text);
    return page;
  };

  m_info_tabs->addTab(make_info_page("fileInfoPage","fileInfoText",m_file_info_text),tr("File Info"));
  m_info_tabs->addTab(make_info_page("netcdf4Page","netcdf4Text",m_netcdf4_text),tr("By NetCDF4"));
  m_info_tabs->addTab(make_info_page("cfCheckPage","cfCheckText",m_cf_check_text),tr("CF Check"));
  m_file_info_text->setPlainText(tr("No file opened."));
  m_netcdf4_text->setPlainText(tr("No file opened."));
  m_cf_check_text->setPlainText(tr("No file opened."));
  info_panel_layout->addWidget(m_info_tabs);
  connect(m_info_tabs,&QTabWidget::currentChanged,this,&MainWindow::load_report_tab);

  m_main_splitter=new QSplitter(Qt::Vertical,this);
  m_main_splitter->setObjectName("mainSplitter");
  m_main_splitter->setChildrenCollapsible(false);
  m_main_splitter->setHandleWidth(8);
  m_main_splitter->setOpaqueResize(true);
  m_main_splitter->addWidget(m_mdi_area);
  m_main_splitter->addWidget(info_panel);
  m_main_splitter->setStretchFactor(0,1);
  m_main_splitter->setStretchFactor(1,0);
  m_main_splitter->setSizes({560,190});
  setCentralWidget(m_main_splitter);

  setWindowTitle(tr(app_name));

  ///////////////////////////////////////////////////////////////////////////////////////
  //status bar
  ///////////////////////////////////////////////////////////////////////////////////////

  //an elevated process cannot receive drag and drop from a normal Explorer window (Windows UIPI)
  if(process_is_elevated())
    statusBar()->showMessage(tr("Running as administrator: Windows blocks dragging files here from Explorer. "
                                "Start HDFStudio normally to use drag and drop, or use File > Open."));
  else
    statusBar()->showMessage(tr("Ready"));

  ///////////////////////////////////////////////////////////////////////////////////////
  //dock for tree
  ///////////////////////////////////////////////////////////////////////////////////////

  m_tree_dock = new QDockWidget(this);
  m_tree_dock->setWindowTitle(tr("Datasets"));
  m_tree_dock->setFeatures(QDockWidget::DockWidgetMovable);

  ///////////////////////////////////////////////////////////////////////////////////////
  //browser tree
  ///////////////////////////////////////////////////////////////////////////////////////

  m_tree = new FileTreeWidget();
  m_tree->setAcceptDrops(true);
  m_tree->viewport()->setAcceptDrops(true);
  m_tree->setDragDropMode(QAbstractItemView::DropOnly);
  m_tree->setDropIndicatorShown(true);
  m_tree->setHeaderHidden(1);
  m_tree->set_main_window(this);
  QStringList str_style = QStyleFactory::keys();
  qDebug() << str_style;
  for(int i = 0; i < str_style.size(); ++i)
  {
    if("Windows" == str_style[i])
    {
      QStyle *style = QStyleFactory::create(str_style[i]);
      m_tree->setStyle(style);
    }
  }
  //add dock
  m_tree_dock->setWidget(m_tree);
  addDockWidget(Qt::LeftDockWidgetArea, m_tree_dock);

  ///////////////////////////////////////////////////////////////////////////////////////
  //actions
  ///////////////////////////////////////////////////////////////////////////////////////

  ///////////////////////////////////////////////////////////////////////////////////////
  //open
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_open = new QAction(tr("&Open..."), this);
  m_action_open->setIcon(QIcon(":/images/open.png"));
  m_action_open->setShortcut(QKeySequence::Open);
  m_action_open->setStatusTip(tr("Open a file"));
  connect(m_action_open, SIGNAL(triggered()), this, SLOT(open_file()));

  ///////////////////////////////////////////////////////////////////////////////////////
  //close file (the file of the selected tree node) and close all files
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_close_file = new QAction(tr("&Close File"), this);
  m_action_close_file->setObjectName("actionCloseFile");
  // Qt's standard Close key: Ctrl+F4 first and Ctrl+W as the alternative on Windows.
  m_action_close_file->setShortcuts(QKeySequence::keyBindings(QKeySequence::Close));
  m_action_close_file->setStatusTip(tr("Close the file of the selected tree node and its views"));
  connect(m_action_close_file, &QAction::triggered, this, &MainWindow::close_current_file);

  m_action_close_all_files = new QAction(tr("Close All F&iles"), this);
  m_action_close_all_files->setObjectName("actionCloseAllFiles");
  m_action_close_all_files->setStatusTip(tr("Close every open file and all views"));
  connect(m_action_close_all_files, &QAction::triggered, this, &MainWindow::close_all_files);

  ///////////////////////////////////////////////////////////////////////////////////////
  //exit
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_exit = new QAction(tr("E&xit"), this);
  m_action_exit->setShortcut(tr("Ctrl+Q"));
  m_action_exit->setStatusTip(tr("Exit the application"));
  connect(m_action_exit, SIGNAL(triggered()), this, SLOT(close()));

  ///////////////////////////////////////////////////////////////////////////////////////
  //windows
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_close_all = new QAction(tr("Close All &Views"), this);
  m_action_close_all->setStatusTip(tr("Close all the view windows (files stay open)"));
  connect(m_action_close_all, SIGNAL(triggered()), m_mdi_area, SLOT(closeAllSubWindows()));

  m_action_tile = new QAction(tr("&Tile"), this);
  m_action_tile->setObjectName("actionTile");
  m_action_tile->setStatusTip(tr("Tile the windows"));
  connect(m_action_tile, SIGNAL(triggered()), m_mdi_area, SLOT(tileSubWindows()));

  //old HDF Explorer Window menu: Cascade, Tile, Arrange Icons and the list of open windows
  m_action_cascade = new QAction(tr("&Cascade"), this);
  m_action_cascade->setObjectName("actionCascade");
  m_action_cascade->setStatusTip(tr("Arrange the windows one behind the other, in order"));
  connect(m_action_cascade, &QAction::triggered, this, &MainWindow::cascade_windows);

  ///////////////////////////////////////////////////////////////////////////////////////
  //about
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_about = new QAction(tr("&About"), this);
  m_action_about->setStatusTip(tr("Show the application's About box"));
  connect(m_action_about, SIGNAL(triggered()), this, SLOT(about()));

  ///////////////////////////////////////////////////////////////////////////////////////
  //layer navigation: the old HDF Explorer main toolbar had up/down/double-up/double-down
  //buttons (commands 32776-32779) acting on the active view, and a go-to-layer dialog (32831)
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_layer_next = new QAction(layer_icon(LayerStep::Next), tr("&Next Layer"), this);
  m_action_layer_next->setObjectName("actionLayerNext");
  m_action_layer_next->setShortcut(QKeySequence(tr("Alt+Right")));
  m_action_layer_next->setStatusTip(tr("Show the next layer of the selected dimension"));
  connect(m_action_layer_next, &QAction::triggered, this, [this] { step_layer(LayerStep::Next); });

  m_action_layer_previous = new QAction(layer_icon(LayerStep::Previous), tr("&Previous Layer"), this);
  m_action_layer_previous->setObjectName("actionLayerPrevious");
  m_action_layer_previous->setShortcut(QKeySequence(tr("Alt+Left")));
  m_action_layer_previous->setStatusTip(tr("Show the previous layer of the selected dimension"));
  connect(m_action_layer_previous, &QAction::triggered, this, [this] { step_layer(LayerStep::Previous); });

  m_action_layer_last = new QAction(layer_icon(LayerStep::Last), tr("&Last Layer"), this);
  m_action_layer_last->setObjectName("actionLayerLast");
  m_action_layer_last->setShortcut(QKeySequence(tr("Alt+End")));
  m_action_layer_last->setStatusTip(tr("Show the last layer of the selected dimension"));
  connect(m_action_layer_last, &QAction::triggered, this, [this] { step_layer(LayerStep::Last); });

  m_action_layer_first = new QAction(layer_icon(LayerStep::First), tr("&First Layer"), this);
  m_action_layer_first->setObjectName("actionLayerFirst");
  m_action_layer_first->setShortcut(QKeySequence(tr("Alt+Home")));
  m_action_layer_first->setStatusTip(tr("Show the first layer of the selected dimension"));
  connect(m_action_layer_first, &QAction::triggered, this, [this] { step_layer(LayerStep::First); });

  m_action_layer_goto = new QAction(tr("&Go to Layer..."), this);
  m_action_layer_goto->setObjectName("actionLayerGoTo");
  m_action_layer_goto->setShortcut(QKeySequence(tr("Ctrl+G")));
  m_action_layer_goto->setStatusTip(tr("Type the number of the layer to show"));
  connect(m_action_layer_goto, &QAction::triggered, this, &MainWindow::go_to_layer);

  m_layer_axis_combo = new QComboBox;
  m_layer_axis_combo->setObjectName("layerAxisCombo");
  m_layer_axis_combo->setToolTip(tr("Dimension stepped by the layer buttons"));
  m_layer_axis_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  connect(m_layer_axis_combo, &QComboBox::activated, this, [this](int axis) {
    m_layer_playback->stop();
    LayerNavigation *view = current_layer_view();
    if(!view) return;
    view->navigation_axis = axis;
    update_layer_controls();
  });

  m_layer_index_combo = new QComboBox;
  m_layer_index_combo->setObjectName("layerIndexCombo");
  m_layer_index_combo->setToolTip(tr("Select the layer to show"));
  m_layer_index_combo->setMinimumContentsLength(4);
  connect(m_layer_index_combo, &QComboBox::activated, this, [this](int index) {
    m_layer_playback->stop();
    LayerNavigation *view = current_layer_view();
    if(view) view->set_layer_index(view->navigation_axis, index);
    update_layer_controls();
  });

  m_layer_label = new QLabel;
  m_layer_label->setObjectName("layerLabel");
  m_layer_label->setContentsMargins(6, 0, 6, 0);

  m_layer_playback=new LayerPlayback(this);
  m_action_layer_play=new QAction(QIcon(":/images/play_green.png"),tr("Play"),this);
  m_action_layer_play->setObjectName("actionLayerPlay");m_action_layer_play->setCheckable(true);
  m_action_layer_play->setToolTip(tr("Play / pause layers; linked images advance together through the Anchor"));
  m_playback_interval=new QSpinBox(this);m_playback_interval->setObjectName("playbackIntervalSpin");
  m_playback_interval->setAccessibleName(tr("Frame interval (ms)"));m_playback_interval->setRange(100,10000);
  m_playback_interval->setValue(500);m_playback_interval->setSuffix(tr(" ms"));m_playback_interval->setSingleStep(100);
  m_playback_interval->setToolTip(tr("Display interval per layer in milliseconds; not the dataset time interval"));
  connect(m_playback_interval,&QSpinBox::valueChanged,m_layer_playback,&LayerPlayback::set_interval_ms);
  connect(m_action_layer_play,&QAction::triggered,this,[this](bool checked){
    if(!checked)m_layer_playback->stop();
    else if(auto *view=current_layer_view())m_layer_playback->start(dynamic_cast<QWidget*>(view),view->navigation_axis);
    update_layer_controls();
  });
  connect(m_layer_playback,&LayerPlayback::playing_changed,this,[this](bool playing){
    const QSignalBlocker blocker(m_action_layer_play);m_action_layer_play->setChecked(playing);
    m_action_layer_play->setText(playing?tr("Pause"):tr("Play"));
    m_action_layer_play->setIcon(QIcon(playing?":/images/pause_green.png":":/images/play_green.png"));
  });
  m_layer_playback->set_ready_check([this]{
    if(auto *grid=qobject_cast<GribTableView*>(m_layer_playback->target()))return !grid->read_pending();
    if(auto *section=qobject_cast<HovmollerView*>(m_layer_playback->target()))return section->ready();
    auto *raster=qobject_cast<RasterView*>(m_layer_playback->target());
    if(!raster)return true;
    if(raster->render_pending())return false;
    if(m_workspace_sync && m_workspace_sync->anchor()==raster)
      for(const auto &binding:m_workspace_sync->followers())
        if(binding.sync_layer && binding.view && binding.view->render_pending())return false;
    return true;
  });

  connect(m_mdi_area, &QMdiArea::subWindowActivated, this, &MainWindow::watch_current_view);

  ///////////////////////////////////////////////////////////////////////////////////////
  //recent files
  ///////////////////////////////////////////////////////////////////////////////////////

  for(int i = 0; i < max_recent_files; ++i)
  {
    m_action_recent_file[i] = new QAction(this);
    m_action_recent_file[i]->setVisible(false);
    connect(m_action_recent_file[i], SIGNAL(triggered()), this, SLOT(open_recent_file()));
  }

  ///////////////////////////////////////////////////////////////////////////////////////
  //menus
  ///////////////////////////////////////////////////////////////////////////////////////

  m_menu_file = menuBar()->addMenu(tr("&File"));
  m_menu_file->addAction(m_action_open);
  m_menu_file->addAction(m_action_close_file);
  m_menu_file->addAction(m_action_close_all_files);
  connect(m_menu_file, &QMenu::aboutToShow, this, &MainWindow::update_close_actions);
  m_action_separator_recent = m_menu_file->addSeparator();
  for(int i = 0; i < max_recent_files; ++i)
  {
    m_menu_file->addAction(m_action_recent_file[i]);
  }
  m_menu_file->addSeparator();
  m_menu_file->addAction(m_action_exit);

  auto *view_menu = menuBar()->addMenu(tr("&View"));
  auto *image_action = view_menu->addAction(tr("Image View"), this, [this] {
    if(m_tree->currentItem()) if(auto *item = get_item_data(m_tree->currentItem())) add_image(item);
  });
  image_action->setObjectName("actionImageView");
  auto *texture_action = view_menu->addAction(tr("Texture Entropy Image"), this, &MainWindow::show_texture_entropy);
  texture_action->setObjectName("actionTextureEntropyImage");
  texture_action->setToolTip(tr("Open a 2D map of local 4x4 gray-level entropy, bilinear to source size; no comparison required"));
  auto *section_action=view_menu->addAction(tr("Space-Time Section (Hovmöller)"),this,[this]{if(m_tree->currentItem())add_space_time_section(get_item_data(m_tree->currentItem()));});
  section_action->setObjectName("actionSpaceTimeSection");
  connect(view_menu, &QMenu::aboutToShow, this, [this, image_action, texture_action, section_action] {
    const auto *item = m_tree->currentItem() ? get_item_data(m_tree->currentItem()) : nullptr;
    const bool numeric_plane = item && item->m_kind == ItemData::Variable && item->m_dataset &&
      item->m_dataset->m_dim.size() >= 2 && (item->m_dataset->m_datatype_class == H5T_INTEGER || item->m_dataset->m_datatype_class == H5T_FLOAT);
    const bool grib_variable=item&&item->m_grib&&item->m_kind==ItemData::Variable;
    const auto grib_info=grib_variable?GribData::describe(item->m_source_path,item->m_object.object_id):DataSource::ArrayInfo{};
    image_action->setEnabled(numeric_plane||(grib_variable&&grib_info.error.isEmpty()&&grib_info.can_image));
    section_action->setEnabled(numeric_plane||(grib_variable&&grib_info.error.isEmpty()&&grib_info.can_section));
    auto *sub = m_mdi_area->currentSubWindow();
    texture_action->setEnabled(numeric_plane || (sub && qobject_cast<RasterView*>(sub->widget())));
  });

  m_menu_layer = menuBar()->addMenu(tr("&Layer"));
  m_menu_layer->addAction(m_action_layer_next);
  m_menu_layer->addAction(m_action_layer_previous);
  m_menu_layer->addAction(m_action_layer_last);
  m_menu_layer->addAction(m_action_layer_first);
  m_menu_layer->addSeparator();
  m_menu_layer->addAction(m_action_layer_goto);
  m_menu_layer->addSeparator();m_menu_layer->addAction(m_action_layer_play);

  m_menu_windows = menuBar()->addMenu(tr("&Window"));
  m_action_hide_infobar=new QAction(tr("Hide Infobar"),this);
  m_action_hide_infobar->setObjectName("actionHideInfobar");
  m_action_hide_infobar->setCheckable(true);
  connect(m_action_hide_infobar,&QAction::toggled,info_panel,[info_panel](bool hidden){info_panel->setVisible(!hidden);});
  m_workspace_sync=new WorkspaceSyncController(this);
  connect(m_workspace_sync,&WorkspaceSyncController::sync_changed,this,[this]{m_layer_playback->stop();watch_current_view(nullptr);});
  m_action_compare=new QAction(tr("Compare & Metrics"),this);
  m_action_compare->setObjectName("actionCompareMetrics");
  connect(m_action_compare,&QAction::triggered,this,&MainWindow::show_compare_panel);
  connect(m_mdi_area,&QMdiArea::subWindowActivated,this,[this]{if(m_compare_panel)QTimer::singleShot(0,this,&MainWindow::update_compare_views);});
  connect(m_menu_windows, &QMenu::aboutToShow, this, &MainWindow::update_window_menu);
  update_window_menu();

  m_menu_help = menuBar()->addMenu(tr("&Help"));
  auto *help_action = m_menu_help->addAction(tr("&Help"));
  help_action->setObjectName("actionHelp");
  help_action->setShortcut(QKeySequence::HelpContents);
  help_action->setStatusTip(tr("Open the local user guide in your web browser"));
  connect(help_action, &QAction::triggered, this, [this] {
    const QString help_path = QDir(QCoreApplication::applicationDirPath()).filePath("doc/help.html");
    if(!QFileInfo(help_path).isFile()) {
      QMessageBox::warning(this, tr("Help"), tr("The local help page is missing:\n%1\nKeep the doc folder beside HDFStudio.exe.").arg(QDir::toNativeSeparators(help_path)));
      return;
    }
    if(!QDesktopServices::openUrl(QUrl::fromLocalFile(help_path)))
      QMessageBox::warning(this, tr("Help"), tr("Could not open your web browser. Open this file directly:\n%1").arg(QDir::toNativeSeparators(help_path)));
  });
  m_menu_help->addAction(m_action_about);
  m_menu_help->addAction(tr("CF-1.8 Diagnostics..."),this,&MainWindow::show_cf_diagnostics);

  ///////////////////////////////////////////////////////////////////////////////////////
  //toolbar
  ///////////////////////////////////////////////////////////////////////////////////////

  m_tool_bar = addToolBar(tr("&File"));
  m_tool_bar->setObjectName("mainToolBar");
  m_tool_bar->addAction(m_action_open);
  //old layout: Open | up (next), down (previous), double up (last), double down (first)
  m_tool_bar->addSeparator();
  m_tool_bar->addAction(m_action_layer_next);
  m_tool_bar->addAction(m_action_layer_previous);
  m_tool_bar->addAction(m_action_layer_last);
  m_tool_bar->addAction(m_action_layer_first);
  m_tool_bar->addWidget(m_layer_axis_combo);
  m_tool_bar->addWidget(m_layer_index_combo);
  m_tool_bar->addWidget(m_layer_label);
  m_tool_bar->addSeparator();m_tool_bar->addAction(m_action_layer_play);
  auto *play_button=qobject_cast<QToolButton*>(m_tool_bar->widgetForAction(m_action_layer_play));
  play_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);play_button->setAccessibleName(tr("Play / pause layers"));
  auto *interval_label=new QLabel(tr("Frame interval"),this);interval_label->setBuddy(m_playback_interval);
  m_tool_bar->addWidget(interval_label);m_tool_bar->addWidget(m_playback_interval);
  m_tool_bar->addSeparator();m_tool_bar->addAction(m_action_compare);
  auto *compare_button=qobject_cast<QToolButton*>(m_tool_bar->widgetForAction(m_action_compare));
  compare_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  compare_button->setAccessibleName(tr("Compare & Metrics"));
  m_action_compare->setToolTip(tr("Open image comparison: Anchor/Target, lag, metrics and residual maps"));
  update_layer_controls();

  //avoid popup on toolbar
  setContextMenuPolicy(Qt::NoContextMenu);

  ///////////////////////////////////////////////////////////////////////////////////////
  //settings
  ///////////////////////////////////////////////////////////////////////////////////////

  QSettings settings("space", "hdf_explorer");
  m_sl_recent_files = settings.value("recentFiles").toStringList();
  update_recent_file_actions();

  ///////////////////////////////////////////////////////////////////////////////////////
  //icons
  ///////////////////////////////////////////////////////////////////////////////////////

  m_icon_main = QIcon(":/images/sample.png");
  m_icon_group = QIcon(":/images/folder.png");
  m_icon_dataset = QIcon(":/images/matrix.png");
  m_icon_attribute = QIcon(":/images/document.png");
  m_icon_image_indexed = QIcon(":/images/image_indexed.png");
  m_icon_image_true = QIcon(":/images/image_true.png");

  ///////////////////////////////////////////////////////////////////////////////////////
  //set main window icon
  ///////////////////////////////////////////////////////////////////////////////////////

  setWindowIcon(m_icon_main);
  update_close_actions();
}

void MainWindow::show_cf_diagnostics() {
  if(m_str_current_file.isEmpty()) {
    QMessageBox::information(this,tr("CF-1.8 Diagnostics"),tr("Open a NetCDF file first."));
    return;
  }
  NcReader reader(m_str_current_file);
  if(reader.id<0) {
    QMessageBox::warning(this,tr("CF-1.8 Diagnostics"),tr("The current file cannot be opened by netCDF-C."));
    return;
  }
  const auto findings=reader.diagnose_cf18();
  QStringList lines={tr("CF-1.8 advisory subset only; this is not a conformance certification."),
    tr("Checked: Conventions declaration, same-group bounds/climatology/coordinates references, scalar _FillValue type, and simple grid_mapping reference."),
    tr("Not checked: complete CF rules, ancestor/external references, data-wide value constraints, calendars, units database, or recommendations."),QString()};
  for(const auto &finding:findings)
    lines << QString("[%1] %2 | %3 | %4").arg(finding.severity,finding.rule,finding.path,finding.message);
  if(findings.empty()) lines << tr("No findings in the checked subset.");
  auto *dialog=new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("CF-1.8 Diagnostics"));
  auto *layout=new QVBoxLayout(dialog);
  auto *text=new QPlainTextEdit(dialog);text->setReadOnly(true);text->setPlainText(lines.join('\n'));
  layout->addWidget(text);
  dialog->resize(760,480);dialog->show();
}

static void start_report(QPlainTextEdit *page, const QStringList &arguments) {
  QFile script(":/netcdf_report.py");
  if(!script.open(QIODevice::ReadOnly)) { page->setPlainText(QObject::tr("Report resource unavailable.")); return; }
  auto *job=new QProcess(page);
  page->setPlainText(QObject::tr("Reading dataset..."));
  QObject::connect(job,&QProcess::errorOccurred,page,[page,job](QProcess::ProcessError error){
    if(error==QProcess::FailedToStart) {
      page->setPlainText(QObject::tr("Cannot start Python: %1").arg(job->errorString())); job->deleteLater();
    }
  });
  QObject::connect(job,&QProcess::finished,page,[page,job](int code,QProcess::ExitStatus status){
    const QString out=QString::fromUtf8(job->readAllStandardOutput());
    page->setPlainText(status==QProcess::NormalExit && code==0 ? out :
        QObject::tr("Report incomplete.\n%1").arg(QString::fromUtf8(job->readAllStandardError())));
    job->deleteLater();
  });
  job->start(qEnvironmentVariable("HDFSTUDIO_PYTHON","python"),QStringList{"-I","-c",QString::fromUtf8(script.readAll())}+arguments);
}

// Right-click translation of a time coordinate: raw values are converted through their declared
// "units since epoch" so the business formats (yyyymmdd, yyyymmdd hh:mm:ss) can be read directly.
void MainWindow::show_time_values() {
  FileTreeWidget *tree=findChild<FileTreeWidget*>();
  QTreeWidgetItem *item=tree?tree->currentItem():nullptr;
  if(!item) return;
  const QVariant data=item->data(0,Qt::UserRole);
  ItemData *item_data=data.value<ItemData*>();
  if(!item_data || !item_data->m_dataset) return;
  const QString file=QString::fromStdString(item_data->m_file_name);
  auto *dialog=new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Time Values"));
  auto *layout=new QVBoxLayout(dialog);
  auto *text=new QPlainTextEdit(dialog);
  text->setObjectName("timeValuesText");
  text->setReadOnly(true);
  text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  start_report(text,{"time",QFileInfo(file).absoluteFilePath(),QString::fromStdString(item_data->m_dataset->m_path)});
  layout->addWidget(text);
  dialog->resize(560,460);
  dialog->show();
}

void MainWindow::update_file_info(const QString &file_name) {
  const QFileInfo info(file_name);
  const bool grib_source=GribData::is_grib(info.absoluteFilePath());
  const FormatProbe format=grib_source ? FormatProbe{"GRIB","Native GRIB catalog",false} : probe_data_format(info.absoluteFilePath());
  const qint64 bytes=info.size();
  QString size;
  if(bytes<1024) size=tr("%1 bytes").arg(bytes);
  else if(bytes<1024*1024) size=tr("%1 KB (%2 bytes)").arg(double(bytes)/1024.0,0,'f',2).arg(bytes);
  else if(bytes<1024LL*1024*1024) size=tr("%1 MB (%2 bytes)").arg(double(bytes)/(1024.0*1024.0),0,'f',2).arg(bytes);
  else size=tr("%1 GB (%2 bytes)").arg(double(bytes)/(1024.0*1024.0*1024.0),0,'f',2).arg(bytes);
  const QDateTime modified=info.lastModified();
  QStringList lines{
    tr("Name:     %1").arg(info.fileName()),
    tr("Path:     %1").arg(info.absoluteFilePath()),
    tr("Format:   %1").arg(format.kind),
    tr("Size:     %1").arg(size),
    tr("Modified: %1").arg(modified.toString(Qt::ISODate))
  };
  m_file_info_text->setToolTip(format.detail);
  m_file_info_text->setPlainText(lines.join('\n'));

  if(grib_source) {
    m_file_info_text->setPlainText(GribData::properties(file_name,"/")+"\n"+lines.mid(3).join('\n'));
    m_file_info_text->setToolTip(tr("Native GRIB catalog; virtual arrays and physical messages"));
    for(auto *page : {m_netcdf4_text,m_cf_check_text}) {
      if(auto *job=page->findChild<QProcess*>()) {job->disconnect(page);job->kill();job->deleteLater();}
      page->setProperty("reportFile",info.absoluteFilePath());page->setProperty("pending",false);
    }
    m_netcdf4_text->setPlainText(tr("GRIB native structure: Variables [virtual arrays] / Messages [physical order].\nNo Python dependency for browsing."));
    m_cf_check_text->setPlainText(tr("CF check: not applicable to GRIB.\nFile structure, decoded values and geographical coordinates require separate verification."));
    return;
  }

  for(auto *page : {m_netcdf4_text,m_cf_check_text}) {
    // Discard stale results when the current file changes.
    if(auto *job=page->findChild<QProcess*>()) { job->disconnect(page); job->kill(); job->deleteLater(); }
    page->setProperty("reportFile",info.absoluteFilePath());
    page->setProperty("pending",format.netcdf);
    page->setPlainText(format.netcdf ? tr("Select this tab to load the report.") : tr("NetCDF identity unverified: %1").arg(format.kind));
  }
  load_report_tab(m_info_tabs->currentIndex());
}

void MainWindow::load_report_tab(int index) {
  if(index!=1 && index!=2) return;
  auto *page=index==1 ? m_netcdf4_text : m_cf_check_text;
  if(!page->property("pending").toBool()) return;
  page->setProperty("pending",false);
  start_report(page,{index==1 ? "summary" : "cf",page->property("reportFile").toString()});
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::about
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::about()
{
  QMessageBox::about(this,
    tr("About HDFStudio"),
    tr("HDFStudio 0.14.06\nAuthor: DuLiangMin\n\n"
       "Free NetCDF, HDF5 & GRIB1/GRIB2 viewer for Windows\n"
       "Metadata convention target: CF-1.8\n"
       "Product libraries: netCDF-C 4.9.3, HDF5 1.8.20\n\n"
       "Reference environment: Python 3.12.10, netCDF4-python 1.7.5.1.post1\n"
       "Python package libraries: netCDF-C 4.10.1, HDF5 2.2.0"));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::closeEvent
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::closeEvent(QCloseEvent *eve)
{
  m_layer_playback->stop();
  if(SliceJobController::any_busy()||SectionDates::any_busy()||DataReadController::any_busy()) {
    eve->ignore();SliceJobController::cancel_all();SectionDates::cancel_all();DataReadController::cancel_all();
    if(!property("sectionShutdownPending").toBool()) {
      setProperty("sectionShutdownPending",true);setEnabled(false);
      auto *timer=new QTimer(this);timer->setInterval(20);
      connect(timer,&QTimer::timeout,this,[this,timer]{if(!SliceJobController::any_busy()&&!SectionDates::any_busy()&&!DataReadController::any_busy()){timer->stop();timer->deleteLater();close();}});timer->start();
    }
    return;
  }
  QSettings settings("space", "hdf_explorer");
  settings.setValue("recentFiles", m_sl_recent_files);
  eve->accept();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//last_component
/////////////////////////////////////////////////////////////////////////////////////////////////////

QString last_component(const QString &full_file_name)
{
  return QFileInfo(full_file_name).fileName();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::update_recent_file_actions
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::update_recent_file_actions()
{
  QMutableStringListIterator i(m_sl_recent_files);
  while(i.hasNext())
  {
    QString file_name = i.next();
    if(!QFile::exists(file_name))
    {
      i.remove();
    }
  }

  for(int j = 0; j < max_recent_files; ++j)
  {
    if(j < m_sl_recent_files.count())
    {
      /////////////////////////////////////////////////////////////////////////////////////////////////////
      //display full name of OpenDAP URL or just last component of local file
      /////////////////////////////////////////////////////////////////////////////////////////////////////

      QString file_name;

      file_name = last_component(m_sl_recent_files[j]);
      QString text = tr("&%1 %2")
        .arg(j + 1)
        .arg(file_name);
      m_action_recent_file[j]->setText(text);
      m_action_recent_file[j]->setData(m_sl_recent_files[j]);
      m_action_recent_file[j]->setVisible(true);
    }
    else
    {
      m_action_recent_file[j]->setVisible(false);
    }
  }
  m_action_separator_recent->setVisible(!m_sl_recent_files.isEmpty());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::set_current_file
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::set_current_file(const QString &file_name)
{
  m_str_current_file = file_name;

  QString shownName = tr("Untitled");
  if(!m_str_current_file.isEmpty())
  {
    shownName = last_component(m_str_current_file);
    m_sl_recent_files.removeAll(m_str_current_file);
    m_sl_recent_files.prepend(m_str_current_file);
    update_recent_file_actions();
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::open_file
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::open_file()
{
  QString file_name = QFileDialog::getOpenFileName(this,
    tr("Open File"), ".",
    tr("Scientific data (*.h5 *.hdf5 *.he5 *.nc *.nc4 *.grib *.grib2 *.grb *.grb2);;All files (*.*)"));

  if(file_name.isEmpty())
    return;

  read_file(file_name);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//close file
//Order matters: views are deleted first (a grid keeps a pointer into its ItemData), then the
//ItemData objects, then the tree nodes. Python report jobs are killed because they hold the file open.
/////////////////////////////////////////////////////////////////////////////////////////////////////

static void collect_item_data(QTreeWidgetItem *item, std::vector<ItemData*> &out)
{
  if(ItemData *item_data = get_item_data(item)) out.push_back(item_data);
  for(int i = 0; i < item->childCount(); ++i) collect_item_data(item->child(i), out);
}

void MainWindow::tag_view(QMdiSubWindow *sub, const ItemData *source)
{
  sub->setProperty("sourceItem", QVariant::fromValue(reinterpret_cast<quintptr>(source)));
  QStringList files;for(int i=0;i<m_tree->topLevelItemCount();++i)if(auto *item=get_item_data(m_tree->topLevelItem(i)))files.append(item_file(item));setProperty("openSourceFiles",files);
  if(m_compare_panel)QTimer::singleShot(0,this,&MainWindow::update_compare_views);
}

void MainWindow::show_compare_panel() {
  if(!m_compare_panel) {m_compare_panel=new ComparePanel(m_workspace_sync,this);addDockWidget(Qt::RightDockWidgetArea,m_compare_panel);}
  update_compare_views();m_compare_panel->show();m_compare_panel->raise();
}
void MainWindow::update_compare_views() {
  if(!m_compare_panel)return;
  QList<RasterView*> views;
  for(auto *sub:m_mdi_area->subWindowList(QMdiArea::CreationOrder))if(auto *view=qobject_cast<RasterView*>(sub->widget()))views.append(view);
  m_compare_panel->set_views(views);
}

void MainWindow::update_close_actions()
{
  const bool has_file = m_tree->topLevelItemCount() > 0;
  m_action_close_file->setEnabled(has_file);
  m_action_close_all_files->setEnabled(has_file);
}

void MainWindow::close_current_file()
{
  QTreeWidgetItem *item = m_tree->currentItem();
  if(!item && m_tree->topLevelItemCount() > 0) item = m_tree->topLevelItem(m_tree->topLevelItemCount() - 1);
  if(!item) { statusBar()->showMessage(tr("No file opened.")); return; }
  close_file_of(item);
}

void MainWindow::close_all_files()
{
  while(m_tree->topLevelItemCount() > 0) close_file_of(m_tree->topLevelItem(0));
}

void MainWindow::close_file_of(QTreeWidgetItem *item)
{
  if(!item) return;
  QTreeWidgetItem *root = item;
  while(root->parent()) root = root->parent();

  std::vector<ItemData*> owned;
  collect_item_data(root, owned);
  const QString closed_name = root->text(0);

  const auto views = m_mdi_area->subWindowList();
  for(QMdiSubWindow *sub : views) {
    const quintptr source = sub->property("sourceItem").value<quintptr>();
    for(ItemData *item_data : owned) {
      if(reinterpret_cast<quintptr>(item_data) == source) {
        sub->setAttribute(Qt::WA_DeleteOnClose, false);
        delete sub;
        break;
      }
    }
  }

  delete m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(root));
  for(ItemData *item_data : owned) delete item_data;
  QStringList open_files;
  for(int i=0;i<m_tree->topLevelItemCount();++i)if(auto *data=get_item_data(m_tree->topLevelItem(i)))open_files.append(item_file(data));
  setProperty("openSourceFiles",open_files);

  if(m_tree->topLevelItemCount() == 0) {
    m_str_current_file.clear();
    for(auto *page : {m_file_info_text, m_netcdf4_text, m_cf_check_text}) {
      if(auto *job = page->findChild<QProcess*>()) { job->disconnect(page); job->kill(); job->deleteLater(); }
      page->setProperty("pending", false);
      page->setPlainText(tr("No file opened."));
    }
    m_file_info_text->setToolTip(QString());
    statusBar()->showMessage(tr("Closed %1").arg(closed_name));
  } else {
    QTreeWidgetItem *last = m_tree->topLevelItem(m_tree->topLevelItemCount() - 1);
    m_tree->setCurrentItem(last);
    const ItemData *last_data = get_item_data(last);
    if(last_data) {
      m_str_current_file = item_file(last_data);
      update_file_info(m_str_current_file);
    }
    statusBar()->showMessage(tr("Closed %1").arg(closed_name));
  }
  update_close_actions();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
  if(accepts_file_drop(event->mimeData())) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  }
}

void MainWindow::dropEvent(QDropEvent *event)
{
  handle_file_drop(event->mimeData(), event);
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
  if(accepts_file_drop(event->mimeData())) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  }
}

// true for this window and every widget inside it, including floating docks and its dialogs
bool MainWindow::owns_widget(const QWidget *widget) const
{
  for(const QWidget *w = widget; w; w = w->parentWidget())
    if(w == this) return true;
  return false;
}

// Installed on the application (Qt 6 "The Event System": an application event filter sees an event
// before the widget does). A local file dragged from Explorer is opened wherever it is released in
// HDFStudio; child widgets that accept text drops (line edits, editable text) would otherwise take
// the file path as text and the file would not open.
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
  const QEvent::Type type = event->type();
  if(type == QEvent::DragEnter || type == QEvent::DragMove || type == QEvent::Drop) {
    QWidget *widget = qobject_cast<QWidget *>(watched);
    auto *drop_event = static_cast<QDropEvent *>(event);   // QDragEnterEvent and QDragMoveEvent derive from it
    if(widget && owns_widget(widget) && accepts_file_drop(drop_event->mimeData())) {
      if(type == QEvent::Drop) {
        handle_file_drop(drop_event->mimeData(), drop_event);
      } else {
        drop_event->setDropAction(Qt::CopyAction);
        drop_event->accept();
      }
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::accepts_file_drop(const QMimeData *mime_data) const
{
  for(const QUrl &url : mime_data->urls())
    if(url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) return true;
  return false;
}

void MainWindow::handle_file_drop(const QMimeData *mime_data, QDropEvent *event)
{
  QStringList files;
  for(const QUrl &url : mime_data->urls())
    if(url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) files << url.toLocalFile();
  if(files.isEmpty()) return;
  event->setDropAction(Qt::CopyAction);
  event->accept();
  // Finish the shell drop before opening files or reporting a format error.
  QTimer::singleShot(0, this, [this, files]() {
    for(const QString &file : files) read_file(file);
  });
}

void FileTreeWidget::dragEnterEvent(QDragEnterEvent *event)
{
  if(m_main_window && m_main_window->accepts_file_drop(event->mimeData())) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  }
}

void FileTreeWidget::dragMoveEvent(QDragMoveEvent *event)
{
  if(m_main_window && m_main_window->accepts_file_drop(event->mimeData())) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  }
}

void FileTreeWidget::dropEvent(QDropEvent *event)
{
  if(m_main_window) m_main_window->handle_file_drop(event->mimeData(), event);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////
//MainWindow::open_recent_file
/////////////////////////////////////////////////////////////////////////////////////////////////////

void MainWindow::open_recent_file()
{
  QAction *action = qobject_cast<QAction *>(sender());
  if(action)
  {
    read_file(action->data().toString());
  }
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::add_classic_tree
//builds the tree of a NetCDF-3 file: variables with their attributes, then the global attributes.
//Paths follow the HDF5 convention: "/name" for a variable, an attribute item keeps its parent's path.
///////////////////////////////////////////////////////////////////////////////////////

static void add_classic_attributes(QTreeWidgetItem *parent, const std::string &file_name,
                                   const std::string &parent_path,
                                   const std::vector<NcClassicAttribute> &attributes, const QIcon &icon)
{
  for(const NcClassicAttribute &attribute : attributes)
  {
    size_t size = 0;
    H5T_sign_t sign = H5T_SGN_2;
    H5T_class_t cls = H5T_NO_CLASS;
    classic_describe_type(attribute.type, size, sign, cls);
    // HDF5 stores text and single values as scalars, longer numeric attributes as a 1-D array
    std::vector<hsize_t> dim;
    if(cls != H5T_STRING && attribute.length > 1) dim.push_back(attribute.length);
    hdf_dataset_t *dataset = new hdf_dataset_t(parent_path.c_str(), dim,
        cls == H5T_STRING ? attribute.length : size, sign, cls);
    dataset->m_classic = true;
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, QString::fromStdString(attribute.name));
    item->setIcon(0, icon);
    ItemData *item_data = new ItemData(ItemData::Attribute, file_name, attribute.name, dataset);
    QVariant data;
    data.setValue(item_data);
    item->setData(0, Qt::UserRole, data);
  }
}

QTreeWidgetItem *MainWindow::add_classic_tree(const QString &file_name, const std::string &stored_name,
                                              const QString &display_name)
{
  const NcClassicFile info = nc_classic_inspect(file_name);
  if(!info.error.isEmpty())
  {
    QMessageBox::warning(this, tr("Open file"),
        tr("Cannot open as NetCDF classic (CDF-1/2/5):\n%1\n%2").arg(file_name, info.error));
    return nullptr;
  }

  ItemData *group_data = new ItemData(ItemData::Group, stored_name, "/", (hdf_dataset_t*)NULL);
  QTreeWidgetItem *root_item = new QTreeWidgetItem(m_tree);
  root_item->setText(0, display_name);
  root_item->setIcon(0, m_icon_group);
  QVariant data;
  data.setValue(group_data);
  root_item->setData(0, Qt::UserRole, data);

  for(const NcClassicVariable &variable : info.variables)
  {
    size_t size = 0;
    H5T_sign_t sign = H5T_SGN_2;
    H5T_class_t cls = H5T_NO_CLASS;
    classic_describe_type(variable.type, size, sign, cls);
    std::vector<hsize_t> dim(variable.shape.begin(), variable.shape.end());
    const std::string path = "/" + variable.name;
    hdf_dataset_t *dataset = new hdf_dataset_t(path.c_str(), dim, size, sign, cls);
    dataset->m_classic = true;
    auto *item_var = new QTreeWidgetItem(root_item);
    item_var->setText(0, QString::fromStdString(variable.name));
    item_var->setIcon(0, dataset_icon(dim.size()));
    ItemData *item_data = new ItemData(ItemData::Variable, stored_name, variable.name, dataset);
    QVariant variable_data;
    variable_data.setValue(item_data);
    item_var->setData(0, Qt::UserRole, variable_data);
    add_classic_attributes(item_var, stored_name, path, variable.attributes, m_icon_attribute);
  }
  add_classic_attributes(root_item, stored_name, "/", info.global_attributes, m_icon_attribute);
  return root_item;
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::read_file
///////////////////////////////////////////////////////////////////////////////////////

QTreeWidgetItem *MainWindow::add_grib_tree(const QString &file_name)
{
  QPointer<MainWindow> window=this;
  setProperty("gribIndexPending",true);
  // The dialog and event loop stay valid even if a programmatic caller deletes the window.
  QProgressDialog progress(tr("Indexing GRIB messages..."),tr("Cancel"),0,0);
  progress.setObjectName("gribIndexProgress");progress.setWindowTitle(tr("Open GRIB"));
  progress.setWindowModality(Qt::ApplicationModal);progress.setMinimumDuration(0);
  auto *reader=new DataReadController;
  QEventLoop loop;std::shared_ptr<const grib::Catalog> catalog;QString error;
  bool started=false,cancelled=false;
  connect(reader,&DataReadController::indexed,&loop,[&](auto result){catalog=std::move(result);loop.quit();});
  connect(reader,&DataReadController::failed,&loop,[&](const QString &message){error=message;loop.quit();});
  connect(reader,&DataReadController::drained,&loop,[&]{if(started)loop.quit();});
  connect(&progress,&QProgressDialog::canceled,&loop,[&]{cancelled=true;reader->cancel();});
  connect(this,&QObject::destroyed,&loop,[&]{cancelled=true;reader->cancel();});
  QTimer::singleShot(0,&loop,[&]{if(cancelled){loop.quit();return;}reader->index(file_name);started=true;});
  loop.exec();reader->disconnect(&loop);progress.disconnect(&loop);reader->dispose();progress.close();
  if(window)window->setProperty("gribIndexPending",false);
  if(!window||cancelled||!catalog) {
    GribData::release(file_name);
    if(window&&!cancelled&&!error.isEmpty())QMessageBox::warning(window,tr("Open GRIB"),error);
    return nullptr;
  }
  try {
    // Build off-tree: cancellation/invalid metadata cannot publish an incomplete source.
    auto root=std::make_unique<QTreeWidgetItem>();
    std::vector<std::unique_ptr<ItemData>> staged;
    auto make_item=[&](QTreeWidgetItem *parent,const QString &label,const QString &object,DataSource::NodeKind kind){
      auto *node=parent?new QTreeWidgetItem(parent):root.get();
      const bool array=kind==DataSource::NodeKind::Array||kind==DataSource::NodeKind::Field;
      auto data=std::make_unique<ItemData>(array?ItemData::Variable:ItemData::Group,std::string(),label.toUtf8().toStdString(),nullptr);
      data->m_grib=true;data->m_source_path=file_name;data->m_object={catalog->session,object,kind};
      if(array)data->m_array=GribData::describe(*catalog,object);
      node->setText(0,label);node->setToolTip(0,object);
      node->setData(0,Qt::UserRole,QVariant::fromValue(data.get()));
      node->setData(0,Qt::UserRole+10,object);node->setData(0,Qt::UserRole+13,catalog->session);
      node->setIcon(0,array?dataset_icon(data->m_array.shape.size()):m_icon_group);
      staged.push_back(std::move(data));return node;
    };
    auto starts=std::make_shared<std::vector<size_t>>();
    for(size_t i=0;i<catalog->fields.size();++i)if(!i||catalog->fields[i].offset!=catalog->fields[i-1].offset)starts->push_back(i);
    const size_t message_count=starts->size();starts->push_back(catalog->fields.size());
    make_item(nullptr,QFileInfo(file_name).fileName()+QString(" [%1; %2 messages / %3 fields]").arg(catalog->format).arg(message_count).arg(catalog->fields.size()),"/",DataSource::NodeKind::File);
    auto *variables=make_item(root.get(),tr("Variables [virtual arrays]"),"/variables",DataSource::NodeKind::Group);
    get_item_data(variables)->m_grib_details=tr("Virtual arrays group compatible source fields; these are not HDF groups.\n%1 arrays. Missing slots and duplicate fields remain explicit.").arg(catalog->cubes.size());
    for(const auto &cube:catalog->cubes) {
      auto *node=make_item(variables,cube.title,cube.id,DataSource::NodeKind::Array);
      const auto &info=get_item_data(node)->m_array;QStringList shape;
      for(const auto &axis:info.axes)shape<<QString("%1=%2").arg(axis.name).arg(axis.length);
      node->setToolTip(0,cube.id+"\n("+shape.join(", ")+")\n"+info.disabled_reasons.value("image"));
      auto *axes=make_item(node,tr("Axes [source labels, index0]"),cube.id,DataSource::NodeKind::Information);
      QString details=tr("Axes describe the virtual array; labels retain source product identity.\n");
      for(const auto &axis:info.axes) {
        details+=QString("%1: length=%2; units=%3\n").arg(axis.name).arg(axis.length).arg(axis.units.isEmpty()?QString("unknown"):axis.units);
        const int shown=qMin(axis.labels.size(),qsizetype(256));
        for(int i=0;i<shown;++i)details+=QString("  [%1] %2\n").arg(i).arg(axis.labels[i]);
        if(axis.labels.size()>shown)details+=tr("  Additional labels omitted from this bounded report.\n");
      }
      get_item_data(axes)->m_grib_details=details;
    }
    auto *messages=make_item(root.get(),tr("Messages [physical order, paged]"),"/messages",DataSource::NodeKind::Group);
    get_item_data(messages)->m_grib_details=tr("%1 physical messages / %2 fields; pages contain at most 256 messages.\nOffsets, lengths and sub-field indices identify original source fields.").arg(message_count).arg(catalog->fields.size());
    for(size_t begin=0;begin<message_count;begin+=256) {
      const size_t end=std::min(begin+256,message_count);
      auto *page=make_item(messages,QString("Messages %1-%2").arg(begin).arg(end-1),"/messages",DataSource::NodeKind::Group);
      get_item_data(page)->m_grib_details=tr("Physical messages [%1, %2), zero-based; expand this page to load fields.").arg(begin).arg(end);
      page->setData(0,Qt::UserRole+11,qulonglong(begin));page->setData(0,Qt::UserRole+12,qulonglong(end));
      new QTreeWidgetItem(page,QStringList{tr("Expand to load")});
    }
    make_item(root.get(),tr("File information / diagnostics"),"/",DataSource::NodeKind::Information);
    auto session=std::make_unique<GribTreeSession>(file_name);
    connect(m_tree,&QTreeWidget::itemExpanded,session.get(),[this,catalog,starts,file_name](QTreeWidgetItem *page){
      // Expansion from another file must never be interpreted using this source's catalog.
      if(page->data(0,Qt::UserRole+13).toString()!=catalog->session||!page->data(0,Qt::UserRole+11).isValid())return;
      const size_t begin=page->data(0,Qt::UserRole+11).toULongLong(),end=page->data(0,Qt::UserRole+12).toULongLong();
      page->setData(0,Qt::UserRole+11,QVariant());qDeleteAll(page->takeChildren());
      auto append=[&](QTreeWidgetItem *parent,const QString &label,const QString &object,DataSource::NodeKind kind){
        auto *node=new QTreeWidgetItem(parent);auto *data=new ItemData(kind==DataSource::NodeKind::Field?ItemData::Variable:ItemData::Group,std::string(),label.toUtf8().toStdString(),nullptr);
        data->m_grib=true;data->m_source_path=file_name;data->m_object={catalog->session,object,kind};
        node->setText(0,label);node->setData(0,Qt::UserRole,QVariant::fromValue(data));
        node->setData(0,Qt::UserRole+10,object);node->setData(0,Qt::UserRole+13,catalog->session);
        node->setIcon(0,kind==DataSource::NodeKind::Field?dataset_icon(2):m_icon_group);return node;
      };
      for(size_t message=begin;message<end;++message) {
        const auto &first=catalog->fields[starts->at(message)];
        auto *physical=append(page,QString("Message %1 [offset=%2, length=%3]").arg(message).arg(first.offset).arg(first.length),"/messages",DataSource::NodeKind::Group);
        get_item_data(physical)->m_grib_details=QString("Physical message %1\nOffset: %2\nLength: %3\nSub-fields: %4\n").arg(message).arg(first.offset).arg(first.length).arg(starts->at(message+1)-starts->at(message));
        for(size_t i=starts->at(message);i<starts->at(message+1);++i) {
          const auto &field=catalog->fields[i];auto *node=append(physical,QString("Field %1 [sub=%2] %3 %4").arg(i).arg(field.sub_field).arg(field.short_name,field.time_label),QString("/field/%1").arg(i),DataSource::NodeKind::Field);
          // Shape/capabilities come from the immutable catalog when an analysis is implemented.
          node->setToolTip(0,QString("/field/%1\noffset=%2 length=%3 sub_field=%4").arg(i).arg(field.offset).arg(field.length).arg(field.sub_field));
        }
      }
    });
    get_item_data(root.get())->m_grib_session=std::move(session);
    m_tree->addTopLevelItem(root.get());for(auto &data:staged)data.release();
    auto *published=root.release();published->setExpanded(true);variables->setExpanded(true);return published;
  }catch(const grib::Error &failure) {
    GribData::release(file_name);QMessageBox::warning(this,tr("Open GRIB"),failure.code+": "+failure.message);return nullptr;
  }
}

int MainWindow::read_file(QString file_name)
{
  if(property("sectionShutdownPending").toBool())return -1;
  QByteArray ba;
  std::string path;
  std::string str_file_name;
  QString name;
  int index;
  int len;
  hid_t fid;

  //convert QString to char*
  file_name = QFileInfo(file_name).absoluteFilePath();
  if(property("gribIndexPending").toBool())return -1;
  if(GribData::is_grib(file_name)) {
    for(int i=0;i<m_tree->topLevelItemCount();++i) {
      auto *data=get_item_data(m_tree->topLevelItem(i));
      if(data&&data->m_grib&&data->m_source_path==file_name) {m_tree->setCurrentItem(m_tree->topLevelItem(i));return 0;}
    }
    auto *root=add_grib_tree(file_name);if(!root)return -1;
    auto files=property("openSourceFiles").toStringList();if(!files.contains(file_name))files.append(file_name);setProperty("openSourceFiles",files);
    m_tree->setCurrentItem(root);set_current_file(file_name);update_file_info(file_name);update_close_actions();statusBar()->showMessage(file_name);return 0;
  }
  ba = file_name.toLocal8Bit();
  if(QString::fromLocal8Bit(ba) != file_name) {
    QMessageBox::warning(this, tr("Open file"), tr("This HDF5 library cannot represent this path in the current Windows code page:\n%1").arg(file_name));
    return -1;
  }

  //convert to std::string
  str_file_name = ba.data();

  index = file_name.lastIndexOf(QChar('/'));
  len = file_name.length();
  name = file_name.right(len - index - 1);

  QTreeWidgetItem *root_item = nullptr;

  ///////////////////////////////////////////////////////////////////////////////////////
  //NetCDF-3 (CDF-1/2/5) has no HDF5 container: H5Fopen reports "file signature not found",
  //so these files are read through netCDF-C
  ///////////////////////////////////////////////////////////////////////////////////////

  if(is_classic_netcdf_file(file_name))
  {
    root_item = add_classic_tree(file_name, str_file_name, name);
    if(!root_item) return -1;
  }
  else
  {

  ///////////////////////////////////////////////////////////////////////////////////////
  //construct list of objects
  ///////////////////////////////////////////////////////////////////////////////////////

  m_visit.visit_info.clear();

  ///////////////////////////////////////////////////////////////////////////////////////
  //populate objects
  ///////////////////////////////////////////////////////////////////////////////////////

  if((fid = H5Fopen(str_file_name.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT)) < 0)
  {
    QMessageBox::warning(this, tr("Open file"), tr("Cannot open as HDF5 / NetCDF-4:\n%1").arg(file_name));
    return -1;
  }
  m_visit.visit(str_file_name.c_str());

  //item data group item
  ItemData *item_data_grp = new ItemData(ItemData::Group, str_file_name, "/", (hdf_dataset_t*)NULL);

  //add root
  root_item = new QTreeWidgetItem(m_tree);
  root_item->setText(0, name);
  root_item->setIcon(0, m_icon_group);
  //add data
  QVariant data;
  data.setValue(item_data_grp);
  root_item->setData(0, Qt::UserRole, data);

  if(iterate(str_file_name, "/", fid, root_item) < 0)
  {

  }

  if(H5Fclose(fid) < 0)
  {

  }

  }
  root_item->setExpanded(true);
  {auto files=property("openSourceFiles").toStringList();if(!files.contains(file_name))files.append(file_name);setProperty("openSourceFiles",files);}
  set_current_file(file_name);
  update_file_info(file_name);
  QString suffix = QFileInfo(file_name).suffix().toLower();
  if(suffix == "nc" || suffix == "nc4") {
    NcReader nc(file_name);
    QString cf = nc.cf_summary();
    root_item->setToolTip(0, cf);
    root_item->setText(0, name + "  [" + cf + "]");
    statusBar()->showMessage(cf);
  } else statusBar()->showMessage(file_name);
  update_close_actions();
  return 0;
}


///////////////////////////////////////////////////////////////////////////////////////
//count_objects_cb
//callback function function for H5Literate
///////////////////////////////////////////////////////////////////////////////////////

static herr_t count_objects_cb(hid_t, const char *, const H5L_info_t *, void *_op_data)
{
  hsize_t *op_data = (hsize_t *)_op_data;

  (*op_data)++;

  return(H5_ITER_CONT);
}

///////////////////////////////////////////////////////////////////////////////////////
//get_name_type_cb
//callback function function for H5Literate
//////////////////////////////////////////////////////////////////////////////////////

static herr_t get_name_type_cb(hid_t loc_id, const char *name, const H5L_info_t *, void *op_data)
{
  H5G_stat_t stat_buf;

  if(H5Gget_objinfo(loc_id, name, 0, &stat_buf) < 0)
  {

  }

  ((name_type_t *)op_data)->type = stat_buf.type;
  ((name_type_t *)op_data)->name = (char *)strdup(name);

  // define H5_ITER_STOP for return. This will cause the iterator to stop 
  return H5_ITER_STOP;
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::find_object
//////////////////////////////////////////////////////////////////////////////////////

H5O_info_added_t* MainWindow::find_object(haddr_t addr)
{
  for(size_t idx = 0; idx < m_visit.visit_info.size(); idx++)
  {
    if(addr == m_visit.visit_info[idx].oinfo.addr)
    {
      return &(m_visit.visit_info[idx]);
    }
  }
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::iterate
//iterates in group specified by location id 'loc_id'
//full group name is used to recursevely construct a dataset path
//QTreeWidgetItem * is used to build the tree item hierarchy 
//passing new QTreeWidgetItem for group as parent
//for datasets, sizes and metadata are stored
///////////////////////////////////////////////////////////////////////////////////////

int MainWindow::iterate(const std::string& file_name, const std::string& grp_path, const hid_t loc_id, QTreeWidgetItem *tree_item_parent)
{
  hsize_t nbr_objects = 0;
  QTreeWidgetItem *item_grp = NULL;
  QTreeWidgetItem *item_var = NULL;
  hsize_t index;
  name_type_t info;
  ItemData *item_data;
  QVariant data;
  std::string path;
  bool do_iterate;
  size_t datatype_size;
  H5T_sign_t datatype_sign;
  H5T_class_t datatype_class;
  hid_t gid;
  hid_t did;
  hid_t sid;
  hid_t ftid;
  hid_t mtid;
  hsize_t dims[H5S_MAX_RANK];
  int rank;

  if(H5Literate(loc_id, H5_INDEX_NAME, H5_ITER_INC, NULL, count_objects_cb, &nbr_objects) < 0)
  {

  }

  for(hsize_t idx_obj = 0; idx_obj < nbr_objects; idx_obj++)
  {

    index = idx_obj;

    //'index' allows an interrupted iteration to be resumed; it is passed in by the application with a starting point 
    //and returned by the library with the point at which the iteration stopped

    if(H5Literate(loc_id, H5_INDEX_NAME, H5_ITER_INC, &index, get_name_type_cb, &info) < 0)
    {

    }

    // initialize path 
    path = grp_path;
    if(grp_path != "/")
      path += "/";
    path += info.name;

    switch(info.type)
    {
      ///////////////////////////////////////////////////////////////////////////////////////
      //H5G_GROUP
      //////////////////////////////////////////////////////////////////////////////////////

    case H5G_GROUP:

      if((gid = H5Gopen2(loc_id, info.name, H5P_DEFAULT)) < 0)
      {

      }

      H5O_info_t oinfo_buf;
      do_iterate = true;

      //get object info
      if(H5Oget_info(gid, &oinfo_buf) < 0)
      {

      }

      //group item
      item_grp = new QTreeWidgetItem(tree_item_parent);
      item_grp->setText(0, info.name);
      item_grp->setIcon(0, m_icon_group);
      //item data
      item_data = new ItemData(ItemData::Group, file_name, info.name, (hdf_dataset_t*)NULL);
      data.setValue(item_data);
      item_grp->setData(0, Qt::UserRole, data);

      if(oinfo_buf.rc > 1)
      {
        H5O_info_added_t *oinfo_added = find_object(oinfo_buf.addr);

        if(!oinfo_added || oinfo_added->added > 0)
        {
          //avoid infinite recursion due to a circular path in the file.
          do_iterate = false;
        }
        else
        {
          oinfo_added->added++;
        }
      }

      //iterate in sub group passing QTreeWidgetItem for group as parent
      if(do_iterate && iterate(file_name, path, gid, item_grp) < 0)
      {

      }

      if(get_attributes(file_name, path, gid, item_grp) < 0)
      {

      }

      if(H5Gclose(gid) < 0)
      {

      }

      free(info.name);
      break;

      ///////////////////////////////////////////////////////////////////////////////////////
      //H5G_DATASET
      //////////////////////////////////////////////////////////////////////////////////////

    case H5G_DATASET:

      if((did = H5Dopen2(loc_id, info.name, H5P_DEFAULT)) < 0)
      {

      }

      if((sid = H5Dget_space(did)) < 0)
      {

      }

      if((rank = H5Sget_simple_extent_dims(sid, dims, NULL)) < 0)
      {

      }

      if((ftid = H5Dget_type(did)) < 0)
      {

      }

      if((mtid = H5Tget_native_type(ftid, H5T_DIR_DEFAULT)) < 0)
      {

      }

      ///////////////////////////////////////////////////////////////////////////////////////
      //store datatype sizes and metadata needed to display HDF5 buffer data
      ///////////////////////////////////////////////////////////////////////////////////////

      if((datatype_size = H5Tget_size(mtid)) == 0)
      {

      }

      if((datatype_sign = H5Tget_sign(mtid)) < 0)
      {

      }

      if((datatype_class = H5Tget_class(mtid)) < 0)
      {

      }

      if(H5Sclose(sid) < 0)
      {

      }

      if(H5Tclose(ftid) < 0)
      {

      }

      if(H5Tclose(mtid) < 0)
      {

      }

      //store dimensions in ItemData
      std::vector< hsize_t> dim; //dataset dimensions
      for(int idx = 0; idx < rank; ++idx)
      {
        dim.push_back(dims[idx]);
      }

      //store a hdf_dataset_t with full path, dimensions and metadata
      hdf_dataset_t *dataset = new hdf_dataset_t(
        path.c_str(),
        dim,
        datatype_size,
        datatype_sign,
        datatype_class);

      //append item
      item_var = new QTreeWidgetItem(tree_item_parent);
      item_var->setText(0, info.name);
      item_var->setIcon(0, dataset_icon(dim.size()));
      //item data
      item_data = new ItemData(ItemData::Variable, file_name, info.name, dataset);
      data.setValue(item_data);
      item_var->setData(0, Qt::UserRole, data);

      if(get_attributes(file_name, path, did, item_var) < 0)
      {

      }

      if(H5Dclose(did) < 0)
      {

      }

      break;
    }

  }

  return 0;
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::get_attributes
// it is assumed that loc_id is either from 
// loc_id = H5Gopen( fid, name);
// loc_id = H5Dopen( fid, name);
// loc_id = H5Topen( fid, name);
///////////////////////////////////////////////////////////////////////////////////////

int MainWindow::get_attributes(const std::string& file_name, const std::string& path, const hid_t loc_id, QTreeWidgetItem *tree_item_parent)
{
  H5O_info_t oinfo;
  hid_t aid;
  size_t datatype_size;
  H5T_sign_t datatype_sign;
  H5T_class_t datatype_class;
  hid_t sid;
  hid_t ftid;
  hid_t mtid;
  hsize_t dims[H5S_MAX_RANK];
  char name[1024];
  int rank;
  ItemData *item_data;
  QVariant data;
  QTreeWidgetItem *item = NULL;

  //get object info
  if(H5Oget_info(loc_id, &oinfo) < 0)
  {

  }

  for(hsize_t idx = 0; idx < oinfo.num_attrs; idx++)
  {
    if((aid = H5Aopen_by_idx(loc_id, ".", H5_INDEX_CRT_ORDER, H5_ITER_INC, idx, H5P_DEFAULT, H5P_DEFAULT)) < 0)
    {

    }

    if(H5Aget_name(aid, (size_t)1024, name) < 0)
    {

    }

    if((sid = H5Aget_space(aid)) < 0)
    {

    }

    if((rank = H5Sget_simple_extent_dims(sid, dims, NULL)) < 0)
    {

    }

    if((ftid = H5Aget_type(aid)) < 0)
    {

    }

    if((mtid = H5Tget_native_type(ftid, H5T_DIR_DEFAULT)) < 0)
    {

    }

    ///////////////////////////////////////////////////////////////////////////////////////
    //store datatype sizes and metadata needed to display HDF5 buffer data
    ///////////////////////////////////////////////////////////////////////////////////////

    if((datatype_size = H5Tget_size(mtid)) == 0)
    {

    }

    if((datatype_sign = H5Tget_sign(mtid)) < 0)
    {

    }

    if((datatype_class = H5Tget_class(mtid)) < 0)
    {

    }

    if(H5Sclose(sid) < 0)
    {

    }

    if(H5Tclose(ftid) < 0)
    {

    }

    if(H5Tclose(mtid) < 0)
    {

    }

    if(H5Aclose(aid) < 0)
    {

    }

    //store dimensions in ItemData
    std::vector< hsize_t> dim; //dataset dimensions
    for(int idx = 0; idx < rank; ++idx)
    {
      dim.push_back(dims[idx]);
    }

    //store a hdf_dataset_t with full path, dimensions and metadata
    hdf_dataset_t *dataset = new hdf_dataset_t(
      path.c_str(),
      dim,
      datatype_size,
      datatype_sign,
      datatype_class);

    //append item
    item = new QTreeWidgetItem(tree_item_parent);
    item->setText(0, name);
    item->setIcon(0, m_icon_attribute);
    //item data
    item_data = new ItemData(ItemData::Attribute, file_name, name, dataset);
    data.setValue(item_data);
    item->setData(0, Qt::UserRole, data);
  }

  return 0;
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::FileTreeWidget 
///////////////////////////////////////////////////////////////////////////////////////

FileTreeWidget::FileTreeWidget(QWidget *parent) : QTreeWidget(parent)
{
  setContextMenuPolicy(Qt::CustomContextMenu);

  //right click menu
  connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), SLOT(show_context_menu(const QPoint &)));

  //double click
  connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
    ItemData *data = get_item_data(item);
    if(data&&data->m_grib) {
      setCurrentItem(item);
      if(data->m_kind==ItemData::Variable) {
        const auto info=GribData::describe(data->m_source_path,data->m_object.object_id);
        if(info.error.isEmpty()&&info.can_image){m_main_window->add_image(data);return;}
      }
      show_item_properties();return;
    }
    if(!data || !data->m_dataset) return;
    if(data->m_kind == ItemData::Variable && data->m_dataset->m_dim.size() >= 2 &&
       (data->m_dataset->m_datatype_class == H5T_INTEGER || data->m_dataset->m_datatype_class == H5T_FLOAT))
      m_main_window->add_image(data);
    else add_grid();
  });
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::~FileTreeWidget
///////////////////////////////////////////////////////////////////////////////////////

FileTreeWidget::~FileTreeWidget()
{
  QTreeWidgetItemIterator it(this);
  while(*it)
  {
    ItemData *item_data = get_item_data(*it);
    delete item_data;
    ++it;
  }

}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::load_item
/////////////////////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::load_item(QTreeWidgetItem  *item)
{
  hid_t fid;
  hid_t did;
  hid_t sid;
  hid_t ftid;
  hid_t mtid;
  int rank;
  size_t datatype_size;
  H5T_sign_t datatype_sign;
  H5T_class_t datatype_class;
  hsize_t dims[H5S_MAX_RANK];
  hsize_t nbr_elements = 1;

  ItemData *item_data = get_item_data(item);
  if(!item_data||!item_data->m_dataset)return;
  assert(item_data->m_kind == ItemData::Variable);

  //if not loaded, read buffer from file 
  if(item_data->m_dataset->m_buf != NULL)
  {
    return;
  }

  if(item_data->m_dataset->m_classic)
  {
    const QString error = classic_load_variable(item_data, std::vector<int>());
    if(!error.isEmpty()) QMessageBox::warning(this, tr("Grid"), error);
    return;
  }

  if((fid = H5Fopen(item_data->m_file_name.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT)) < 0)
  {

  }

  if((did = H5Dopen2(fid, item_data->m_dataset->m_path.c_str(), H5P_DEFAULT)) < 0)
  {

  }

  if((sid = H5Dget_space(did)) < 0)
  {

  }

  if((rank = H5Sget_simple_extent_dims(sid, dims, NULL)) < 0)
  {

  }

  if((ftid = H5Dget_type(did)) < 0)
  {

  }

  if((mtid = H5Tget_native_type(ftid, H5T_DIR_DEFAULT)) < 0)
  {

  }

  if((datatype_size = H5Tget_size(mtid)) == 0)
  {

  }

  if((datatype_sign = H5Tget_sign(mtid)) < 0)
  {

  }

  if((datatype_class = H5Tget_class(mtid)) < 0)
  {

  }

  for(int idx = 0; idx < rank; idx++)
  {
    nbr_elements *= dims[idx];
  }

  const QString filter_error = hdf_prepare_dataset(did);
  item_data->m_dataset->m_buf = malloc(static_cast<size_t>(datatype_size * nbr_elements));

  if(H5Dread(did, mtid, H5S_ALL, H5S_ALL, H5P_DEFAULT, item_data->m_dataset->m_buf) < 0)
  {
    free(item_data->m_dataset->m_buf);
    item_data->m_dataset->m_buf = nullptr;
    QMessageBox::warning(this, tr("Grid"), filter_error.isEmpty() ? hdf_filter_read_error(tr("Cannot read this dataset. Check its size and compression filter.")) : filter_error);
    qDebug() << item_data->m_dataset->m_path.c_str();
  }

  if(H5Dclose(did) < 0)
  {

  }

  if(H5Sclose(sid) < 0)
  {

  }

  if(H5Tclose(ftid) < 0)
  {

  }

  if(H5Tclose(mtid) < 0)
  {

  }

  if(H5Fclose(fid) < 0)
  {

  }

}


/////////////////////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::load_item_attribute
/////////////////////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::load_item_attribute(QTreeWidgetItem  *item)
{
  hid_t fid;
  hid_t aid;
  hid_t sid;
  hid_t ftid;
  hid_t mtid;
  hid_t obj_id = -1;
  int rank;
  size_t datatype_size;
  H5T_sign_t datatype_sign;
  H5T_class_t datatype_class;
  hsize_t dims[H5S_MAX_RANK];
  hsize_t nbr_elements = 1;
  H5O_info_t oinfo;
  ItemData *item_data = get_item_data(item);
  assert(item_data->m_kind == ItemData::Attribute);
  const char* path = item_data->m_dataset->m_path.c_str();
  const char* name = item_data->m_item_nm.c_str();

  //if not loaded, read buffer from file 
  if(item_data->m_dataset->m_buf != NULL)
  {
    return;
  }

  if(item_data->m_dataset->m_classic)
  {
    // The attribute's parent is "/" (global) or "/variable"; the element count is the product of m_dim.
    quint64 elements = 1;
    for(hsize_t dimension : item_data->m_dataset->m_dim) elements *= dimension;
    const quint64 bytes = elements * item_data->m_dataset->m_datatype_size;
    if(elements == 0 || bytes > grid_buffer_limit_bytes) return;
    item_data->m_dataset->m_buf = malloc(static_cast<size_t>(bytes));
    const QString error = item_data->m_dataset->m_buf
        ? nc_classic_read_attribute(classic_file_of(item_data), path, name, item_data->m_dataset->m_buf)
        : QObject::tr("Not enough memory to read this attribute.");
    if(!error.isEmpty())
    {
      free(item_data->m_dataset->m_buf);
      item_data->m_dataset->m_buf = nullptr;
      QMessageBox::warning(this, tr("Grid"), tr("Cannot read this attribute: %1").arg(error));
    }
    return;
  }

  if((fid = H5Fopen(item_data->m_file_name.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT)) < 0)
  {

  }

  //get object info
  if(H5Oget_info_by_name(fid, path, &oinfo, H5P_DEFAULT) < 0)
  {

  }

  // open the PARENT object 
  switch(oinfo.type)
  {
  case H5G_DATASET:
    if((obj_id = H5Dopen2(fid, path, H5P_DEFAULT)) < 0)
    {
    }
    break;
  case H5G_GROUP:
    if((obj_id = H5Gopen2(fid, path, H5P_DEFAULT)) < 0)
    {

    }
    break;
  case H5G_TYPE:
    if((obj_id = H5Topen2(fid, path, H5P_DEFAULT)) < 0)
    {

    }
    break;
  default:
    assert(0);
    break;
  }

  if((aid = H5Aopen(obj_id, name, H5P_DEFAULT)) < 0)
  {

  }

  if((sid = H5Aget_space(aid)) < 0)
  {

  }

  if((rank = H5Sget_simple_extent_dims(sid, dims, NULL)) < 0)
  {

  }

  if((ftid = H5Aget_type(aid)) < 0)
  {

  }

  if((mtid = H5Tget_native_type(ftid, H5T_DIR_DEFAULT)) < 0)
  {

  }

  if((datatype_size = H5Tget_size(mtid)) == 0)
  {

  }

  if((datatype_sign = H5Tget_sign(mtid)) < 0)
  {

  }

  if((datatype_class = H5Tget_class(mtid)) < 0)
  {

  }

  for(int idx = 0; idx < rank; idx++)
  {
    nbr_elements *= dims[idx];
  }

  item_data->m_dataset->m_buf = malloc(static_cast<size_t>(datatype_size * nbr_elements));

  if(H5Aread(aid, mtid, item_data->m_dataset->m_buf) < 0)
  {
    free(item_data->m_dataset->m_buf);
    item_data->m_dataset->m_buf = nullptr;
    QMessageBox::warning(this, tr("Grid"), tr("Cannot read this attribute."));
    qDebug() << item_data->m_dataset->m_path.c_str() << ":" << item_data->m_item_nm.c_str();
  }

  if(H5Aclose(aid) < 0)
  {

  }

  if(H5Sclose(sid) < 0)
  {

  }

  if(H5Tclose(ftid) < 0)
  {

  }

  if(H5Tclose(mtid) < 0)
  {

  }

  // close the PARENT object 
  switch(oinfo.type)
  {
  case H5G_DATASET:
    if(H5Dclose(obj_id) < 0)
    {
    };
    break;
  case H5G_GROUP:
    if(H5Gclose(obj_id) < 0)
    {
    };
    break;
  case H5G_TYPE:
    if(H5Tclose(obj_id) < 0)
    {
    };
    break;
  default:
    assert(0);
    break;
  }

  if(H5Fclose(fid) < 0)
  {

  }


}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::show_context_menu
///////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::show_context_menu(const QPoint &p)
{
  QTreeWidgetItem *item = static_cast <QTreeWidgetItem*> (itemAt(p));
  if(!item) return;
  setCurrentItem(item);
  ItemData *item_data = get_item_data(item);
  if(item_data&&item_data->m_grib) {
    QMenu menu(this);
    auto *properties=menu.addAction(tr("Properties..."));
    connect(properties,&QAction::triggered,this,&FileTreeWidget::show_item_properties);
    if(item_data->m_kind==ItemData::Variable) {
      const auto info=GribData::describe(item_data->m_source_path,item_data->m_object.object_id);
      auto *image=menu.addAction(tr("Image View"));image->setEnabled(info.error.isEmpty()&&info.can_image);image->setToolTip(info.disabled_reasons.value("image",info.error));connect(image,&QAction::triggered,this,[this,item_data]{m_main_window->add_image(item_data);});
      auto *export_png=menu.addAction(tr("Export As Image..."));export_png->setEnabled(image->isEnabled());connect(export_png,&QAction::triggered,this,&FileTreeWidget::export_item_as_image);
      auto *histogram=menu.addAction(tr("Histogram..."));histogram->setObjectName("actionGribHistogram");histogram->setEnabled(image->isEnabled());histogram->setToolTip(image->toolTip());connect(histogram,&QAction::triggered,this,[this,item_data]{m_main_window->add_histogram(item_data);});
      auto *texture=menu.addAction(tr("Texture Entropy Image"));texture->setObjectName("actionGribTextureEntropy");texture->setEnabled(image->isEnabled());texture->setToolTip(image->toolTip());connect(texture,&QAction::triggered,this,[this,item_data]{m_main_window->add_texture_image(item_data);});
      auto *section=menu.addAction(tr("Space-Time Section (Hovmöller)..."));section->setObjectName("actionSpaceTimeSectionContext");section->setEnabled(info.error.isEmpty()&&info.can_section);section->setToolTip(tr("Any two source axes; time labels retain reference cycle and step; no coordinate resampling"));connect(section,&QAction::triggered,this,[this,item_data]{m_main_window->add_space_time_section(item_data);});
      auto *decoded=menu.addAction(tr("Data Grid (decoded)..."));decoded->setToolTip(tr("ecCodes decoded values; no CF scale/offset applied"));connect(decoded,&QAction::triggered,this,&FileTreeWidget::add_grid);
      auto *stored=menu.addAction(tr("Quantized X Grid (reconstructed)..."));stored->setToolTip(tr("Validated reconstruction of quantized integers; unavailable for unsupported packing"));connect(stored,&QAction::triggered,this,&FileTreeWidget::add_stored_grid);
      auto *both=menu.addAction(tr("Decoded and Quantized X..."));connect(both,&QAction::triggered,this,&FileTreeWidget::add_both_grids);
    }
    auto *close=menu.addAction(tr("Close File"));
    connect(close,&QAction::triggered,this,[this,item]{m_main_window->close_file_of(item);});
    menu.exec(QCursor::pos());return;
  }
  QMenu menu;
  const bool has_dataset = item_data && item_data->m_dataset;
  const bool numeric = has_dataset && (item_data->m_dataset->m_datatype_class == H5T_INTEGER ||
                                       item_data->m_dataset->m_datatype_class == H5T_FLOAT);
  const bool variable = item_data && item_data->m_kind == ItemData::Variable;
  const bool attribute = item_data && item_data->m_kind == ItemData::Attribute;
  const size_t rank = has_dataset ? item_data->m_dataset->m_dim.size() : 0;

  QAction *action_properties = menu.addAction(tr("Properties..."));
  connect(action_properties, &QAction::triggered, this, &FileTreeWidget::show_item_properties);
  menu.addSeparator();

  // three grids: what the data means, what the file stores, and both side by side for comparison
  QAction *action_grid = menu.addAction(tr("Data Grid (decoded to real)..."));
  action_grid->setToolTip(tr("Values as the image view reads them: stored x scale_factor + add_offset"));
  action_grid->setEnabled(numeric && (variable || attribute));
  connect(action_grid, &QAction::triggered, this, &FileTreeWidget::add_grid);

  QAction *action_stored = menu.addAction(tr("Stored Grid (raw value)..."));
  action_stored->setToolTip(tr("Values exactly as written in the file, before scale_factor/add_offset"));
  action_stored->setEnabled(numeric && (variable || attribute));
  connect(action_stored, &QAction::triggered, this, &FileTreeWidget::add_stored_grid);

  QAction *action_both = menu.addAction(tr("Both Grids (decoded and raw)..."));
  action_both->setToolTip(tr("One table with the decoded column and the raw column side by side"));
  action_both->setEnabled(numeric && (variable || attribute));
  connect(action_both, &QAction::triggered, this, &FileTreeWidget::add_both_grids);

  QAction *action_image = menu.addAction(tr("Image View"));
  action_image->setEnabled(numeric && variable && rank >= 2);
  connect(action_image, &QAction::triggered, this, [this, item_data]() { m_main_window->add_image(item_data); });

  auto *action_texture = menu.addAction(tr("Texture Entropy Image"));
  action_texture->setEnabled(numeric && variable && rank >= 2);
  action_texture->setToolTip(tr("2D local 4x4 texture entropy image (bits), interpolated to the source size"));
  connect(action_texture, &QAction::triggered, this, [this, item_data] { m_main_window->add_texture_image(item_data); });
  auto *action_section=menu.addAction(tr("Space-Time Section (Hovmöller)..."));
  action_section->setObjectName("actionSpaceTimeSectionContext");action_section->setEnabled(numeric&&variable&&rank>=2);
  connect(action_section,&QAction::triggered,this,[this,item_data]{m_main_window->add_space_time_section(item_data);});

  QAction *action_histogram = menu.addAction(tr("Histogram..."));
  action_histogram->setToolTip(tr("One layer of this variable as a histogram, sampled at random "
                                  "(1 value in 20 by default)"));
  action_histogram->setEnabled(numeric && variable && rank >= 2);
  connect(action_histogram, &QAction::triggered, this, [this, item_data]() { m_main_window->add_histogram(item_data); });

  QAction *action_dimensions = menu.addAction(tr("Dimension Selection..."));
  action_dimensions->setEnabled(variable && rank > 2);
  connect(action_dimensions, &QAction::triggered, this, &FileTreeWidget::add_grid);

  QAction *action_time = menu.addAction(tr("Time values..."));
  const QString timePath = has_dataset ? QString::fromStdString(item_data->m_dataset->m_path) : QString();
  action_time->setEnabled(variable && rank == 1 &&
      (timePath.compare("time", Qt::CaseInsensitive) == 0 ||
       timePath.endsWith("/time", Qt::CaseInsensitive)));
  connect(action_time, &QAction::triggered, this, [this]() {
    if(m_main_window) m_main_window->show_time_values();
  });

  QAction *action_export_text = menu.addAction(tr("Export as text..."));
  action_export_text->setEnabled(numeric && (variable || attribute));
  connect(action_export_text, &QAction::triggered, this, &FileTreeWidget::export_item_as_text);

  QAction *action_export_image = menu.addAction(tr("Export As Image..."));
  action_export_image->setEnabled(variable && rank >= 2 && numeric);
  connect(action_export_image, &QAction::triggered, this, &FileTreeWidget::export_item_as_image);

  menu.addSeparator();
  menu.addAction(tr("Discrete Map View"))->setEnabled(false);
  menu.addAction(tr("Continuous Map View"))->setEnabled(false);
  QAction *action_vector=menu.addAction(tr("Vector View..."));
  action_vector->setEnabled(variable && numeric && rank>=2 &&
      (QString::fromLocal8Bit(item_data->m_file_name.c_str()).endsWith(".nc",Qt::CaseInsensitive) ||
       QString::fromLocal8Bit(item_data->m_file_name.c_str()).endsWith(".nc4",Qt::CaseInsensitive)));
  connect(action_vector,&QAction::triggered,this,[this,item_data] {
    QString file=QString::fromLocal8Bit(item_data->m_file_name.c_str());
    bool ok=false;
    QString vName=QInputDialog::getText(this,tr("Vector View"),tr("V component variable full path:"),QLineEdit::Normal,QString(),&ok);
    if(!ok || vName.isEmpty()) return;
    NcReader reader(file); int group,variable; int vGroup,vVariable;
    const std::string uPath=item_data->m_dataset->m_path;
    const std::string vPath=vName.toUtf8().toStdString();
    if(!reader.locate(uPath,group,variable) || !reader.locate(vPath,vGroup,vVariable)) {
      QMessageBox::warning(this,tr("Vector View"),tr("Both component variables must exist in this NetCDF file.")); return;
    }
    int uRank=0,vRank=0; nc_inq_varndims(group,variable,&uRank); nc_inq_varndims(vGroup,vVariable,&vRank);
    std::vector<int> uDims(static_cast<size_t>(uRank),0),vDims(static_cast<size_t>(vRank),0);
    if(uRank!=vRank || uRank<2 || nc_inq_vardimid(group,variable,uDims.data())!=NC_NOERR ||
       nc_inq_vardimid(vGroup,vVariable,vDims.data())!=NC_NOERR || uDims!=vDims) {
      QMessageBox::warning(this,tr("Vector View"),tr("U/V variables must have identical rank and dimension identities.")); return;
    }
    auto *view=new VectorView(file,uPath,vPath,item_data->m_dataset->m_dim);
    auto *sub=m_main_window->m_mdi_area->addSubWindow(view);
    sub->setAttribute(Qt::WA_DeleteOnClose); m_main_window->tag_view(sub,item_data); sub->resize(720,510); sub->show();
  });
  QAction *action_mesh=menu.addAction(tr("View mesh..."));
  action_mesh->setEnabled(variable && numeric &&
      (QString::fromLocal8Bit(item_data->m_file_name.c_str()).endsWith(".nc",Qt::CaseInsensitive) ||
       QString::fromLocal8Bit(item_data->m_file_name.c_str()).endsWith(".nc4",Qt::CaseInsensitive)));
  connect(action_mesh,&QAction::triggered,this,[this,item_data]{
    auto *view=new MeshView(QString::fromLocal8Bit(item_data->m_file_name.c_str()),item_data->m_dataset->m_path);
    auto *sub=m_main_window->m_mdi_area->addSubWindow(view);
    sub->setAttribute(Qt::WA_DeleteOnClose);m_main_window->tag_view(sub,item_data);sub->resize(760,560);sub->show();
  });
  menu.addAction(tr("Dump..."))->setEnabled(false);

  menu.addSeparator();
  QAction *action_close = menu.addAction(tr("Close View"));
  action_close->setEnabled(m_main_window && m_main_window->m_mdi_area->activeSubWindow());
  connect(action_close, &QAction::triggered, this, [this]() {
    if(m_main_window && m_main_window->m_mdi_area->activeSubWindow())
      m_main_window->m_mdi_area->activeSubWindow()->close();
  });
  QAction *action_close_all = menu.addAction(tr("Close All Views"));
  action_close_all->setEnabled(m_main_window && !m_main_window->m_mdi_area->subWindowList().isEmpty());
  connect(action_close_all, &QAction::triggered, this, [this]() {
    if(m_main_window) m_main_window->m_mdi_area->closeAllSubWindows();
  });
  menu.addSeparator();
  QAction *action_close_file = menu.addAction(tr("Close File"));
  action_close_file->setEnabled(m_main_window != nullptr);
  connect(action_close_file, &QAction::triggered, this, [this, item]() {
    if(m_main_window) m_main_window->close_file_of(item);
  });
  menu.exec(QCursor::pos());
}

void FileTreeWidget::show_item_properties()
{
  QTreeWidgetItem *item = currentItem();
  const QString report = item_properties_text(item);
  if(report.isEmpty()) return;

  //non-modal, so several objects can be compared; the text is selectable and Copy All puts the
  //whole report on the clipboard, ready to paste into a search, an issue or an LLM prompt
  QDialog *dialog = new QDialog(this);
  dialog->setObjectName("propertiesDialog");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Properties - %1").arg(item->text(0)));
  QPlainTextEdit *text = new QPlainTextEdit(report, dialog);
  text->setObjectName("propertiesText");
  text->setReadOnly(true);
  text->setLineWrapMode(QPlainTextEdit::NoWrap);
  text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  QPushButton *copy = buttons->addButton(tr("Copy All"), QDialogButtonBox::ActionRole);
  copy->setObjectName("propertiesCopyAll");
  copy->setToolTip(tr("Copy the whole report to the clipboard"));
  QLabel *copied = new QLabel(dialog);
  copied->setObjectName("propertiesCopied");
  connect(copy, &QPushButton::clicked, dialog, [text, copied] {
    QApplication::clipboard()->setText(text->toPlainText());
    copied->setText(QObject::tr("Copied %1 lines to the clipboard.").arg(text->blockCount()));
  });
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  QVBoxLayout *layout = new QVBoxLayout(dialog);
  layout->addWidget(text);
  QHBoxLayout *bottom = new QHBoxLayout;
  bottom->addWidget(copied, 1);
  bottom->addWidget(buttons);
  layout->addLayout(bottom);
  const QFontMetrics metrics(text->font());
  int widest = 0;
  for(const QString &line : report.split(QLatin1Char('\n'))) widest = qMax(widest, metrics.horizontalAdvance(line));
  dialog->resize(qBound(480, widest + 60, 1100), qBound(320, (report.count(QLatin1Char('\n')) + 1) * metrics.lineSpacing() + 110, 760));
  dialog->show();
}

QString FileTreeWidget::item_properties_text(QTreeWidgetItem *item) const
{
  ItemData *item_data = item ? get_item_data(item) : nullptr;
  if(!item_data) return QString();
  if(item_data->m_grib) {
    const QString target=item_data->m_object.object_id;
    const QString details=item_data->m_grib_details.isEmpty() ? GribData::properties(item_data->m_source_path,target) : item_data->m_grib_details;
    return QString("HDFStudio %1 Properties\nFile: %2\nObject: %3\nSession: %4\n\n").arg(QCoreApplication::applicationVersion(),item_data->m_source_path,target,item_data->m_object.source_session_id)+details;
  }

  QString shape = tr("Scalar");
  quint64 elements = 1;
  if(item_data->m_dataset) {
    const auto &dimensions = item_data->m_dataset->m_dim;
    if(!dimensions.empty()) {
      QStringList parts;
      for(hsize_t dimension : dimensions) {
        parts.append(QString::number(static_cast<qulonglong>(dimension)));
        elements *= static_cast<quint64>(dimension);
      }
      shape = QStringLiteral("[%1]").arg(parts.join(QStringLiteral(" x ")));
    }
  } else {
    shape = tr("Not applicable");
  }
  QString type = tr("Not applicable");
  if(item_data->m_dataset) {
    switch(item_data->m_dataset->m_datatype_class) {
    case H5T_INTEGER: type = tr("Integer"); break;
    case H5T_FLOAT: type = tr("Floating point"); break;
    case H5T_STRING: type = tr("String"); break;
    case H5T_COMPOUND: type = tr("Compound"); break;
    case H5T_ENUM: type = tr("Enum"); break;
    case H5T_ARRAY: type = tr("Array"); break;
    default: type = tr("Other"); break;
    }
  }
  const quint64 bytes = item_data->m_dataset
      ? elements * static_cast<quint64>(item_data->m_dataset->m_datatype_size) : 0;
  const QString kind = item_data->m_kind == ItemData::Root ? tr("File")
      : item_data->m_kind == ItemData::Group ? tr("Group")
      : item_data->m_kind == ItemData::Variable ? tr("Variable") : tr("Attribute");

  QStringList details = {
    tr("Name: %1").arg(QString::fromStdString(item_data->m_item_nm)),
    tr("Kind: %1").arg(kind),
    tr("Path: %1").arg(item_data->m_dataset
        ? QString::fromStdString(item_data->m_dataset->m_path)
        : QString::fromStdString(item_data->m_item_nm)),
    tr("Shape: %1").arg(shape),
    tr("Type: %1").arg(type),
    tr("Elements: %1").arg(item_data->m_dataset ? QString::number(elements) : tr("Not applicable")),
    tr("Uncompressed size: %1 bytes").arg(bytes)
  };
  QString objectPath=QString::fromStdString(item_data->m_dataset
      ? item_data->m_dataset->m_path : item_data->m_item_nm);
  if(item_data->m_kind==ItemData::Root || objectPath==QString::fromStdString(item_data->m_file_name))
    objectPath=QStringLiteral("/");
  // same detail lines as `hdf_cli props <file> <path>`
  const QString file = QString::fromLocal8Bit(item_data->m_file_name.c_str());
  auto summary=HdfProperties::inspect(file,objectPath);
  details.append(HdfProperties::describe(summary));
  //the file and object come first, so a pasted report says where it was read from
  details.prepend(tr("Object: %1").arg(objectPath));
  details.prepend(tr("File: %1").arg(QDir::toNativeSeparators(file)));
  details.prepend(tr("HDFStudio %1 Properties").arg(QCoreApplication::applicationVersion()));
  return details.join(QLatin1Char('\n'));
}

void FileTreeWidget::export_item_as_text()
{
  QTreeWidgetItem *item = currentItem();
  ItemData *item_data = item ? get_item_data(item) : nullptr;
  if(!item_data || !item_data->m_dataset) return;
  const bool is_attribute=item_data->m_kind==ItemData::Attribute;
  if(is_attribute) load_item_attribute(item);
  if(is_attribute && !item_data->m_dataset->m_buf) return;
  if(item_data->m_dataset->m_datatype_class != H5T_INTEGER &&
     item_data->m_dataset->m_datatype_class != H5T_FLOAT) {
    QMessageBox::warning(this, tr("Export as text"), tr("Only integer and floating-point data can be exported."));
    return;
  }

  const QString output = QFileDialog::getSaveFileName(this, tr("Export as text"),
      QString::fromStdString(item_data->m_item_nm) + QStringLiteral(".txt"),
      tr("Text files (*.txt);;CSV files (*.csv);;All files (*)"));
  if(output.isEmpty()) return;

  QSaveFile file(output);
  if(!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, tr("Export as text"), file.errorString());
    return;
  }
  if(is_attribute) {
    const hdf_dataset_t *dataset=item_data->m_dataset;
    quint64 count=1;
    for(hsize_t dimension:dataset->m_dim) {
      if(dimension && count>std::numeric_limits<quint64>::max()/dimension) {
        file.cancelWriting();
        QMessageBox::warning(this,tr("Export as text"),tr("Attribute size exceeds the supported export limit."));
        return;
      }
      count*=dimension;
    }
    if(count>16*1024*1024) {
      file.cancelWriting();
      QMessageBox::warning(this,tr("Export as text"),tr("Attribute export is limited to 16 million values."));
      return;
    }
    QTextStream stream(&file);
    stream.setRealNumberPrecision(17);
    for(quint64 i=0;i<count;++i) {
      if(i) stream << (dataset->m_dim.empty() || i%dataset->m_dim.back() ? ',' : '\n');
      if(dataset->m_datatype_class==H5T_FLOAT) {
        if(dataset->m_datatype_size==sizeof(float)) stream << static_cast<const float*>(dataset->m_buf)[i];
        else stream << static_cast<const double*>(dataset->m_buf)[i];
      } else {
        switch(dataset->m_datatype_size) {
          case 1:stream << int(static_cast<const qint8*>(dataset->m_buf)[i]);break;
          case 2:stream << static_cast<const qint16*>(dataset->m_buf)[i];break;
          case 4:stream << static_cast<const qint32*>(dataset->m_buf)[i];break;
          case 8:stream << static_cast<const qint64*>(dataset->m_buf)[i];break;
          default:file.cancelWriting();QMessageBox::warning(this,tr("Export as text"),tr("Unsupported integer size."));return;
        }
      }
    }
    stream << '\n';
    stream.flush();
    if(stream.status()!=QTextStream::Ok || !file.commit())
      QMessageBox::warning(this,tr("Export as text"),file.errorString());
    return;
  }
  QProgressDialog progress(tr("Exporting numeric data..."),tr("Cancel"),0,100,this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(500);
  const QString objectPath=QString::fromStdString(item_data->m_dataset->m_path);
  const auto result=HdfTextExport::write(QString::fromLocal8Bit(item_data->m_file_name.c_str()),
      objectPath,file,65536,[&progress](quint64 written,quint64 total) {
        if(progress.wasCanceled()) return false;
        progress.setValue(total?int((written*100)/total):100);
        QCoreApplication::processEvents();
        return !progress.wasCanceled();
      });
  if(!result.ok || !file.commit()) {
    file.cancelWriting();
    QMessageBox::warning(this,tr("Export as text"),result.error.isEmpty()?file.errorString():result.error);
    return;
  }
}

void FileTreeWidget::export_item_as_image()
{
  QTreeWidgetItem *item = currentItem();
  ItemData *item_data = item ? get_item_data(item) : nullptr;
  if(!item_data || item_data->m_kind != ItemData::Variable || (!item_data->m_grib&&!item_data->m_dataset)) return;
  m_main_window->export_image(item_data, this);
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::add_grid
///////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::add_grid()
{
  open_grid(GridDecoded);
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::add_stored_grid
//same buffer, but the table shows it as stored in the file (no scale_factor/add_offset)
///////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::add_stored_grid()
{
  open_grid(GridStored);
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::add_both_grids
//one table with a decoded column and the raw column next to it, for comparing them
///////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::add_both_grids()
{
  open_grid(GridBoth);
}

// Shared by the three grid commands: load the buffer of the current tree item, then open a table.
void FileTreeWidget::open_grid(GridValueMode mode)
{
  QTreeWidgetItem *item = static_cast <QTreeWidgetItem*> (currentItem());
  if(!item) return;
  ItemData *item_data = get_item_data(item);
  if(item_data&&item_data->m_grib){if(item_data->m_kind==ItemData::Variable)m_main_window->add_table(item_data,int(mode));return;}
  if(!item_data || !item_data->m_dataset) return;
  if(item_data->m_dataset->m_datatype_class != H5T_INTEGER &&
    item_data->m_dataset->m_datatype_class != H5T_FLOAT)
  {
    return;
  }
  if(item_data->m_kind == ItemData::Variable)
  {
    this->load_item(item);
  }
  else if(item_data->m_kind == ItemData::Attribute)
  {
    this->load_item_attribute(item);
  }
  if(item_data->m_dataset->m_buf) m_main_window->add_table(item_data, int(mode));
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow: Layer toolbar and menu
// Synced image followers route to their anchor; independent views retain active-view routing.
///////////////////////////////////////////////////////////////////////////////////////

LayerNavigation *MainWindow::current_layer_view() const
{
  // currentSubWindow() stays valid while the focus is on the toolbar, activeSubWindow() may not
  QMdiSubWindow *sub = m_mdi_area->currentSubWindow();
  if(!sub || !sub->widget()) return nullptr;
  if(auto *texture = qobject_cast<TextureEntropyView*>(sub->widget()))
    if(auto *source = texture->source_view()) return m_workspace_sync ? m_workspace_sync->layer_navigation_target(source) : source;
  if(m_workspace_sync)if(auto *raster=qobject_cast<RasterView*>(sub->widget()))
    return m_workspace_sync->layer_navigation_target(raster);
  return dynamic_cast<LayerNavigation *>(sub->widget());
}

void MainWindow::watch_current_view(QMdiSubWindow *)
{
  if(m_layer_playback && m_layer_playback->is_playing()) {
    auto *view=current_layer_view();
    if(!view || dynamic_cast<QWidget*>(view)!=m_layer_playback->target() || view->navigation_axis!=m_layer_playback->axis())m_layer_playback->stop();
  }
  disconnect(m_layer_connection);m_layer_connection={};
  m_layer_view = nullptr;
  m_layer_combo_view = nullptr;   // another view: always refill the combos
  if(auto *view=dynamic_cast<QWidget*>(current_layer_view()))
  {
    m_layer_view = view;
    // ChildWindow and RasterView both declare the signal layer_changed()
    m_layer_connection=connect(m_layer_view, SIGNAL(layer_changed()), this, SLOT(update_layer_controls()));
  }
  update_layer_controls();
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::update_window_menu
//rebuilt each time the menu opens, so the list always matches the open views (creation order)
///////////////////////////////////////////////////////////////////////////////////////

void MainWindow::update_window_menu()
{
  qDeleteAll(m_menu_windows->findChildren<QActionGroup *>(QString(), Qt::FindDirectChildrenOnly));
  m_menu_windows->clear();
  m_menu_windows->addAction(m_action_cascade);
  m_menu_windows->addAction(m_action_tile);
  m_menu_windows->addAction(m_action_close_all);
  m_menu_windows->addAction(m_action_compare);
  m_menu_windows->addAction(m_action_hide_infobar);
  const QList<QMdiSubWindow *> windows = m_mdi_area->subWindowList(QMdiArea::CreationOrder);
  m_action_cascade->setEnabled(!windows.isEmpty());
  m_action_tile->setEnabled(!windows.isEmpty());
  m_action_close_all->setEnabled(!windows.isEmpty());
  if(windows.isEmpty()) return;
  m_menu_windows->addSeparator();
  QActionGroup *group = new QActionGroup(m_menu_windows);
  for(int i = 0; i < windows.size(); ++i)
  {
    QMdiSubWindow *sub = windows.at(i);
    QString title = sub->windowTitle();
    title.replace(QLatin1Char('&'), QStringLiteral("&&"));
    const QString text = i < 9 ? QStringLiteral("&%1 %2").arg(i + 1).arg(title)
                               : QStringLiteral("%1 %2").arg(i + 1).arg(title);
    QAction *action = m_menu_windows->addAction(text);
    action->setObjectName(QStringLiteral("actionWindow%1").arg(i + 1));
    action->setCheckable(true);
    action->setChecked(sub == m_mdi_area->currentSubWindow());
    action->setActionGroup(group);
    connect(action, &QAction::triggered, this, [this, sub = QPointer<QMdiSubWindow>(sub)] {
      if(sub) m_mdi_area->setActiveSubWindow(sub);
    });
  }
}

MainWindow::~MainWindow()
{
  m_layer_playback->stop();
  qApp->removeEventFilter(this);
  disconnect(m_mdi_area, nullptr, this, nullptr);
  disconnect(m_layer_connection);
  // Derived widgets outlive this destructor body; their source may emit while QWidget deletes them.
  for(auto *view : findChildren<TextureEntropyView*>()) disconnect(view, nullptr, this, nullptr);
  for(auto *view : findChildren<HovmollerView*>()) disconnect(view, nullptr, this, nullptr);
  delete m_compare_panel;m_compare_panel=nullptr;
  delete m_workspace_sync;m_workspace_sync=nullptr;
}

void MainWindow::cascade_windows()
{
  const QList<QMdiSubWindow *> windows = m_mdi_area->subWindowList(QMdiArea::CreationOrder);
  if(windows.isEmpty()) return;
  QMdiSubWindow *active = m_mdi_area->currentSubWindow();
  const QRect area = m_mdi_area->viewport()->rect();
  const int step = qMax(20, windows.first()->style()->pixelMetric(QStyle::PM_TitleBarHeight, nullptr, windows.first()) + 6);
  const int width = qMax(320, area.width() * 2 / 3);
  const int height = qMax(240, area.height() * 2 / 3);
  // offsets that still fit in the area; wrap to the top-left corner after that
  const int fit = qMax(1, qMin((area.width() - width) / step, (area.height() - height) / step) + 1);
  for(int i = 0; i < windows.size(); ++i)
  {
    QMdiSubWindow *sub = windows.at(i);
    if(sub->isMinimized() || sub->isMaximized()) sub->showNormal();
    const int offset = (i % fit) * step;
    sub->setGeometry(area.left() + offset, area.top() + offset, width, height);
    sub->raise();
  }
  if(active) m_mdi_area->setActiveSubWindow(active);
}

void MainWindow::step_layer(LayerStep step)
{
  m_layer_playback->stop();
  LayerNavigation *view = current_layer_view();
  if(!view || view->layer_axis_count() == 0) return;
  const int axis = view->navigation_axis;
  view->set_layer_index(axis, layer_step_target(view->layer_index(axis), view->layer_count(axis), step));
  update_layer_controls();
}

void MainWindow::go_to_layer()
{
  m_layer_playback->stop();
  LayerNavigation *view = current_layer_view();
  if(!view || view->layer_axis_count() == 0) return;
  const int axis = view->navigation_axis;
  const int count = view->layer_count(axis);
  bool ok = false;
  const int number = QInputDialog::getInt(this, tr("Go to Layer"),
      tr("Layer of %1 (1 - %2):").arg(view->layer_axis_name(axis)).arg(count),
      view->layer_index(axis) + 1, 1, count, 1, &ok);
  if(!ok) return;
  view->set_layer_index(axis, number - 1);
  update_layer_controls();
}

void MainWindow::update_layer_controls()
{
  LayerNavigation *view = current_layer_view();
  const int axes = view ? view->layer_axis_count() : 0;
  if(axes>0)view->navigation_axis=layer_clamp(view->navigation_axis,axes);
  const bool can_play=axes>0 && view->layer_count(view->navigation_axis)>=2;
  m_action_layer_play->setEnabled(can_play);
  if(m_layer_playback->is_playing() && (!can_play || dynamic_cast<QWidget*>(view)!=m_layer_playback->target() || view->navigation_axis!=m_layer_playback->axis()))m_layer_playback->stop();
  const QSignalBlocker play_blocker(m_action_layer_play);
  m_action_layer_play->setChecked(m_layer_playback->is_playing());
  const QList<QAction *> actions = {m_action_layer_next, m_action_layer_previous, m_action_layer_last,
                                    m_action_layer_first, m_action_layer_goto};
  if(axes == 0)
  {
    for(QAction *action : actions) action->setEnabled(false);
    m_layer_axis_combo->clear();
    m_layer_index_combo->clear();
    m_layer_axis_combo->setEnabled(false);
    m_layer_index_combo->setEnabled(false);
    m_layer_label->setText(tr("No layers"));
    m_layer_combo_view = nullptr;
    m_layer_combo_axis = -1;
    m_layer_combo_count = -1;
    return;
  }

  view->navigation_axis = layer_clamp(view->navigation_axis, axes);
  const int axis = view->navigation_axis;
  const int count = view->layer_count(axis);
  const int index = view->layer_index(axis);

  // refill the combos only for another view, axis or length: a long time axis is not rebuilt per step
  const QSignalBlocker block_axis(m_layer_axis_combo);
  const QSignalBlocker block_index(m_layer_index_combo);
  if(m_layer_combo_view != view || m_layer_combo_count != count || m_layer_combo_axis != axis)
  {
    m_layer_axis_combo->clear();
    for(int a = 0; a < axes; ++a)
      m_layer_axis_combo->addItem(tr("%1 (%2)").arg(view->layer_axis_name(a)).arg(view->layer_count(a)));
    m_layer_index_combo->clear();
    QStringList numbers;
    if(!dynamic_cast<HovmollerView*>(view)||count<=10000)for(int i = 1; i <= count; ++i) numbers << QString::number(i);
    m_layer_index_combo->addItems(numbers);
    m_layer_combo_view = view;
    m_layer_combo_axis = axis;
    m_layer_combo_count = count;
  }
  m_layer_axis_combo->setCurrentIndex(axis);
  m_layer_index_combo->setCurrentIndex(count>0?index:-1);
  m_layer_axis_combo->setEnabled(true);
  m_layer_index_combo->setEnabled(count>1&&(!dynamic_cast<HovmollerView*>(view)||count<=10000));
  m_action_layer_next->setEnabled(index < count - 1);
  m_action_layer_last->setEnabled(index < count - 1);
  m_action_layer_previous->setEnabled(index > 0);
  m_action_layer_first->setEnabled(index > 0);
  m_action_layer_goto->setEnabled(count > 1);
  m_layer_label->setText(count>0?tr("Layer %1 of %2").arg(index + 1).arg(count):tr("No frames on this axis"));
  if(dynamic_cast<HovmollerView*>(view)){m_layer_label->setText(count>0?tr("Position %1 of %2 (index0=%3)").arg(index+1).arg(count).arg(index):tr("No toolbar frames (axis exceeds int range)"));m_action_layer_play->setToolTip(tr("Sweep %1; wait for each section to become Ready").arg(view->layer_axis_name(axis)));}
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::add_table
///////////////////////////////////////////////////////////////////////////////////////

void MainWindow::add_table(ItemData *item_data, int mode)
{
  if(item_data&&item_data->m_grib) {
    auto *view=new GribTableView(item_data->m_source_path,item_data->m_object.object_id,mode);
    auto *sub=m_mdi_area->addSubWindow(view);sub->setAttribute(Qt::WA_DeleteOnClose);tag_view(sub,item_data);
    const QSize available=m_mdi_area->viewport()->size();sub->resize(qMin(900,available.width()),qMin(580,available.height()));sub->show();return;
  }
  QWidget *window = create_legacy_grid(this, item_data, GridValueMode(mode));
  QMdiSubWindow *sub = m_mdi_area->addSubWindow(window);
  sub->setAttribute(Qt::WA_DeleteOnClose);
  tag_view(sub, item_data);
  const QSize available = m_mdi_area->viewport()->size();
  sub->resize(qMin(900, available.width()), qMin(580, available.height()));
  window->show();
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::add_image
///////////////////////////////////////////////////////////////////////////////////////

void MainWindow::add_image(ItemData *item)
{
  if(item&&item->m_grib&&item->m_kind==ItemData::Variable) {
    const auto info=GribData::describe(item->m_source_path,item->m_object.object_id);
    if(!info.error.isEmpty()||!info.can_image){statusBar()->showMessage(info.error.isEmpty()?info.disabled_reasons.value("image"):info.error);return;}
    auto *view=new RasterView(item->m_source_path,item->m_object.object_id,info);auto *sub=m_mdi_area->addSubWindow(view);sub->setAttribute(Qt::WA_DeleteOnClose);tag_view(sub,item);
    const auto available=m_mdi_area->viewport()->size();sub->resize(qMin(1100,available.width()),qMin(700,available.height()));sub->show();return;
  }
  if(!item || !item->m_dataset || item->m_kind != ItemData::Variable || item->m_dataset->m_dim.size() < 2 ||
     (item->m_dataset->m_datatype_class != H5T_INTEGER && item->m_dataset->m_datatype_class != H5T_FLOAT)) return;
  auto view = new RasterView(item->m_file_name, item->m_dataset->m_path, item->m_dataset->m_dim);
  auto sub = m_mdi_area->addSubWindow(view);
  sub->setAttribute(Qt::WA_DeleteOnClose);
  tag_view(sub, item);
  sub->resize(720, 510);
  sub->show();
}

void MainWindow::show_texture_entropy() {
  if(auto *sub = m_mdi_area->currentSubWindow()) {
    if(auto *source = qobject_cast<RasterView*>(sub->widget())) {
      auto *view = new TextureEntropyView(source);
      connect(view, &TextureEntropyView::layer_changed, this, &MainWindow::update_layer_controls);
      auto *derived = m_mdi_area->addSubWindow(view); derived->setAttribute(Qt::WA_DeleteOnClose);
      derived->setProperty("sourceItem", sub->property("sourceItem"));
      derived->resize(820, 660); derived->show(); return;
    }
  }
  if(m_tree->currentItem()) add_texture_image(get_item_data(m_tree->currentItem()));
}

void MainWindow::add_texture_image(ItemData *item) {
  if(!item||item->m_kind!=ItemData::Variable)return;
  if(!item->m_grib&&(!item->m_dataset||item->m_dataset->m_dim.size()<2||
      (item->m_dataset->m_datatype_class!=H5T_INTEGER&&item->m_dataset->m_datatype_class!=H5T_FLOAT)))return;
  std::optional<DataSource::ArrayInfo> grib_info;
  if(item->m_grib){grib_info=GribData::describe(item->m_source_path,item->m_object.object_id);if(!grib_info->error.isEmpty()||!grib_info->can_image){statusBar()->showMessage(grib_info->error.isEmpty()?grib_info->disabled_reasons.value("image"):grib_info->error);return;}}
  TextureEntropyView *view = nullptr;
  if(auto *sub = m_mdi_area->currentSubWindow()) {
    if(sub->property("sourceItem").value<quintptr>() == reinterpret_cast<quintptr>(item))
      if(auto *source = qobject_cast<RasterView*>(sub->widget())) view = new TextureEntropyView(source);
  }
  if(!view){if(grib_info)view=new TextureEntropyView(item->m_source_path,item->m_object.object_id,*grib_info);else view=new TextureEntropyView(item->m_file_name,item->m_dataset->m_path,item->m_dataset->m_dim);}
  connect(view, &TextureEntropyView::layer_changed, this, &MainWindow::update_layer_controls);
  auto *sub = m_mdi_area->addSubWindow(view); sub->setAttribute(Qt::WA_DeleteOnClose);
  tag_view(sub, item); sub->resize(820, 660); sub->show();
}

void MainWindow::add_space_time_section(ItemData *item) {
  if(property("sectionShutdownPending").toBool()||!item||item->m_kind!=ItemData::Variable)return;
  if(item->m_grib){const auto info=GribData::describe(item->m_source_path,item->m_object.object_id);if(!info.error.isEmpty()||!info.can_section){statusBar()->showMessage(info.error.isEmpty()?tr("Sections require a rectangular source grid"):info.error);return;}}
  else if(!item->m_dataset||item->m_dataset->m_dim.size()<2||(item->m_dataset->m_datatype_class!=H5T_INTEGER&&item->m_dataset->m_datatype_class!=H5T_FLOAT))return;
  auto *view=new HovmollerView(item->m_grib?item->m_source_path:QString::fromLocal8Bit(item->m_file_name.c_str()),item->m_grib?item->m_object.object_id:QString::fromStdString(item->m_dataset->m_path));
  connect(view,&HovmollerView::stop_playback_requested,m_layer_playback,&LayerPlayback::stop);
  auto *sub=m_mdi_area->addSubWindow(view);sub->setAttribute(Qt::WA_DeleteOnClose);tag_view(sub,item);
  const auto available=m_mdi_area->viewport()->size();sub->resize(std::min(1100,available.width()),std::min(800,available.height()));sub->showMaximized();
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::add_histogram
//one layer of the variable, sampled at random (initial 1 in 20) and drawn as bars
///////////////////////////////////////////////////////////////////////////////////////

void MainWindow::add_histogram(ItemData *item)
{
  if(!item||item->m_kind!=ItemData::Variable)return;
  HistogramView *view=nullptr;
  if(item->m_grib) {
    const auto info=GribData::describe(item->m_source_path,item->m_object.object_id);
    if(!info.error.isEmpty()||!info.can_image){statusBar()->showMessage(info.error.isEmpty()?info.disabled_reasons.value("image"):info.error);return;}
    view=new HistogramView(item->m_source_path,item->m_object.object_id,info);
  }else {
    if(!item->m_dataset)return;
    view=new HistogramView(item->m_file_name,item->m_dataset->m_path,item->m_dataset->m_dim);
  }
  auto sub = m_mdi_area->addSubWindow(view);
  sub->setAttribute(Qt::WA_DeleteOnClose);
  tag_view(sub, item);
  sub->resize(760, 560);
  sub->show();
}

void MainWindow::export_image(ItemData *item, QWidget *parent)
{
  if(!item || (!item->m_grib&&!item->m_dataset)) return;
  const QString output = QFileDialog::getSaveFileName(parent, tr("Export As Image"),
      QString::fromStdString(item->m_item_nm) + QStringLiteral(".png"), tr("PNG images (*.png)"));
  if(output.isEmpty()) return;
  if(item->m_grib)for(const auto &source:property("openSourceFiles").toStringList())if(slice_same_file(output,source)){QMessageBox::warning(parent,tr("Export As Image"),tr("Cannot overwrite an open source dataset."));return;}

  RasterView *view=nullptr;
  // Prefer this dataset's current image (layer, palette, Mask and orientation).
  auto windows=m_mdi_area->subWindowList();
  if(auto *active=m_mdi_area->activeSubWindow()) {windows.removeAll(active);windows.prepend(active);}
  for(auto *sub:windows) {
    if(sub->property("sourceItem").value<quintptr>()==reinterpret_cast<quintptr>(item))
      if(auto *raster=qobject_cast<RasterView*>(sub->widget())) {view=raster;break;}
  }
  std::unique_ptr<RasterView> temporary;
  if(!view) {
    if(item->m_grib)temporary=std::make_unique<RasterView>(item->m_source_path,item->m_object.object_id,GribData::describe(item->m_source_path,item->m_object.object_id));
    else temporary=std::make_unique<RasterView>(item->m_file_name,item->m_dataset->m_path,item->m_dataset->m_dim);
    view=temporary.get();
  }
  const QImage image=view->image_for_export();
  QSaveFile destination(output);
  const bool saved=!image.isNull() && destination.open(QIODevice::WriteOnly) &&
      image.save(&destination,"PNG") && destination.commit();
  if(!saved) QMessageBox::warning(parent, tr("Export As Image"), tr("Could not save the image."));
}
