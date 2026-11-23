#include "grib_table_view.hpp"
#include "analysis_table_view.hpp"
#include "slice_io.hpp"
#include <cmath>

namespace {
class GribTableModel : public QAbstractTableModel {
public:
  using QAbstractTableModel::QAbstractTableModel;
  std::shared_ptr<const DataSource::PlaneData> plane;
  std::shared_ptr<void> budget;
  int mode=0;
  int rowCount(const QModelIndex &parent={}) const override {return parent.isValid()||!plane?0:int(plane->rows);}
  int columnCount(const QModelIndex &parent={}) const override {return parent.isValid()||!plane?0:int(plane->cols)*(mode==2?2:1);}
  QVariant data(const QModelIndex &index,int role=Qt::DisplayRole) const override {
    if(!plane||!index.isValid()||index.row()<0||index.column()<0||index.row()>=rowCount()||index.column()>=columnCount())return {};
    if(role==Qt::TextAlignmentRole)return int(Qt::AlignRight|Qt::AlignVCenter);
    if(role!=Qt::DisplayRole&&role!=Qt::ToolTipRole)return {};
    const bool stored=mode==1||(mode==2&&index.column()%2);
    const int column=mode==2?index.column()/2:index.column();
    const size_t position=size_t(index.row())*plane->cols+size_t(column);
    if(stored&&plane->stored_semantics==DataSource::StoredSemantics::Unavailable)return "unavailable";
    const auto &values=stored?plane->stored_x:plane->values;
    if(position>=values.size())return {};
    return std::isfinite(values[position])?QString::number(values[position],'g',17):QString("nan");
  }
  QVariant headerData(int section,Qt::Orientation orientation,int role=Qt::DisplayRole) const override {
    if(role==Qt::ToolTipRole)return "ecCodes decoded values; Quantized X is a validated reconstruction, not packed bytes. Indices are zero-based.";
    if(role!=Qt::DisplayRole)return {};
    if(orientation==Qt::Vertical)return section;
    return QString("%1\n%2").arg(mode==2?section/2:section).arg(mode==1||(mode==2&&section%2)?"Quantized X":"decoded");
  }
  void set(std::shared_ptr<const DataSource::PlaneData> value={},std::shared_ptr<void> lease={}) {
    beginResetModel();plane=std::move(value);budget=std::move(lease);endResetModel();
  }
};
}

GribTableView::GribTableView(QString file,QString target,int mode)
  :m_file(std::move(file)),m_target(std::move(target)),m_mode(mode)
{
  setObjectName("gribTableView");setWindowTitle(m_target+" - GRIB Data Grid");
  m_source_stamp=slice_stamp(m_file);
  m_info=GribData::describe(m_file,m_target);
  auto *layout=new QVBoxLayout(this);auto *tools=new QToolBar(this);layout->addWidget(tools);
  const size_t spatial=m_info.rectangular?2:1;
  for(size_t axis=0;axis+spatial<m_info.shape.size();++axis) {
    tools->addWidget(new QLabel(m_info.axes[axis].name,tools));auto *spin=new QSpinBox(tools);
    spin->setRange(0,int(std::min<size_t>(m_info.shape[axis]-1,INT_MAX)));spin->setKeyboardTracking(false);
    spin->setObjectName(QString("gribLayer%1").arg(axis));spin->setAccessibleName(m_info.axes[axis].name+" index0");
    spin->setToolTip(tr("Zero-based index. Source labels are available in Axes / Properties."));
    tools->addWidget(spin);m_layers.push_back(spin);
    connect(spin,&QSpinBox::valueChanged,this,[this]{refresh();emit layer_changed();});
  }
  m_export=tools->addAction(tr("Export current CSV"));m_export->setObjectName("gribGridExportCsv");m_export->setEnabled(false);
  auto *model=new GribTableModel(this);m_model=model;model->mode=mode;
  auto *table=new AnalysisTableView(this);table->setObjectName("gribDataTable");table->setAccessibleName(tr("GRIB data grid cells"));table->setModel(model);layout->addWidget(table,1);
  tools->addAction(table->copyAction());tools->addAction(table->autoFitAction());
  m_status=new QLabel(this);m_status->setObjectName("gribGridStatus");m_status->setAccessibleName("GRIB grid status");m_status->setWordWrap(true);layout->addWidget(m_status);
  m_reader=new DataReadController;
  connect(m_reader,&DataReadController::completed,this,[this,model,table](auto data,auto lease){
    if(slice_stamp(m_file)!=m_source_stamp){model->set();m_status->setText("SourceChanged: close and reopen source");return;}
    // Bind after the asynchronous shape is known so large-table accessibility stays bounded.
    table->setModel(nullptr);model->set(data,lease);table->setModel(model);
    m_loaded_stamp=m_source_stamp;setProperty("ready",true);m_export->setEnabled(true);
    m_status->setText(tr("%1 x %2 | %3 | ecCodes decoded values; %4").arg(data->rows).arg(data->cols).arg(data->units.isEmpty()?QString("unknown units"):data->units,m_mode==0?tr("Quantized X not requested"):data->stored_description));
  });
  connect(m_reader,&DataReadController::failed,this,[this,model](const QString &error){model->set();m_loaded_stamp={};setProperty("ready",false);m_export->setEnabled(false);m_status->setText(error);});
  connect(m_export,&QAction::triggered,this,[this]{
    if(!ready())return;
    const auto path=QFileDialog::getSaveFileName(this,tr("Export current GRIB plane"),{},"CSV (*.csv)");if(path.isEmpty())return;
    const bool overwrite=QFileInfo::exists(path);
    if(overwrite&&QMessageBox::question(this,tr("Overwrite"),tr("Replace existing output?"))!=QMessageBox::Yes)return;
    const auto error=export_csv(path,overwrite);if(!error.isEmpty())QMessageBox::warning(this,tr("Export"),error);
  });
  if(mode<0||mode>2)m_info.error="Invalid GRIB grid mode";
  if(!m_info.error.isEmpty()){setProperty("ready",false);m_status->setText(m_info.error_code+": "+m_info.error);return;}
  refresh();
}

GribTableView::~GribTableView(){m_reader->disconnect(this);m_reader->dispose();}
void GribTableView::refresh() {
  static_cast<GribTableModel*>(m_model)->set();m_loaded_stamp={};setProperty("ready",false);m_export->setEnabled(false);m_status->setText(tr("Reading GRIB plane..."));
  std::vector<size_t> indices;for(auto *spin:m_layers)indices.push_back(size_t(spin->value()));
  m_reader->read(m_file,m_target,indices,false,m_mode!=0);
}
QString GribTableView::export_csv(const QString &path,bool overwrite) const {
  const auto *model=static_cast<const GribTableModel*>(m_model);
  if(!ready()||!model->plane)return "No completed GRIB plane to export";
  if(slice_same_file(path,m_file))return "Cannot overwrite a source dataset";
  if(slice_stamp(m_file)!=m_loaded_stamp)return "SourceChanged: close and reopen source";
  QJsonArray indices;for(auto *spin:m_layers)indices.append(QString::number(spin->value()));
  QJsonObject metadata{{"file",m_file},{"object",m_target},{"source_session_id",m_info.object.source_session_id},{"indices0",indices},{"reader","ecCodes"},{"decode_profile",model->plane->decode_profile},{"units",model->plane->units},{"values",m_mode==0?"decoded":m_mode==1?"reconstructed-quantized-X":"decoded-and-reconstructed-quantized-X"},{"quantized_semantics",model->plane->stored_description},{"missing","nan"},{"source_stamp",m_loaded_stamp}};
  return slice_write_atomic(path,overwrite,[model,metadata](QIODevice &file){
    QTextStream text(&file);text.setLocale(QLocale::c());
    text<<"# grib_grid_schema=1\n# metadata="<<QJsonDocument(metadata).toJson(QJsonDocument::Compact)<<'\n';
    for(int row=0;row<model->rowCount();++row){for(int col=0;col<model->columnCount();++col){if(col)text<<',';text<<model->data(model->index(row,col)).toString();}text<<'\n';}
    text.flush();return text.status()==QTextStream::Ok?QString():QString("CSV write failed");
  });
}
int GribTableView::layer_axis_count() const {return int(m_layers.size());}
QString GribTableView::layer_axis_name(int axis) const {return axis>=0&&axis<int(m_layers.size())?m_info.axes[size_t(axis)].name:QString();}
int GribTableView::layer_count(int axis) const {return axis>=0&&axis<int(m_layers.size())?m_layers[size_t(axis)]->maximum()+1:0;}
int GribTableView::layer_index(int axis) const {return axis>=0&&axis<int(m_layers.size())?m_layers[size_t(axis)]->value():0;}
void GribTableView::set_layer_index(int axis,int index) {if(axis>=0&&axis<int(m_layers.size())&&index>=0&&index<layer_count(axis))m_layers[size_t(axis)]->setValue(index);}
