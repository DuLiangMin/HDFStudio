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
#include <QSaveFile>
#include <QPainter>
#include <string>
#include <cassert>
#include <vector>
#include <algorithm>
#include "hdf_explorer.hpp"
#include "raster_view.hpp"
#include "nc_reader.hpp"
#include "nc_classic.hpp"
#include "properties_inspector.hpp"
#include "text_export.hpp"
#include "vector_view.hpp"
#include "mesh_view.hpp"

static const char app_name[] = "HDFStudio";

/////////////////////////////////////////////////////////////////////////////////////////////////////
//main
/////////////////////////////////////////////////////////////////////////////////////////////////////

// Builds the startup waiting box. A plain frameless widget is used instead of QSplashScreen:
// QSplashScreen::event() handles QEvent::Show by spinning in waitForWidgetMapped() until the
// native window is mapped, capped at 1000 ms (10 ms Sleep per round). That mapped check never
// succeeded from inside the handler on this deployment, so every show() burned the whole cap and
// the box was closed before it ever painted. Measured: constructed 165 ms, show() returned
// 1183 ms; in the same process a plain top-level widget mapped in 135 ms, so the second was the
// wait itself, not window creation. Kept out of the main() test guard so tests can inspect it.
// Qt's own docs also advise Qt::WindowStaysOnTopHint, which WA_ShowWithoutActivating needs here.
QWidget *create_startup_splash()
{
  QWidget *splash = new QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint |
                                         Qt::WindowStaysOnTopHint);
  splash->setObjectName("startupSplash");
  splash->setAttribute(Qt::WA_ShowWithoutActivating);
  auto *splash_layout = new QVBoxLayout(splash);
  splash_layout->setContentsMargins(28, 20, 28, 20);
  splash_layout->setSpacing(6);
  auto *splash_title = new QLabel(QStringLiteral("HDFStudio"), splash);
  QFont splash_font = splash_title->font();
  splash_font.setPointSize(splash_font.pointSize() + 9);
  splash_font.setBold(true);
  splash_title->setFont(splash_font);
  auto *splash_line = new QLabel(QStringLiteral("Unleash Your NetCDF Power."), splash);
  splash_line->setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
  splash_layout->addWidget(splash_title);
  splash_layout->addWidget(splash_line);
  splash->setStyleSheet(QStringLiteral(
    "#startupSplash { background: #fafafa; border: 2px solid #3d99a4; }"
    "QLabel { color: #2a4752; }"));
  splash->adjustSize();
  if(QScreen *screen = QGuiApplication::primaryScreen()) {
    const QRect area = screen->availableGeometry();
    splash->move(area.center() - QPoint(splash->width() / 2, splash->height() / 2));
  }
  return splash;
}

#ifndef HDF_EXPLORER_TEST_BUILD
int main(int argc, char *argv[])
{
  Q_INIT_RESOURCE(hdf_explorer);
  QApplication app(argc, argv);
  QCoreApplication::setApplicationVersion("0.10.27");
  QCoreApplication::setApplicationName(app_name);
#if QT_VERSION >= 0x050000
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({"preview", "Export a numeric dataset preview.", "variable"});
  parser.addOption({"png", "Preview output PNG.", "path"});
  parser.addOption({"layer", "First leading axis index (zero-based).", "index", "0"});
  parser.addPositionalArgument("file", "The file to open.");
  parser.process(app);
  const QStringList args = parser.positionalArguments();
#endif

#if QT_VERSION >= 0x050000
  // The preview batch mode stays silent; the interactive start shows the waiting box until the
  // main window is ready. QSplashScreen is deliberately avoided (see create_startup_splash()).
  QWidget *splash = nullptr;
  // The window is ready in about 200 ms now, so the splash is held for a short minimum time to
  // be readable instead of flashing for a few frames.
  QElapsedTimer splash_clock;
  const int minimum_splash_ms = 1000;
  if(!parser.isSet("preview")) {
    splash = create_startup_splash();
    splash->show();
    splash_clock.start();
    app.processEvents();
  }
#endif
  MainWindow window;
#if QT_VERSION >= 0x050000
  if(args.size())
  {
    QString file_name = args.at(0);
    if(window.read_file(file_name)!=0) return 2;
    if(parser.isSet("preview")) {
      auto tree=window.findChild<FileTreeWidget*>();
      const auto entries=tree->findItems(parser.value("preview"),Qt::MatchExactly|Qt::MatchRecursive);
      if(entries.size()!=1 || !parser.isSet("png")) return 3;
      emit tree->itemDoubleClicked(entries.first(),0);
      app.processEvents();
      auto canvas=window.findChild<QGraphicsView*>();
      if(!canvas) return 4;
      auto widget=canvas->parentWidget();
      auto spin=widget->findChild<QSpinBox*>();
      bool ok=false; int layer=parser.value("layer").toInt(&ok);
      if(!ok || layer<0 || layer>(spin?spin->maximum():0)) return 5;
      if(spin) spin->setValue(layer);
      auto items=canvas->scene()->items();
      if(items.size()!=1) return 6;
      auto raster=qgraphicsitem_cast<QGraphicsPixmapItem*>(items.front());
      auto output=parser.value("png");
      if(!raster || !raster->pixmap().save(output,"PNG")) return 7;
      QJsonObject report;
      for(auto key:{"minimum","maximum","reader"}) report[key]=QJsonValue::fromVariant(widget->property(key));
      report["width"]=raster->pixmap().width(); report["height"]=raster->pixmap().height();
      QFile json(output+".json"); if(!json.open(QIODevice::WriteOnly)) return 8;
      return json.write(QJsonDocument(report).toJson())<0 ? 9 : 0;
    }
    // Interactive start: every file named on the command line is opened, not only the first.
    for(int i=1;i<args.size();++i) window.read_file(args.at(i));
  }
#endif
  // Hold the splash for the remaining minimum time before the main window covers it, so the
  // message is actually legible rather than a single frame.
  if(splash) {
    while(splash_clock.elapsed() < minimum_splash_ms)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  window.showMaximized();
  if(splash) {
    splash->close();
    splash->deleteLater();
    splash = nullptr;
  }
  return app.exec();
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////
//hdf_dataset_t (common definition for HDF5 dataset and attribute)
//a HDF dataset is defined here simply has a:
// 1) location in memory to store dataset data
// 2) an array of dimensions
// 3) a full name path to open using a file id
// 4) size, sign and class of datatype
// the array of dimensions (of HDF defined type ' hsize_t') is defined in iteration
// the data buffer and datatype sizes are stored on per load variable 
// from tree using the HDF API from item input
/////////////////////////////////////////////////////////////////////////////////////////////////////

class hdf_dataset_t
{
public:
  hdf_dataset_t(const char* path, const std::vector< hsize_t> &dim,
    size_t size, H5T_sign_t sign, H5T_class_t datatype_class) :
    m_path(path),
    m_dim(dim),
    m_datatype_size(size),
    m_datatype_sign(sign),
    m_datatype_class(datatype_class)
  {
    m_buf = NULL;
  }

  ~hdf_dataset_t()
  {
    free(m_buf);
  }

  void store(void *buf)
  {
    m_buf = buf;
  }
  std::string m_path;
  std::vector<hsize_t> m_dim;

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //needed to access HDF5 buffer data
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  size_t m_datatype_size;
  H5T_sign_t m_datatype_sign;
  H5T_class_t  m_datatype_class;
  void *m_buf;

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //NetCDF-3 (CDF-1/2/5) items are read through netCDF-C instead of HDF5.
  //m_slice_mode: for rank > 2 m_buf holds only the plane picked by the layer selectors, so a
  //multi-gigabyte variable is never loaded whole into the grid.
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  bool m_classic = false;
  bool m_slice_mode = false;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ItemData
//used by QTreeWidgetItem::setData to store custom data
//contains information to load a dataset from an HDF file:
//1) the file name
//2) the dataset name
//3) that's all :-)
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ItemData
{
public:
  enum ItemKind
  {
    Root,
    Group,
    Variable,
    Attribute
  };

  ItemData(ItemKind kind, const std::string& file_name, const std::string& item_nm, hdf_dataset_t *dataset) :
    m_file_name(file_name),
    m_item_nm(item_nm),
    m_kind(kind),
    m_dataset(dataset)
  {
  }
  ~ItemData()
  {
    delete m_dataset;
  }
  std::string m_file_name;  // (Root/Variable/Group/Attribute) file name
  std::string m_item_nm; // (Root/Variable/Group/Attribute ) item name to display on tree
  ItemKind m_kind; // (Root/Variable/Group/Attribute) type of item 
  hdf_dataset_t *m_dataset; // (Variable) HDF variable to display
};

Q_DECLARE_METATYPE(ItemData*);

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

static const quint64 grid_buffer_limit_bytes = 2ULL * 1024 * 1024 * 1024;

static QString classic_file_of(const ItemData *item_data)
{
  return QString::fromLocal8Bit(item_data->m_file_name.c_str());
}

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

// Allocates the grid buffer on first use and fills it. A variable with more than two dimensions
// keeps only the plane selected by `layers` (one index per leading axis); anything else is read whole.
static QString classic_load_variable(ItemData *item_data, const std::vector<int> &layers)
{
  hdf_dataset_t *dataset = item_data->m_dataset;
  const size_t rank = dataset->m_dim.size();
  std::vector<size_t> start(rank, 0), count(rank, 1);
  quint64 elements = 1;
  for(size_t axis = 0; axis < rank; ++axis)
  {
    const bool leading = rank > 2 && axis < rank - 2;
    count[axis] = leading ? 1 : static_cast<size_t>(dataset->m_dim[axis]);
    if(leading) start[axis] = axis < layers.size() ? static_cast<size_t>(std::max(0, layers[axis])) : 0;
    elements *= count[axis];
  }
  if(elements == 0) return QObject::tr("This variable has no values yet.");
  if(!dataset->m_buf)
  {
    if(elements > grid_buffer_limit_bytes / std::max<size_t>(1, dataset->m_datatype_size))
      return QObject::tr("The values to display need more than 2 GB of memory.");
    dataset->m_buf = malloc(static_cast<size_t>(elements * dataset->m_datatype_size));
    if(!dataset->m_buf) return QObject::tr("Not enough memory to read this variable.");
    dataset->m_slice_mode = rank > 2;
  }
  const QString error = nc_classic_read_variable(classic_file_of(item_data), dataset->m_path,
      rank == 0 ? std::vector<size_t>() : start, count, dataset->m_buf);
  if(!error.isEmpty())
  {
    free(dataset->m_buf);
    dataset->m_buf = nullptr;
  }
  return error;
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
  m_mdi_area->installEventFilter(this);
  m_mdi_area->viewport()->setAcceptDrops(true);
  m_mdi_area->viewport()->installEventFilter(this);

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
  m_action_tile->setStatusTip(tr("Tile the windows"));
  connect(m_action_tile, SIGNAL(triggered()), m_mdi_area, SLOT(tileSubWindows()));

  ///////////////////////////////////////////////////////////////////////////////////////
  //about
  ///////////////////////////////////////////////////////////////////////////////////////

  m_action_about = new QAction(tr("&About"), this);
  m_action_about->setStatusTip(tr("Show the application's About box"));
  connect(m_action_about, SIGNAL(triggered()), this, SLOT(about()));

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

  m_menu_windows = menuBar()->addMenu(tr("&Window"));
  m_menu_windows->addAction(m_action_tile);
  m_menu_windows->addAction(m_action_close_all);

  m_menu_help = menuBar()->addMenu(tr("&Help"));
  m_menu_help->addAction(m_action_about);
  m_menu_help->addAction(tr("CF-1.8 Diagnostics..."),this,&MainWindow::show_cf_diagnostics);

  ///////////////////////////////////////////////////////////////////////////////////////
  //toolbar
  ///////////////////////////////////////////////////////////////////////////////////////

  m_tool_bar = addToolBar(tr("&File"));
  m_tool_bar->addAction(m_action_open);

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

// Starts the embedded netcdf_report.py worker owned by `owner`; returns nullptr if the resource
// is missing. Python runs outside the GUI thread. Arguments are passed directly without a shell.
static QProcess *start_python_worker(QObject *owner, const QStringList &arguments) {
  QFile script(":/netcdf_report.py");
  if(!script.open(QIODevice::ReadOnly)) return nullptr;
  auto *job=new QProcess(owner);
  job->start(qEnvironmentVariable("HDFSTUDIO_PYTHON","python"),QStringList{"-I","-c",QString::fromUtf8(script.readAll())}+arguments);
  return job;
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
  const FormatProbe format=probe_data_format(info.absoluteFilePath());
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
    tr("HDFStudio 0.10.27\nAuthor: DuLiangMin\n\n"
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
    tr("HDF5 and NetCDF-4 (*.h5 *.hdf5 *.he5 *.nc *.nc4);;All files (*.*)"));

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
      m_str_current_file = QString::fromLocal8Bit(last_data->m_file_name.c_str());
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

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
  if((watched == m_mdi_area || watched == m_mdi_area->viewport()) && event->type() == QEvent::DragEnter) {
    auto *drop_event = static_cast<QDragEnterEvent *>(event);
    if(accepts_file_drop(drop_event->mimeData())) {
      drop_event->setDropAction(Qt::CopyAction);
      drop_event->accept();
      return true;
    }
  } else if((watched == m_mdi_area || watched == m_mdi_area->viewport()) && event->type() == QEvent::DragMove) {
    auto *drop_event = static_cast<QDragMoveEvent *>(event);
    if(accepts_file_drop(drop_event->mimeData())) {
      drop_event->setDropAction(Qt::CopyAction);
      drop_event->accept();
      return true;
    }
  } else if((watched == m_mdi_area || watched == m_mdi_area->viewport()) && event->type() == QEvent::Drop) {
    handle_file_drop(static_cast<QDropEvent *>(event)->mimeData(), static_cast<QDropEvent *>(event));
    return true;
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

int MainWindow::read_file(QString file_name)
{
  QByteArray ba;
  std::string path;
  std::string str_file_name;
  QString name;
  int index;
  int len;
  hid_t fid;

  //convert QString to char*
  file_name = QFileInfo(file_name).absoluteFilePath();
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

  item_data->m_dataset->m_buf = malloc(static_cast<size_t>(datatype_size * nbr_elements));

  if(H5Dread(did, mtid, H5S_ALL, H5S_ALL, H5P_DEFAULT, item_data->m_dataset->m_buf) < 0)
  {
    free(item_data->m_dataset->m_buf);
    item_data->m_dataset->m_buf = nullptr;
    QMessageBox::warning(this, tr("Grid"), tr("Cannot read this dataset. Check its size and compression filter."));
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

  QAction *action_grid = menu.addAction(tr("Grid..."));
  action_grid->setEnabled(numeric && (variable || attribute));
  connect(action_grid, &QAction::triggered, this, &FileTreeWidget::add_grid);

  QAction *action_image = menu.addAction(tr("Image View"));
  action_image->setEnabled(numeric && variable && rank >= 2);
  connect(action_image, &QAction::triggered, this, [this, item_data]() { m_main_window->add_image(item_data); });

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
  ItemData *item_data = item ? get_item_data(item) : nullptr;
  if(!item_data) return;

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
  auto summary=HdfProperties::inspect(QString::fromLocal8Bit(item_data->m_file_name.c_str()),objectPath);
  details.append(HdfProperties::describe(summary));
  QMessageBox::information(this, tr("Properties"), details.join(QLatin1Char('\n')));
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
  if(!item_data || item_data->m_kind != ItemData::Variable || !item_data->m_dataset) return;
  m_main_window->export_image(item_data, this);
}

///////////////////////////////////////////////////////////////////////////////////////
//FileTreeWidget::add_grid
///////////////////////////////////////////////////////////////////////////////////////

void FileTreeWidget::add_grid()
{
  QTreeWidgetItem *item = static_cast <QTreeWidgetItem*> (currentItem());
  if(!item) return;
  ItemData *item_data = get_item_data(item);
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
  if(item_data->m_dataset->m_buf) m_main_window->add_table(item_data);

}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel
/////////////////////////////////////////////////////////////////////////////////////////////////////

class TableModel : public QAbstractTableModel
{
public:
  TableModel(QObject *parent, ItemData *item_data);
  int rowCount(const QModelIndex &parent = QModelIndex()) const;
  int columnCount(const QModelIndex &parent = QModelIndex()) const;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;

  hdf_dataset_t *m_dataset; // HDF variable to display (convenience pointer to data in ItemData) 
  ChildWindow* m_widget; //get layers in toolbar
  int m_nbr_rows;   // number of rows
  int m_nbr_cols;   // number of numeric columns (date columns come after these)
  void data_changed(); //update table view when change of layer
  // Appends "yyyymmdd" and "yyyymmdd hh:mm:ss" columns decoded by netCDF4.num2date from the
  // variable's own units/calendar; each list must have one entry per row.
  void set_time_columns(const QStringList &days, const QStringList &moments, const QString &source);
  QStringList m_days;    // decoded yyyymmdd per row; empty when the variable is not a time coordinate
  QStringList m_moments; // decoded yyyymmdd hh:mm:ss per row
  QString m_time_source; // "units | calendar" shown as the date header tooltip
private:
  ItemData *m_item_data; // the tree item that generated this grid 
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::TableModel
/////////////////////////////////////////////////////////////////////////////////////////////////////

TableModel::TableModel(QObject *parent, ItemData *item_data) :
QAbstractTableModel(parent),
m_dataset(item_data->m_dataset),
m_widget(NULL),
m_item_data(item_data)
{
  assert(m_dataset->m_dim.size() <= H5S_MAX_RANK);

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //define grid
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  if(m_dataset->m_dim.size() == 0)
  {
    m_nbr_rows = 1;
    m_nbr_cols = 1;
  }
  else if(m_dataset->m_dim.size() == 1)
  {
    m_nbr_rows = m_dataset->m_dim[0];
    m_nbr_cols = 1;
  }
  else
  {
    m_nbr_rows = m_dataset->m_dim[m_dataset->m_dim.size() - 2]; //index 1 for 3D
    m_nbr_cols = m_dataset->m_dim[m_dataset->m_dim.size() - 1]; //index 2 for 3D
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::rowCount
/////////////////////////////////////////////////////////////////////////////////////////////////////

int TableModel::rowCount(const QModelIndex &) const
{
  return m_nbr_rows;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::columnCount
/////////////////////////////////////////////////////////////////////////////////////////////////////

int TableModel::columnCount(const QModelIndex &) const
{
  return m_nbr_cols + (m_days.isEmpty() ? 0 : 2);
}

void TableModel::set_time_columns(const QStringList &days, const QStringList &moments, const QString &source)
{
  if(!m_days.isEmpty() || days.size() != m_nbr_rows || moments.size() != m_nbr_rows || m_nbr_cols != 1) return;
  beginInsertColumns(QModelIndex(), m_nbr_cols, m_nbr_cols + 1);
  m_days = days;
  m_moments = moments;
  m_time_source = source;
  endInsertColumns();
}

QVariant TableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if(orientation == Qt::Horizontal && !m_days.isEmpty()) {
    if(role == Qt::DisplayRole) {
      if(section == 0) return tr("Value");
      if(section == m_nbr_cols) return QStringLiteral("yyyymmdd");
      if(section == m_nbr_cols + 1) return QStringLiteral("yyyymmdd hh:mm:ss");
    }
    if(role == Qt::ToolTipRole && section >= m_nbr_cols)
      return tr("Decoded by netCDF4.num2date: %1").arg(m_time_source);
  }
  return QAbstractTableModel::headerData(section, orientation, role);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::data_changed
//update table view when change of layer
/////////////////////////////////////////////////////////////////////////////////////////////////////

void TableModel::data_changed()
{
  // A NetCDF-3 plane is read on demand: load the newly selected layer before the view repaints.
  if(m_dataset->m_classic && m_dataset->m_slice_mode && m_widget)
  {
    const QString error = classic_load_variable(m_item_data, m_widget->m_layer);
    if(!error.isEmpty()) QMessageBox::warning(nullptr, QObject::tr("Grid"), error);
  }
  if(m_nbr_rows <= 0 || columnCount() <= 0) return;
  QModelIndex top = index(0, 0, QModelIndex());
  QModelIndex bottom = index(m_nbr_rows - 1, columnCount() - 1, QModelIndex());
  dataChanged(top, bottom);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::data
/////////////////////////////////////////////////////////////////////////////////////////////////////

QVariant TableModel::data(const QModelIndex &index, int role) const
{
  ChildWindow* parent = m_widget;
  QString str;
  size_t idx_buf = 0;

  if(index.column() >= m_nbr_cols) {
    if(role != Qt::DisplayRole || index.row() >= m_days.size()) return QVariant();
    return index.column() == m_nbr_cols ? m_days.at(index.row()) : m_moments.at(index.row());
  }

  //start of offset
  //a NetCDF-3 slice buffer already holds only the selected plane
  if(m_dataset->m_slice_mode)
  {
    idx_buf = 0;
  }
  //3D
  else if(parent->m_layer.size() == 1)
  {
    idx_buf = parent->m_layer[0] * m_nbr_rows * m_nbr_cols;
  }
  //4D
  else if(parent->m_layer.size() == 2)
  {
    idx_buf = parent->m_layer[0] * m_dataset->m_dim[1] + parent->m_layer[1];
    idx_buf *= m_nbr_rows * m_nbr_cols;
  }
  //5D
  else if(parent->m_layer.size() == 3)
  {
    idx_buf = parent->m_layer[0] * m_dataset->m_dim[1] * m_dataset->m_dim[2]
      + parent->m_layer[1] * m_dataset->m_dim[2]
      + parent->m_layer[2];
    idx_buf *= m_nbr_rows * m_nbr_cols;
  }

  //into current index
  idx_buf += index.row() * m_nbr_cols + index.column();

  if(role != Qt::DisplayRole)
  {
    return QVariant();
  }

  switch(m_dataset->m_datatype_class)
  {
    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_STRING
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_STRING:

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_FLOAT
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_FLOAT:
    if(sizeof(float) == m_dataset->m_datatype_size)
    {
      float *buf_ = static_cast<float*> (m_dataset->m_buf);
      str = QString::asprintf("%g", buf_[idx_buf]);
      return str;
    }
    else if(sizeof(double) == m_dataset->m_datatype_size)
    {
      // 15 significant digits: "%g" (6 digits) turned epoch seconds such as 915170400 into 9.1517e+08.
      double *buf_ = static_cast<double*> (m_dataset->m_buf);
      str = QString::asprintf("%.15g", buf_[idx_buf]);
      return str;
    }
#if H5_SIZEOF_LONG_DOUBLE !=0
    else if(sizeof(long double) == m_dataset->m_datatype_size)
    {
      long double *buf_;
      buf_ = static_cast<long double*> (m_dataset->m_buf);
      str = QString::asprintf("%Lf", buf_[idx_buf]);
      return str;
    }
#endif
    break;

  case H5T_INTEGER:

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_INTEGER
    ///////////////////////////////////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_NATIVE_SCHAR H5T_NATIVE_UCHAR
    ///////////////////////////////////////////////////////////////////////////////////////

    if(sizeof(char) == m_dataset->m_datatype_size)
    {
      if(H5T_SGN_NONE == m_dataset->m_datatype_sign)
      {
        unsigned char *buf_ = static_cast<unsigned char*> (m_dataset->m_buf);
        str = QString::asprintf("%u", buf_[idx_buf]);
        return str;
      }
      else
      {
        signed char *buf_ = static_cast<signed char*> (m_dataset->m_buf);
        str = QString::asprintf("%hhd", buf_[idx_buf]);
        return str;
      }
    }

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_NATIVE_SHORT H5T_NATIVE_USHORT
    ///////////////////////////////////////////////////////////////////////////////////////

    else if(sizeof(short) == m_dataset->m_datatype_size)
    {
      if(H5T_SGN_NONE == m_dataset->m_datatype_sign)
      {
        unsigned short *buf_ = static_cast<unsigned short*> (m_dataset->m_buf);
        str = QString::asprintf("%u", buf_[idx_buf]);
        return str;

      }
      else
      {
        short *buf_ = static_cast<short*> (m_dataset->m_buf);
        str = QString::asprintf("%d", buf_[idx_buf]);
        return str;

      }

    }

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_NATIVE_INT H5T_NATIVE_UINT
    ///////////////////////////////////////////////////////////////////////////////////////

    else if(sizeof(int) == m_dataset->m_datatype_size)
    {
      if(H5T_SGN_NONE == m_dataset->m_datatype_sign)
      {
        unsigned int* buf_ = static_cast<unsigned int*> (m_dataset->m_buf);
        str = QString::asprintf("%u", buf_[idx_buf]);
        return str;

      }
      else
      {
        int* buf_ = static_cast<int*> (m_dataset->m_buf);
        str = QString::asprintf("%d", buf_[idx_buf]);
        return str;
      }
    }

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_NATIVE_LONG H5T_NATIVE_ULONG
    ///////////////////////////////////////////////////////////////////////////////////////

    else if(sizeof(long) == m_dataset->m_datatype_size)
    {

      if(H5T_SGN_NONE == m_dataset->m_datatype_sign)
      {
        unsigned long* buf_ = static_cast<unsigned long*> (m_dataset->m_buf);
        str = QString::asprintf("%lu", buf_[idx_buf]);
        return str;

      }
      else
      {
        long* buf_ = static_cast<long*> (m_dataset->m_buf);
        str = QString::asprintf("%ld", buf_[idx_buf]);
        return str;

      }

    }

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_NATIVE_LLONG H5T_NATIVE_ULLONG
    ///////////////////////////////////////////////////////////////////////////////////////

    else if(sizeof(long long) == m_dataset->m_datatype_size)
    {

      if(H5T_SGN_NONE == m_dataset->m_datatype_sign)
      {
        unsigned long long* buf_ = static_cast<unsigned long long*> (m_dataset->m_buf);
        str = QString::asprintf("%llu", buf_[idx_buf]);
        return str;

      }
      else
      {
        long long* buf_ = static_cast<long long*> (m_dataset->m_buf);
        str = QString::asprintf("%lld", buf_[idx_buf]);
        return str;

      }

    }

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_COMPOUND
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_COMPOUND:

    break;


    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_ENUM
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_ENUM:

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_ARRAY
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_ARRAY:

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_VLEN
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_VLEN:

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_TIME H5T_BITFIELD H5T_OPAQUE
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_TIME:
  case H5T_BITFIELD:
  case H5T_OPAQUE:

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_REFERENCE
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_REFERENCE:


    break;

  default:


    break;

  }; //switch

  return QVariant();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ChildWindowTable
//model/view
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ChildWindowTable : public ChildWindow
{
public:
  ChildWindowTable(QWidget *parent, ItemData *item_data) :
    ChildWindow(parent, item_data)
  {
    //each new table widget has its own model
    m_model = new TableModel(this, item_data);
    m_model->m_widget = this;
    m_table = new QTableView(this);
    m_table->setModel(m_model);

    //set default row height
    QHeaderView *verticalHeader = m_table->verticalHeader();
#if QT_VERSION >= 0x050000
    verticalHeader->sectionResizeMode(QHeaderView::Fixed);
#else
    verticalHeader->setResizeMode(QHeaderView::Fixed);
#endif
    verticalHeader->setDefaultSectionSize(24);
    setCentralWidget(m_table);
    m_table->setObjectName("gridTable");
    start_time_decoding(item_data);
  }
private:
  // A one-dimensional variable is offered to netcdf_report.py "dates"; the worker exits with 3
  // unless the variable declares "UNIT since DATE" units, in which case the grid stays numeric.
  void start_time_decoding(ItemData *item_data)
  {
    if(item_data->m_kind != ItemData::Variable || m_dataset->m_dim.size() != 1) return;
    const QString file = QFileInfo(QString::fromStdString(item_data->m_file_name)).absoluteFilePath();
    QProcess *job = start_python_worker(this, {"dates", file, QString::fromStdString(m_dataset->m_path)});
    if(!job) return;
    connect(job, &QProcess::errorOccurred, job, [job](QProcess::ProcessError error) {
      if(error == QProcess::FailedToStart) job->deleteLater();
    });
    connect(job, &QProcess::finished, this, [this, job](int code, QProcess::ExitStatus status) {
      job->deleteLater();
      if(status != QProcess::NormalExit || code != 0) return;
      const QStringList lines = QString::fromUtf8(job->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
      if(lines.isEmpty() || !lines.first().startsWith("#\t")) return;
      const QStringList head = lines.first().trimmed().split('\t');
      QStringList days, moments;
      for(int i = 1; i < lines.size(); i++) {
        const QStringList cells = lines.at(i).trimmed().split('\t');
        if(cells.size() != 2) return;
        days.append(cells.at(0));
        moments.append(cells.at(1));
      }
      m_model->set_time_columns(days, moments, head.size() == 3 ? head.at(1) + " | calendar=" + head.at(2) : QString());
      m_table->resizeColumnsToContents();
      if(QMdiSubWindow *sub = qobject_cast<QMdiSubWindow*>(parentWidget()))
        sub->resize(qMax(sub->width(), m_table->horizontalHeader()->length() + 80), sub->height());
    });
  }
  QTableView *m_table;
};


///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::add_table
///////////////////////////////////////////////////////////////////////////////////////

void MainWindow::add_table(ItemData *item_data)
{
  ChildWindowTable *window = new ChildWindowTable(this, item_data);
  QMdiSubWindow *sub = m_mdi_area->addSubWindow(window);
  sub->setAttribute(Qt::WA_DeleteOnClose);
  tag_view(sub, item_data);
  window->show();
}

///////////////////////////////////////////////////////////////////////////////////////
//MainWindow::add_image
///////////////////////////////////////////////////////////////////////////////////////

void MainWindow::add_image(ItemData *item)
{
  auto view = new RasterView(item->m_file_name, item->m_dataset->m_path, item->m_dataset->m_dim);
  auto sub = m_mdi_area->addSubWindow(view);
  sub->setAttribute(Qt::WA_DeleteOnClose);
  tag_view(sub, item);
  sub->resize(720, 510);
  sub->show();
}

void MainWindow::export_image(ItemData *item, QWidget *parent)
{
  if(!item || !item->m_dataset) return;
  const QString output = QFileDialog::getSaveFileName(parent, tr("Export As Image"),
      QString::fromStdString(item->m_item_nm) + QStringLiteral(".png"), tr("PNG images (*.png)"));
  if(output.isEmpty()) return;

  auto *view = new RasterView(item->m_file_name, item->m_dataset->m_path, item->m_dataset->m_dim);
  view->resize(720, 510);
  view->show();
  QCoreApplication::processEvents();
  const bool saved = view->grab().save(output, "PNG");
  delete view;
  if(!saved) QMessageBox::warning(parent, tr("Export As Image"), tr("Could not save the image."));
}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::ChildWindow
///////////////////////////////////////////////////////////////////////////////////////

ChildWindow::ChildWindow(QWidget *parent, ItemData *item_data) :
QMainWindow(parent),
m_dataset(item_data->m_dataset)
{
  QString str;
  str = QString::asprintf(" : %s", item_data->m_item_nm.c_str());
  this->setWindowTitle(last_component(item_data->m_file_name.c_str()) + str);

  //currently selected layers for dimensions greater than two are the first layer
  if(m_dataset->m_dim.size() > 2)
  {
    for(size_t idx_dmn = 0; idx_dmn < m_dataset->m_dim.size() - 2; idx_dmn++)
    {
      m_layer.push_back(0);
    }
  }

  QSignalMapper *signal_mapper_next = NULL;
  QSignalMapper *signal_mapper_previous = NULL;
  QSignalMapper *signal_mapper_combo = NULL;

  //data has layers
  if(m_dataset->m_dim.size() > 2)
  {
    m_tool_bar = addToolBar(tr("Layers"));

    signal_mapper_next = new QSignalMapper(this);
    signal_mapper_previous = new QSignalMapper(this);
    signal_mapper_combo = new QSignalMapper(this);
    connect(signal_mapper_next, &QSignalMapper::mappedInt, this, &ChildWindow::next_layer);
    connect(signal_mapper_previous, &QSignalMapper::mappedInt, this, &ChildWindow::previous_layer);
    connect(signal_mapper_combo, &QSignalMapper::mappedInt, this, &ChildWindow::combo_layer);
  }

  //number of dimensions above a two-dimensional dataset
  for(size_t idx_dmn = 0; idx_dmn < m_layer.size(); idx_dmn++)
  {
    ///////////////////////////////////////////////////////////////////////////////////////
    //next layer
    ///////////////////////////////////////////////////////////////////////////////////////

    QAction *action_next  = new QAction(tr("&Next layer..."), this);
    action_next->setIcon(QIcon(":/images/right.png"));
    action_next->setStatusTip(tr("Next layer"));
    connect(action_next, SIGNAL(triggered()), signal_mapper_next, SLOT(map()));
    signal_mapper_next->setMapping(action_next, idx_dmn);

    ///////////////////////////////////////////////////////////////////////////////////////
    //previous layer
    ///////////////////////////////////////////////////////////////////////////////////////

    QAction *action_previous = new QAction(tr("&Previous layer..."), this);
    action_previous->setIcon(QIcon(":/images/left.png"));
    action_previous->setStatusTip(tr("Previous layer"));
    connect(action_previous, SIGNAL(triggered()), signal_mapper_previous, SLOT(map()));
    signal_mapper_previous->setMapping(action_previous, idx_dmn);

    ///////////////////////////////////////////////////////////////////////////////////////
    //add to toolbar
    ///////////////////////////////////////////////////////////////////////////////////////

    m_tool_bar->addAction(action_next);
    m_tool_bar->addAction(action_previous);

    ///////////////////////////////////////////////////////////////////////////////////////
    //add combo box with layers, fill with possible coordinate variables and store combo in vector 
    ///////////////////////////////////////////////////////////////////////////////////////

    QComboBox *combo = new QComboBox;
    QFont font = combo->font();
    font.setPointSize(9);
    combo->setFont(font);
    QStringList list;

    for(unsigned int idx = 0; idx < m_dataset->m_dim[idx_dmn]; idx++)
    {
      str = QString::asprintf("%u", idx + 1);
      list.append(str);
    }


    combo->addItems(list);
    connect(combo, SIGNAL(currentIndexChanged(int)), signal_mapper_combo, SLOT(map()));
    signal_mapper_combo->setMapping(combo, idx_dmn);
    m_tool_bar->addWidget(combo);
    m_vec_combo.push_back(combo);
  }


}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::previous_layer
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::previous_layer(int idx_layer)
{
  m_layer[idx_layer]--;
  if(m_layer[idx_layer] < 0)
  {
    m_layer[idx_layer] = 0;
    return;
  }
  QComboBox *combo = m_vec_combo.at(idx_layer);
  combo->setCurrentIndex(m_layer[idx_layer]);
  m_model->data_changed();
  update();

}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::next_layer
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::next_layer(int idx_layer)
{
  m_layer[idx_layer]++;
  if((size_t)m_layer[idx_layer] >= m_dataset->m_dim[idx_layer])
  {
    m_layer[idx_layer] = m_dataset->m_dim[idx_layer] - 1;
    return;
  }
  QComboBox *combo = m_vec_combo.at(idx_layer);
  combo->setCurrentIndex(m_layer[idx_layer]);
  m_model->data_changed();
  update();

}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::combo_layer
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::combo_layer(int idx_layer)
{
  QComboBox *combo = m_vec_combo.at(idx_layer);
  m_layer[idx_layer] = combo->currentIndex();;
  m_model->data_changed();
  update();
}
