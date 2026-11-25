#include "search_panel.hpp"
#include <QtWidgets>
#include <atomic>
#include <exception>

class FileSearchThread final : public QThread {
public:
  Search::FileQuery query;
  Search::FileResults result;
  std::atomic_bool cancelled{false};
  std::atomic<quint64> scanned{0};
  std::atomic<int> matches{0};
protected:
  void run() override {
    try {
      result=Search::find_files(query,[this]{return cancelled.load();},[this](quint64 count,int found){scanned=count;matches=found;});
    } catch(const std::exception &error) {
      result.errors=1;result.diagnostics.append(QString::fromUtf8(error.what()));
    }
  }
};

namespace {
QTableView *table(QWidget *parent, QStandardItemModel *model, const char *name) {
  auto *view=new QTableView(parent);view->setObjectName(name);view->setModel(model);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);view->setSelectionMode(QAbstractItemView::SingleSelection);
  view->setEditTriggers(QAbstractItemView::NoEditTriggers);view->setSortingEnabled(true);
  view->verticalHeader()->hide();view->horizontalHeader()->setStretchLastSection(true);
  return view;
}
QStandardItem *cell(const QString &text) {auto *item=new QStandardItem(text);item->setToolTip(text);return item;}
}

SearchPanel::SearchPanel(QWidget *parent):QDockWidget(tr("Find files / variables"),parent) {
  setObjectName("searchDock");setAllowedAreas(Qt::BottomDockWidgetArea|Qt::RightDockWidgetArea|Qt::LeftDockWidgetArea);
  auto *content=new QWidget(this);setWidget(content);auto *layout=new QVBoxLayout(content);
  m_tabs=new QTabWidget(content);m_tabs->setObjectName("searchTabs");layout->addWidget(m_tabs);
  auto *files=new QWidget(m_tabs);m_tabs->addTab(files,tr("Find files"));auto *file_layout=new QVBoxLayout(files);
  auto *directory_row=new QHBoxLayout;file_layout->addLayout(directory_row);
  directory_row->addWidget(new QLabel(tr("Directory"),files));
  m_directory=new QLineEdit(QDir::homePath(),files);m_directory->setObjectName("searchDirectory");directory_row->addWidget(m_directory,1);
  auto *browse=new QPushButton(tr("Browse..."),files);browse->setObjectName("searchBrowse");directory_row->addWidget(browse);
  connect(browse,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,tr("Search directory"),m_directory->text());if(!path.isEmpty())m_directory->setText(path);});
  auto *pattern_row=new QHBoxLayout;file_layout->addLayout(pattern_row);
  pattern_row->addWidget(new QLabel(tr("File name"),files));
  m_filename=new QLineEdit(files);m_filename->setObjectName("searchFilename");m_filename->setPlaceholderText(tr("C7F, *C7F* or *.grib2; empty = all files"));pattern_row->addWidget(m_filename,1);
  m_recursive=new QCheckBox(tr("Subdirectories"),files);m_recursive->setObjectName("searchRecursive");m_recursive->setChecked(true);pattern_row->addWidget(m_recursive);
  m_start=new QPushButton(tr("Search"),files);m_start->setObjectName("searchStart");pattern_row->addWidget(m_start);
  m_stop=new QPushButton(tr("Stop"),files);m_stop->setObjectName("searchStop");m_stop->setEnabled(false);pattern_row->addWidget(m_stop);
  auto *file_note=new QLabel(tr("File names only, case-insensitive. Includes extensionless files; does not read data contents."),files);file_note->setWordWrap(true);file_layout->addWidget(file_note);
  m_files=new QStandardItemModel(0,4,this);m_files->setHorizontalHeaderLabels({tr("File name"),tr("Directory"),tr("Bytes"),tr("Modified")});
  m_file_table=table(files,m_files,"searchFileResults");file_layout->addWidget(m_file_table,1);
  m_file_table->setColumnWidth(0,300);m_file_table->setColumnWidth(1,320);m_file_table->setColumnWidth(2,105);
  auto *file_buttons=new QHBoxLayout;file_layout->addLayout(file_buttons);
  auto *open=new QPushButton(tr("Open"),files);open->setObjectName("searchOpenFile");file_buttons->addWidget(open);
  auto *copy=new QPushButton(tr("Copy path"),files);copy->setObjectName("searchCopyPath");file_buttons->addWidget(copy);
  auto *reveal=new QPushButton(tr("Show in folder"),files);reveal->setObjectName("searchRevealFile");file_buttons->addWidget(reveal);file_buttons->addStretch();
  m_file_status=new QLabel(tr("Choose a directory and search by file name."),files);m_file_status->setObjectName("searchFileStatus");m_file_status->setWordWrap(true);file_layout->addWidget(m_file_status);
  connect(m_start,&QPushButton::clicked,this,&SearchPanel::start_search);connect(m_filename,&QLineEdit::returnPressed,this,&SearchPanel::start_search);
  connect(m_stop,&QPushButton::clicked,this,&SearchPanel::stop_search);
  connect(open,&QPushButton::clicked,this,&SearchPanel::open_selected_file);
  connect(m_file_table,&QTableView::doubleClicked,this,[this]{open_selected_file();});
  connect(copy,&QPushButton::clicked,this,[this]{const auto path=selected_file();if(!path.isEmpty())QApplication::clipboard()->setText(QDir::toNativeSeparators(path));});
  connect(reveal,&QPushButton::clicked,this,[this]{const auto path=selected_file();if(path.isEmpty())return;
#ifdef _WIN32
    QProcess::startDetached("explorer.exe",{QString("/select,%1").arg(QDir::toNativeSeparators(path))});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
  });

  auto *variables=new QWidget(m_tabs);m_tabs->addTab(variables,tr("Find variables"));auto *variable_layout=new QVBoxLayout(variables);
  m_context=new QLabel(variables);m_context->setObjectName("searchCurrentFile");m_context->setWordWrap(true);m_context->setTextInteractionFlags(Qt::TextSelectableByMouse);variable_layout->addWidget(m_context);
  auto *query_row=new QHBoxLayout;variable_layout->addLayout(query_row);
  m_query=new QLineEdit(variables);m_query->setObjectName("searchVariableQuery");m_query->setPlaceholderText(tr("Variable name, path, description or GRIB parameter ID"));query_row->addWidget(m_query,1);
  m_exact=new QCheckBox(tr("Exact name / path / ID"),variables);m_exact->setObjectName("searchVariableExact");query_row->addWidget(m_exact);
  auto *clear=new QPushButton(tr("Clear"),variables);query_row->addWidget(clear);connect(clear,&QPushButton::clicked,m_query,&QLineEdit::clear);
  m_variables=new QStandardItemModel(0,6,this);m_variables->setHorizontalHeaderLabels({tr("Name"),tr("Description"),tr("Units"),tr("Shape / level type"),tr("Fields"),tr("Object path")});
  m_variable_table=table(variables,m_variables,"searchVariableResults");variable_layout->addWidget(m_variable_table,1);
  m_variable_table->setColumnWidth(0,190);m_variable_table->setColumnWidth(1,260);m_variable_table->setColumnWidth(2,95);m_variable_table->setColumnWidth(3,170);
  auto *variable_buttons=new QHBoxLayout;variable_layout->addLayout(variable_buttons);
  const QStringList labels{tr("Locate in tree"),tr("Image"),tr("Table"),tr("Properties")};
  for(int action=0;action<labels.size();++action) {
    auto *button=new QPushButton(labels[action],variables);button->setObjectName(QString("searchVariableAction%1").arg(action));variable_buttons->addWidget(button);
    connect(button,&QPushButton::clicked,this,[this,action]{activate_variable(action);});
  }
  variable_buttons->addStretch();
  m_variable_status=new QLabel(variables);m_variable_status->setObjectName("searchVariableStatus");m_variable_status->setWordWrap(true);variable_layout->addWidget(m_variable_status);
  connect(m_query,&QLineEdit::textChanged,this,&SearchPanel::refresh_variables);connect(m_exact,&QCheckBox::toggled,this,&SearchPanel::refresh_variables);
  connect(m_variable_table,&QTableView::clicked,this,[this]{activate_variable(0);});
  connect(m_variable_table,&QTableView::doubleClicked,this,[this]{activate_variable(0);});
  m_progress=new QTimer(this);m_progress->setInterval(100);
  connect(m_progress,&QTimer::timeout,this,[this]{if(m_worker)m_file_status->setText(tr("Scanning: %1 entries; %2 matches. Results appear when finished or stopped.").arg(m_worker->scanned.load()).arg(m_worker->matches.load()));});
  set_context({},{});
}

SearchPanel::~SearchPanel() {
  ++m_generation;
  if(m_worker){m_worker->cancelled=true;disconnect(m_worker,nullptr,this,nullptr);}
}
void SearchPanel::show_files(){show();raise();m_tabs->setCurrentIndex(0);m_filename->setFocus();}
void SearchPanel::show_variables(){show();raise();m_tabs->setCurrentIndex(1);m_query->setFocus();}
QString SearchPanel::selected_file() const {
  const auto index=m_file_table->currentIndex();
  return index.isValid()?m_files->index(index.row(),0).data(Qt::UserRole).toString():QString();
}
void SearchPanel::open_selected_file() {
  const auto path=selected_file();if(path.isEmpty())return;
  if(!QFileInfo(path).isFile()){m_file_status->setText(tr("File is no longer available: %1").arg(path));return;}
  emit open_file_requested(path);
}
void SearchPanel::start_search() {
  if(m_worker)return;
  const QFileInfo directory(m_directory->text().trimmed());
  if(!directory.isDir()){m_file_status->setText(tr("Directory is not available: %1").arg(directory.filePath()));return;}
  m_files->removeRows(0,m_files->rowCount());
  auto *worker=new FileSearchThread;
  worker->query.directory=directory.absoluteFilePath();worker->query.pattern=m_filename->text();worker->query.recursive=m_recursive->isChecked();
  const auto generation=++m_generation;m_worker=worker;
  m_start->setEnabled(false);m_stop->setEnabled(true);m_directory->setEnabled(false);m_filename->setEnabled(false);m_recursive->setEnabled(false);
  m_file_status->setText(tr("Scanning file names..."));setProperty("searchBusy",true);m_progress->start();
  connect(worker,&QThread::finished,this,[this,worker,generation]{
    if(generation!=m_generation)return;
    m_progress->stop();const auto &result=worker->result;
    m_file_table->setSortingEnabled(false);
    for(const auto &file:result.files) {
      auto *name=cell(file.name);name->setData(file.path,Qt::UserRole);name->setToolTip(file.path);
      auto *bytes=cell({});bytes->setData(file.bytes,Qt::DisplayRole);
      m_files->appendRow({name,cell(QDir::toNativeSeparators(file.directory)),bytes,cell(file.modified.toString("yyyy-MM-dd HH:mm:ss"))});
    }
    m_file_table->setSortingEnabled(true);
    const auto status=result.cancelled?tr("Stopped — partial results"):result.truncated?tr("Limit reached — incomplete results"):result.errors?tr("Finished with errors — incomplete results"):tr("Finished");
    m_file_status->setText(tr("%1: %2 files; %3 entries scanned; %4 errors.").arg(status).arg(result.files.size()).arg(result.scanned).arg(result.errors));
    m_file_status->setToolTip(result.diagnostics.join('\n'));
    m_start->setEnabled(true);m_stop->setEnabled(false);m_directory->setEnabled(true);m_filename->setEnabled(true);m_recursive->setEnabled(true);
    m_worker=nullptr;setProperty("searchBusy",false);
  });
  connect(worker,&QThread::finished,worker,&QObject::deleteLater);
  worker->start();
}
void SearchPanel::stop_search(){if(m_worker){m_worker->cancelled=true;m_stop->setEnabled(false);m_file_status->setText(tr("Stopping after the current filesystem operation..."));}}
void SearchPanel::set_context(const QString &file, const QVector<BrowserSearch::Entry> &entries) {
  m_context_file=file;m_entries=entries;
  m_context->setText(file.isEmpty()?tr("Current file: none"):tr("Current file: %1").arg(QDir::toNativeSeparators(file)));
  m_context->setToolTip(file);refresh_variables();
}
void SearchPanel::refresh_variables() {
  m_variable_table->setSortingEnabled(false);m_variables->removeRows(0,m_variables->rowCount());
  int shown=0;
  for(const auto &entry:m_entries) {
    if(!entry.index.isValid() || !Search::variable_matches(entry.variable,m_query->text(),m_exact->isChecked()))continue;
    if(shown>=10000)break;
    const auto &variable=entry.variable;
    auto *name=cell(variable.name);name->setData(QVariant::fromValue(entry.index),Qt::UserRole);name->setToolTip(variable.path);
    auto *fields=cell({});if(variable.fields>=0)fields->setData(variable.fields,Qt::DisplayRole);
    m_variables->appendRow({name,cell(variable.description),cell(variable.units.isEmpty()?tr("unknown"):variable.units),cell(variable.shape),fields,cell(variable.path)});++shown;
  }
  m_variable_table->setSortingEnabled(true);
  m_variable_status->setText(m_context_file.isEmpty()?tr("Open a file, or select its node in the file tree."):
    tr("%1 / %2 variables. Current file only; layer/member selection uses existing controls.%3").arg(shown).arg(m_entries.size()).arg(shown>=10000?tr(" Display limit: 10000; refine the query."):QString()));
}
void SearchPanel::activate_variable(int action) {
  const auto selected=m_variable_table->currentIndex();if(!selected.isValid())return;
  const auto index=m_variables->index(selected.row(),0).data(Qt::UserRole).value<QPersistentModelIndex>();
  if(!index.isValid()){set_context({},{});return;}
  emit variable_requested(index,action);
}
