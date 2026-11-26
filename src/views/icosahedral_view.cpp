// Existing OpenGL globe + native-grid reconstruction. Camera never changes values.
#include "icosahedral_view.hpp"
#include "ico_3d_canvas.hpp"
#include "ico_atlas_render.hpp"
#include "raster_view.hpp"
#include "raster_budget.hpp"
#include "read_scheduler.hpp"
#include "slice_io.hpp"
#include <QThreadPool>
#include <algorithm>
#include <cmath>

namespace {
QList<IcosahedralView*> sessions;
int active_tasks=0;
}
class IcosahedralCanvas:public QWidget {
public:
  explicit IcosahedralCanvas(QWidget *parent):QWidget(parent){setObjectName("icosahedralCanvas");setAccessibleName("Five slices and source-grid analysis canvas");setMinimumSize(200,180);setMouseTracking(true);}
  std::shared_ptr<const Icosahedral::Result> data;
  HdfColorMap::Palette palette;
  HdfColorMap::Mapping mapping;
  QImage raster;
  IcoAtlasRenderer atlas_renderer;
  bool current=false,frame_pending=false,seams=true;
  double zoom=1;
  QPointF pan,last;
  int mode=0;
  void reset_camera(){zoom=1;pan={};update();}
  QRectF image_rect()const{QSizeF size=raster.size();size.scale(QSizeF(std::max(1,width()-30),std::max(1,height()-42)),Qt::KeepAspectRatio);if(mode>=4)size*=zoom;return QRectF(QPointF((width()-size.width())/2,(height()-size.height())/2)+(mode>=4?pan:QPointF()),size);}
protected:
  void paintEvent(QPaintEvent*)override{
    frame_pending=false;
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#f4f7fa"));
    if(!data){p.setPen(Qt::darkGray);p.drawText(rect().adjusted(12,12,-12,-12),Qt::AlignCenter|Qt::TextWordWrap,"Run analysis to generate the mesh and sample a field");return;}
    if(mode<2){p.setPen(Qt::darkRed);p.drawText(rect().adjusted(12,12,-12,-12),Qt::AlignCenter|Qt::TextWordWrap,"OpenGL 3.3 requires a supported window platform. Select a source-grid mode for headless analysis.");return;}
    if(raster.isNull())return;p.drawImage(image_rect(),raster);p.setPen(Qt::darkGray);p.drawText(QRectF(10,height()-25,width()-20,20),Qt::AlignCenter,mode>=4?"5 slices | wheel: zoom; drag: pan; double-click: Fit":"Original source row/column order (not a new geographical projection)");
  }
  void wheelEvent(QWheelEvent *event)override{if(mode<4){QWidget::wheelEvent(event);return;}zoom=std::clamp(zoom*std::pow(1.2,event->angleDelta().y()/120.),.5,12.);update();event->accept();}
  void mousePressEvent(QMouseEvent *event)override{last=event->position();event->accept();}
  void mouseMoveEvent(QMouseEvent *event)override{
    if(mode<4)return;
    if(event->buttons()&Qt::LeftButton){pan+=event->position()-last;update();}
    else if(current&&data&&!raster.isNull()){const auto target=image_rect();const auto point=event->position()-target.topLeft();const auto hit=atlas_renderer.pick(int(std::floor(point.x()*raster.width()/target.width())),int(std::floor(point.y()*raster.height()/target.height())),*data);
      setToolTip(hit.node>=0?QString("Chart %1 | face #%2 | node #%3 | %4 %5 (nearest node sample)").arg(hit.chart+1).arg(hit.face).arg(hit.node).arg(std::isfinite(hit.value)?QString::number(hit.value,'g',10):"missing / masked").arg(data->source.units):"Outside five charts");
    }else setToolTip({});last=event->position();event->accept();
  }
  void mouseDoubleClickEvent(QMouseEvent *event)override{if(mode>=4)reset_camera();event->accept();}
};

IcosahedralView::IcosahedralView(RasterView *input,bool own_source):source(input),following(input!=nullptr){
  if(input&&own_source){input->setParent(this);input->hide();}
  sessions.append(this);setObjectName("icosahedralView");setWindowTitle(tr("Icosahedral 5 slices"));
  auto *layout=new QVBoxLayout(this);auto *row=new QHBoxLayout;
  row->addWidget(new QLabel(tr("Level"),this));level=new QSpinBox(this);level->setObjectName("icoLevel");level->setRange(0,6);level->setValue(3);row->addWidget(level);
  auto *run=new QPushButton(tr("Run / 运行"),this);run->setObjectName("icoRun");row->addWidget(run);
  auto *cancel=new QPushButton(tr("Cancel"),this);cancel->setObjectName("icoCancel");row->addWidget(cancel);
  row->addStretch();auto *fit=new QPushButton(tr("Fit"),this);fit->setObjectName("icoFit");row->addWidget(fit);layout->addLayout(row);
  auto *colors=new QHBoxLayout;mode=new QComboBox(this);mode->setObjectName("icoDisplayMode");mode->addItems({tr("OpenGL 3D: node field"),tr("OpenGL 3D: face area (sr)"),tr("Restored source grid"),tr("Round-trip error"),tr("5 Unfolded Strips (20 Faces)"),tr("5 Rectangular Charts")});colors->addWidget(mode);
  QString error;palettes=HdfColorMap::load_registry(error);palette=new QComboBox(this);palette->setObjectName("icoPalette");
  for(const auto &item:palettes)palette->addItem(color_map_icon(item),item.name,item.id);palette->setCurrentIndex(std::max(0,palette->findData("hdfstudio:s2s-ocean")));colors->addWidget(palette,1);
  wire=new QCheckBox(tr("Edges"),this);wire->setObjectName("icoEdges");wire->setChecked(true);colors->addWidget(wire);reverse=new QCheckBox(tr("Reverse"),this);reverse->setObjectName("icoReverse");colors->addWidget(reverse);layout->addLayout(colors);
  auto *notice=new QLabel(following?tr("Level and layer controls update automatically; Run applies source or Mask changes. Previous frame stays visible while updating. Regional exterior = NaN. Point interpolation; no mass-conservation claim."):tr("Analytic demonstration — not forecast data. Right-click a variable → Visual Analysis → Icosahedral 5 slices to analyze your field."),this);notice->setWordWrap(true);layout->addWidget(notice);
  // Reuse the original GL controls in a horizontal scroll strip, as in the source project.
  auto *scroll=new QScrollArea(this);auto *contents=new QWidget(scroll);auto *options=new QHBoxLayout(contents);options->setContentsMargins(0,0,0,0);options->setSizeConstraint(QLayout::SetMinimumSize);
  scroll->setWidget(contents);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);scroll->setFixedHeight(fontMetrics().height()+34);layout->addWidget(scroll);
  auto *shape=new QComboBox(this);shape->setObjectName("ico3DShapeCombo");shape->addItems({"Icosphere (Unit Sphere)","Polyhedron (20 Flat Faces)"});options->addWidget(shape);
  auto *morph=new QSlider(Qt::Horizontal,this);morph->setObjectName("ico3DMorphSlider");morph->setRange(0,100);morph->setValue(100);morph->setMaximumWidth(120);options->addWidget(morph);
  canvas_stack=new QStackedWidget(this);canvas=new IcosahedralCanvas(this);canvas_stack->addWidget(canvas);
  const QString platform=QGuiApplication::platformName();if(platform!="offscreen"&&platform!="minimal"){gl_canvas=new Ico3DCanvas(this);canvas_stack->addWidget(gl_canvas);canvas_stack->setCurrentWidget(gl_canvas);}
  auto toggle=[&](const QString &name,const QString &id,bool initial,bool Ico3DCanvas::*member){auto *box=new QCheckBox(name,this);box->setObjectName(id);box->setChecked(initial);options->addWidget(box);connect(box,&QCheckBox::toggled,this,[this,member](bool value){if(gl_canvas){gl_canvas->*member=value;gl_canvas->update();}});};
  toggle("20 Faces","ico3DBaseEdgesCheck",true,&Ico3DCanvas::base_edges);toggle("Graticule","ico3DGraticuleCheck",true,&Ico3DCanvas::graticule);toggle("Turntable","ico3DTurntableCheck",false,&Ico3DCanvas::turntable);toggle("Shading","ico3DShadingCheck",false,&Ico3DCanvas::shading);
  auto *seams=new QCheckBox(tr("Seams"),this);seams->setObjectName("icoSeams");seams->setChecked(true);row->insertWidget(3,seams);if(gl_canvas)gl_canvas->seams=true;
  connect(seams,&QCheckBox::toggled,this,[this](bool value){canvas->seams=value;if(gl_canvas){gl_canvas->seams=value;gl_canvas->update();}if(mode->currentIndex()>=4)recolor();});
  auto *preset=new QComboBox(this);preset->setObjectName("ico3DPresetCombo");preset->addItems({"Default","North Pole","South Pole","0 E","105 E (East Asia)","180 E"});options->addWidget(preset);options->addStretch();
  connect(preset,&QComboBox::activated,this,[this](int value){if(gl_canvas)gl_canvas->reset_camera(value);});
  connect(shape,&QComboBox::currentIndexChanged,this,[morph](int value){morph->setValue(value==0?100:0);});connect(morph,&QSlider::valueChanged,this,[this,shape](int value){QSignalBlocker blocker(shape);shape->setCurrentIndex(value==0?1:0);if(gl_canvas)gl_canvas->set_morph(value/100.);});
  gl_status=new QLabel(this);gl_status->setObjectName("icoGlStatus");gl_status->setWordWrap(true);layout->addWidget(gl_status);
  if(gl_canvas){connect(gl_canvas,&Ico3DCanvas::gl_status_changed,this,[this]{gl_status->setText(gl_canvas->gl_error().isEmpty()?tr("OpenGL 3D | drag: rotate; right/middle: pan; wheel: zoom; double-click: reset | Unlit preserves palette; Shading changes brightness"):gl_canvas->gl_error());export_png->setEnabled(presented&&(mode->currentIndex()>=2||gl_canvas->ready()));});gl_status->setText(tr("OpenGL 3.3 Core | existing mesh renderer"));}else gl_status->setText(tr("OpenGL unavailable on this window platform; numerical analysis and 2D output remain available"));
  layout->addWidget(canvas_stack,1);bar=new ColorBar(this);bar->setObjectName("icoColorBar");bar->set_combined_missing_mask(true);layout->addWidget(bar);
  status=new QLabel(this);status->setObjectName("icoStatus");status->setWordWrap(true);status->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(status);
  connect(mode,&QComboBox::currentIndexChanged,this,[scroll,this](int index){scroll->setVisible(index<2);gl_status->setVisible(index<2);});
  auto *outputs=new QHBoxLayout;export_nc=new QPushButton(tr("Export mesh + fields NetCDF"),this);export_nc->setObjectName("icoExportNetcdf");export_nc->setEnabled(false);outputs->addWidget(export_nc);
  export_png=new QPushButton(tr("Export PNG"),this);export_png->setObjectName("icoExportPng");export_png->setEnabled(false);outputs->addWidget(export_png);outputs->addStretch();layout->addLayout(outputs);
  connect(run,&QPushButton::clicked,this,&IcosahedralView::run_analysis);connect(cancel,&QPushButton::clicked,this,[this]{invalidate(tr("Cancelled; Run to start again"));});connect(fit,&QPushButton::clicked,this,[this]{if(mode->currentIndex()>=4)canvas->reset_camera();else if(gl_canvas)gl_canvas->reset_camera();});
  level_update=new QTimer(this);level_update->setSingleShot(true);level_update->setInterval(150);
  connect(level_update,&QTimer::timeout,this,&IcosahedralView::run_analysis);
  connect(level,&QSpinBox::valueChanged,this,[this]{invalidate(tr("Updating L%1… (previous frame; values unavailable)").arg(level->value()),true);auto_requested=true;level_update->start();});
  connect(palette,&QComboBox::currentIndexChanged,this,[this]{recolor();});connect(reverse,&QCheckBox::toggled,this,[this]{recolor();});connect(wire,&QCheckBox::toggled,this,[this]{recolor();});
  connect(mode,&QComboBox::currentIndexChanged,this,[this]{if(mode->currentIndex()==3){QSignalBlocker block(palette);const int index=palette->findData("hdfstudio:s2s-anomaly");if(index>=0)palette->setCurrentIndex(index);}recolor();});
  connect(ReadScheduler::instance(),&ReadScheduler::available,this,&IcosahedralView::pump);
  source_wait=new QTimer(this);source_wait->setInterval(20);
  connect(source_wait,&QTimer::timeout,this,[this]{
    if(!auto_requested&&!awaiting_grid){source_wait->stop();return;}
    if(!source){invalidate(tr("Source closed"));return;}
    if(source->render_pending())return;
    if(!source->plane_snapshot().values){
      const auto *legend=source->findChild<QLabel*>("rasterLegend");
      invalidate(tr("Source layer unavailable: %1").arg(legend?legend->text():tr("No completed source plane")));return;
    }
    if(auto_requested)run_analysis();else try_source_coordinates();
  });
  connect(export_nc,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getSaveFileName(this,tr("Export graph-model mesh and fields"),"icosahedral.nc",tr("NetCDF (*.nc)"));if(path.isEmpty())return;const auto error=export_data(path);if(!error.isEmpty())QMessageBox::warning(this,tr("Icosahedral export"),error);else status->setText(status->text()+tr("\nExported: %1").arg(QFileInfo(path).fileName()));});
  connect(export_png,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getSaveFileName(this,tr("Export analysis image"),"icosahedral.png",tr("PNG (*.png)"));if(path.isEmpty())return;if(presented&&!presented->source.file_path.isEmpty()&&slice_same_file(path,presented->source.file_path)){QMessageBox::warning(this,tr("Export"),tr("Cannot overwrite source"));return;}const auto image=image_for_export();if(image.isNull()){QMessageBox::warning(this,tr("Export"),tr("No current analysis; Run again before exporting"));return;}const auto error=slice_write_atomic(path,false,[&](QIODevice &device){return image.save(&device,"PNG")?QString():tr("Cannot encode PNG");});if(!error.isEmpty())QMessageBox::warning(this,tr("Export"),error);});
  if(source){
    connect(source,&RasterView::plane_snapshot_changed,this,[this]{if(awaiting_grid)try_source_coordinates();else if(!auto_requested)invalidate(tr("Source layer or Mask changed; Run for the current snapshot"));});
    connect(source,&RasterView::layer_changed,this,[this]{emit layer_changed();});
    connect(source,&RasterView::geographic_grid_ready,this,[this](auto grid,auto lease){if(!awaiting_grid)return;waiting_grid=grid;waiting_grid_lease=lease;try_source_coordinates();});
    connect(source,&QObject::destroyed,this,[this]{source.clear();invalidate(tr("Source image closed; reopen analysis from an Image View"));});
  }
  if(!error.isEmpty())status->setText(error);else QTimer::singleShot(0,this,&IcosahedralView::run_analysis);
}
IcosahedralView::~IcosahedralView(){level_update->stop();source_wait->stop();if(source)source->disconnect(this);sessions.removeAll(this);generation->fetch_add(1);pending={};}
bool IcosahedralView::any_busy(){return active_tasks>0;}
void IcosahedralView::cancel_all(){for(auto *view:sessions)view->invalidate("Closing analysis");}
void IcosahedralView::invalidate(const QString &reason,bool keep_frame){level_update->stop();source_wait->stop();generation->fetch_add(1);pending={};awaiting_grid=false;auto_requested=false;requested={};waiting_grid.reset();waiting_grid_lease.reset();presented.reset();canvas->current=false;canvas->setToolTip({});if(keep_frame){if(gl_canvas)gl_canvas->set_frame_current(false);}else{canvas->data.reset();canvas->raster={};canvas->update();if(gl_canvas)gl_canvas->clear_data();bar->clear();emit stop_playback_requested();}export_nc->setEnabled(false);export_png->setEnabled(false);status->setText(reason);setProperty("icoReady",false);}
void IcosahedralView::run_analysis(){
  const bool keep_frame=bool(canvas->data);invalidate(tr("Preparing immutable snapshot… (previous frame while updating)"),keep_frame);
  if(following){if(!source){invalidate(tr("Source closed"));return;}requested=source->plane_snapshot();if(!requested.values||source->render_pending()){status->setText(tr("Waiting for source layer…"));auto_requested=true;source_wait->start();return;}source_stamp=GeoGrid::stamp(requested.file_path);awaiting_grid=true;source_wait->start();source->request_geographic_grid();}
  else {GeoGrid::Grid grid;auto snapshot=Icosahedral::demo(grid);launch(snapshot,std::make_shared<const GeoGrid::Grid>(std::move(grid)),{});}
}
void IcosahedralView::launch(PlaneSnapshot snapshot,std::shared_ptr<const GeoGrid::Grid> grid,std::shared_ptr<void> grid_lease){
  if(!grid||!grid->error.isEmpty()){invalidate(grid?grid->error:tr("No verified coordinates"));return;}
  const int selected=level->value();auto lease=reserve_raster_bytes(Icosahedral::estimate_bytes(snapshot.rows*snapshot.cols,selected));
  if(!lease){invalidate(tr("Analysis memory budget exceeded; use a lower level or close other views"));return;}
  const auto token=generation;const auto revision=token->load();
  pending=[snapshot,grid,grid_lease,lease,token,revision,selected]{auto result=Icosahedral::analyze(snapshot,*grid,selected,[token,revision]{return token->load()!=revision;});result->storage_lease=lease;return result;};pending_revision=revision;status->setText(tr("Analyzing L%1…").arg(selected));pump();
}
void IcosahedralView::try_source_coordinates(){
  if(!awaiting_grid||!waiting_grid||!source)return;
  if(!waiting_grid->error.isEmpty()){invalidate(waiting_grid->error);return;}
  const auto current=source->plane_snapshot();
  // A GRIB coordinate upgrade re-reads the same plane. Wait for its coherent
  // publication, then verify exact source identity/Mask instead of old revision.
  if(!current.values||source->render_pending())return;
  const auto equal=[](double a,double b){return a==b||(!std::isfinite(a)&&!std::isfinite(b));};
  if(GeoGrid::stamp(current.file_path)!=source_stamp||current.file_path!=requested.file_path||current.variable_path!=requested.variable_path||current.leading_indices!=requested.leading_indices||current.rows!=requested.rows||current.cols!=requested.cols||!requested.values||current.values->size()!=requested.values->size()||!std::equal(current.values->begin(),current.values->end(),requested.values->begin(),equal)){
    invalidate(tr("Source layer or Mask changed while reading coordinates; Run again"));return;
  }
  awaiting_grid=false;auto grid=std::move(waiting_grid);auto lease=std::move(waiting_grid_lease);requested={};launch(current,grid,lease);
}
void IcosahedralView::pump(){
  if(running||!pending||!ReadScheduler::instance()->acquire())return;
  running=true;++active_tasks;auto work=std::move(pending);pending={};const quint64 revision=pending_revision;QPointer<IcosahedralView> guard(this);
  QThreadPool::globalInstance()->start([work,guard,revision]{
    std::shared_ptr<Icosahedral::Result> result;
    try{result=work();}catch(const std::exception &e){result=std::make_shared<Icosahedral::Result>();result->error=QString::fromUtf8(e.what());}
    QMetaObject::invokeMethod(QCoreApplication::instance(),[guard,revision,result]{
      --active_tasks;
      if(guard){guard->running=false;if(guard->generation->load()==revision){
        if(!result->error.isEmpty())guard->invalidate(result->error);
        else if(guard->following&&(!guard->source||GeoGrid::stamp(result->source.file_path)!=guard->source_stamp||guard->source->plane_snapshot().revision!=result->source.revision))guard->invalidate("SourceChanged; Run again");
         else {if(guard->mode->currentIndex()>=2&&guard->gl_canvas)guard->gl_canvas->clear_data();guard->presented=result;guard->canvas->data=result;guard->setProperty("icoReady",true);guard->export_nc->setEnabled(true);guard->export_png->setEnabled(true);
           if(!guard->camera_initialized&&guard->gl_canvas){if(!result->grid.longitude.periodic)guard->gl_canvas->center_on((result->grid.longitude.centers.front()+result->grid.longitude.centers.back())*.5,(result->grid.latitude.centers.front()+result->grid.latitude.centers.back())*.5);guard->camera_initialized=true;}
           guard->recolor();}
      }}
      ReadScheduler::instance()->release();if(guard)guard->pump();
    },Qt::QueuedConnection);
  });
}
void IcosahedralView::recolor(){
  // A retained result is display-only until the selected level/layer completes.
  // Mode/color controls must still work while presented is invalid, without
  // re-enabling old probes or exports or replacing the in-progress status.
  const auto displayed=presented?presented:canvas->data;
  const bool current=bool(presented);const int display=mode->currentIndex();
  canvas_stack->setCurrentWidget(display<2&&gl_canvas?static_cast<QWidget*>(gl_canvas):static_cast<QWidget*>(canvas));
  if(!displayed||palette->currentIndex()<0||palette->currentIndex()>=int(palettes.size()))return;
  const auto &values=display==1?displayed->mesh.area:display==2?displayed->restored:display==3?displayed->residual:displayed->node_values;
  double low=INFINITY,high=-INFINITY;size_t valid=0;for(double v:values)if(std::isfinite(v)){low=std::min(low,v);high=std::max(high,v);++valid;}
  HdfColorMap::Mapping mapping;mapping.minimum=valid?low:0;mapping.maximum=valid?high:1;mapping.reversed=reverse->isChecked();
  if(display==3){const double limit=std::max(std::abs(mapping.minimum),std::abs(mapping.maximum));mapping.minimum=-limit;mapping.maximum=limit;if(limit>0)mapping.normalization=HdfColorMap::Normalization::Centered;}
  canvas->palette=palettes[size_t(palette->currentIndex())];canvas->mapping=mapping;canvas->mode=display;
  if(display<2&&gl_canvas){gl_canvas->sub_mesh=wire->isChecked();gl_canvas->set_data(displayed,display==1,canvas->palette,mapping);gl_canvas->set_frame_current(current);}
  export_png->setEnabled(current&&(display>=2||(gl_canvas&&gl_canvas->ready()&&gl_canvas->gl_error().isEmpty())));
  if(display>=4){canvas->atlas_renderer.rectangular=display==5;canvas->atlas_renderer.mesh_lines=wire->isChecked();canvas->atlas_renderer.seams=canvas->seams;canvas->raster=canvas->atlas_renderer.render(*displayed,canvas->palette,mapping);}
  else if(display>=2){canvas->raster=QImage(int(displayed->source.cols),int(displayed->source.rows),QImage::Format_ARGB32);for(size_t r=0;r<displayed->source.rows;++r){auto *pixels=reinterpret_cast<QRgb*>(canvas->raster.scanLine(int(r)));for(size_t c=0;c<displayed->source.cols;++c)pixels[c]=HdfColorMap::map_value(canvas->palette,mapping,values[r*displayed->source.cols+c]);}}
  canvas->current=current;canvas->frame_pending=display>=2;
  const QString units=display==1?"sr":displayed->source.units;bar->set_mapping(canvas->palette,mapping,units,{},valid>0);canvas->update();
  if(!current)return;
  status->setText(tr("L%1 | V=%2 E=%3 F=%4 | Finite: %5/%6 | Paired: %7 | Bias=%8 MAE=%9 RMSE=%10 %11\n%12\n%13")
    .arg(presented->mesh.level).arg(presented->mesh.xyz.size()).arg(presented->mesh.edges.size()).arg(presented->mesh.faces.size()).arg(valid).arg(values.size()).arg(presented->compared)
    .arg(presented->bias,0,'g',8).arg(presented->mae,0,'g',8).arg(presented->rmse,0,'g',8).arg(presented->source.units,presented->source.variable_path,tr("Node/edge/face point values; face vertex mean is NOT a conservative cell mean. Area is on a unit sphere.")));
}
QString IcosahedralView::export_data(const QString &path)const{
  if(!presented)return "No current analysis";
  if(following&&(!source||source->render_pending()||GeoGrid::stamp(presented->source.file_path)!=source_stamp||source->plane_snapshot().revision!=presented->source.revision))return "SourceChanged; Run again before exporting";
  return Icosahedral::export_netcdf(path,*presented);
}
int IcosahedralView::layer_axis_count()const{return source?source->layer_axis_count():0;}
QString IcosahedralView::layer_axis_name(int axis)const{return source?source->layer_axis_name(axis):QString();}
int IcosahedralView::layer_count(int axis)const{return source?source->layer_count(axis):0;}
int IcosahedralView::layer_index(int axis)const{return source?source->layer_index(axis):0;}
void IcosahedralView::set_layer_index(int axis,int index){
  if(!source||axis<0||axis>=source->layer_axis_count())return;
  index=layer_clamp(index,source->layer_count(axis));if(index==source->layer_index(axis))return;
  invalidate(tr("Reading next layer… (previous frame; values unavailable)"),true);auto_requested=true;source_wait->start();source->set_layer_index(axis,index);
}
bool IcosahedralView::ready()const{
  return presented&&!auto_requested&&!awaiting_grid&&!running&&!pending&&(!following||(source&&!source->render_pending()&&source->plane_snapshot().revision==presented->source.revision))&&
    (mode->currentIndex()>=2?!canvas->frame_pending:!gl_canvas||(gl_canvas->ready()&&!gl_canvas->data_pending()));
}
QImage IcosahedralView::image_for_export()const{
  if(!presented)return {};
  if(following&&(!source||source->render_pending()||GeoGrid::stamp(presented->source.file_path)!=source_stamp||source->plane_snapshot().revision!=presented->source.revision))return {};
  const bool globe=mode->currentIndex()<2;if(globe&&(!gl_canvas||!gl_canvas->ready()||!gl_canvas->gl_error().isEmpty()))return {};
  QImage frame;if(globe){frame=gl_canvas->grabFramebuffer();if(frame.isNull())return {};frame.setDevicePixelRatio(1);}
  const bool atlas=mode->currentIndex()>=4;if(atlas){frame=canvas->raster;if(frame.isNull())return {};}
  const int width=(globe||atlas)?frame.width():canvas->width(),height=(globe||atlas)?frame.height():canvas->height();
  QImage image(QSize(width,height+bar->height()),QImage::Format_ARGB32);image.fill(Qt::white);QPainter painter(&image);if(globe||atlas)painter.drawImage(0,0,frame);else canvas->render(&painter);painter.translate(0,height);if(globe||atlas)painter.scale(double(width)/bar->width(),1);bar->render(&painter);return image;
}
