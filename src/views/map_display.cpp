#include "map_display.hpp"
#include "read_scheduler.hpp"
#include "raster_budget.hpp"
#include <QtWidgets>
#include <QThreadPool>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>

namespace {
QList<MapDisplay*> map_sessions;
int active_map_tasks=0; // GUI-thread owned; also includes tasks from deleted views.
const QStringList projection_keys={"source","lonlat","north-polar","south-polar","east-asia-lambert","mercator"};
// Shape-only display is scale invariant. Never attach a guessed physical model to the source.
MapProjection::EarthModel display_model(const std::shared_ptr<const GeoGrid::Grid> &grid){
  return grid&&grid->earth.valid()?grid->earth:MapProjection::EarthModel{1,1,"Unit-sphere display only; source Earth model unknown; distances unavailable"};
}
}
MapDisplay::MapDisplay(QWidget *parent):QWidget(parent){map_sessions.append(this);setObjectName("mapCanvas");setMouseTracking(true);setMinimumSize(200,120);timer.setSingleShot(true);timer.setInterval(35);connect(&timer,&QTimer::timeout,this,&MapDisplay::launch);connect(ReadScheduler::instance(),&ReadScheduler::available,this,&MapDisplay::pump);load_preferences();}
MapDisplay::~MapDisplay(){map_sessions.removeAll(this);generation->fetch_add(1);pending={};timer.stop();}
bool MapDisplay::any_busy(){if(active_map_tasks)return true;for(const auto *view:map_sessions)if(view->busy())return true;return false;}
void MapDisplay::cancel_all(){for(auto *view:map_sessions)view->invalidate("Closing map display");}
void MapDisplay::set_status(const QString &text){status=text;setProperty("mapStatus",text);setAccessibleName("Map: "+text);emit status_changed(text);update();}
void MapDisplay::set_grid(std::shared_ptr<const GeoGrid::Grid> data,std::shared_ptr<void> lease){grid=std::move(data);grid_lease=std::move(lease);spec.earth=display_model(grid);needs_fit=true;if(active())schedule(true);}
void MapDisplay::set_source(QImage image,std::shared_ptr<const std::vector<double>> data,std::shared_ptr<const std::vector<double>> decoded,std::shared_ptr<void> lease){if(data==values&&decoded==original&&source==image)return;source=std::move(image);values=std::move(data);original=std::move(decoded);source_lease=std::move(lease);if(active())schedule();}
void MapDisplay::hold_frame(const QString &message){generation->fetch_add(1);pending={};timer.stop();presented.reset();values.reset();original.reset();source={};source_lease.reset();setToolTip({});setProperty("mapProbe",{});set_status(message);}
void MapDisplay::invalidate(const QString &reason){hold_frame(reason);painted.reset();}
void MapDisplay::fit_view(){if(active())schedule(true);}
void MapDisplay::zoom(double factor){if(!active()||viewport.width<=0||!std::isfinite(factor)||factor<=0)return;const double cx=(viewport.xmin+viewport.xmax)/2,cy=(viewport.ymin+viewport.ymax)/2;
  const double dx=(viewport.xmax-viewport.xmin)/factor/2,dy=(viewport.ymax-viewport.ymin)/factor/2;if(dx<1e-10||dx>1e12||dy<1e-10||dy>1e12)return;viewport.xmin=cx-dx;viewport.xmax=cx+dx;viewport.ymin=cy-dy;viewport.ymax=cy+dy;schedule();}
void MapDisplay::schedule(bool fit){generation->fetch_add(1);pending={};presented.reset();setToolTip({});setProperty("mapProbe",{});needs_fit=needs_fit||fit;set_status(painted?"Rendering projection... (previous image; map values unavailable)":"Rendering projection...");timer.start();}
void MapDisplay::launch(){if(!active())return;
  auto fail=[this](const QString &reason){presented.reset();painted.reset();set_status(reason);};
  if(!grid||!grid->error.isEmpty()){fail(grid?grid->error:"Coordinates not ready");return;}
  if(!values||!original||source.isNull())return; // A source read is pending; hold its last image, never its probes.
  if(!coastline){static std::weak_ptr<const Coastline::Data> shared;coastline=shared.lock();if(!coastline){coast_lease=reserve_raster_bytes(32ull*1024*1024);auto data=std::make_shared<Coastline::Data>();if(coast_lease){*data=Coastline::load(QCoreApplication::applicationDirPath()+"/resources/coastlines/low/coastlines_l.json",Coastline::bundled_hash());data->storage_lease=coast_lease;}else data->error="Coastline budget unavailable";coastline=data;shared=data;}}
  const double dpr=devicePixelRatioF();const int w=qRound(width()*dpr),h=qRound(height()*dpr);QString error;
  if(needs_fit||viewport.width<=0)viewport=MapProjection::fit(spec,w,h,dpr,error);
  else {const double scale=(viewport.xmax-viewport.xmin)/viewport.width;const double cx=(viewport.xmin+viewport.xmax)/2,cy=(viewport.ymin+viewport.ymax)/2;viewport={cx-scale*w/2,cx+scale*w/2,cy-scale*h/2,cy+scale*h/2,dpr,w,h};}
  needs_fit=false;if(!error.isEmpty()){fail(error);return;}
  if(w<=0||h<=0||w>16*1024*1024/h){fail("Map canvas exceeds 16 megapixels");return;}
  // 13 bytes/result pixel + GUI image backing + source states and bounded overlay/QPainter copies.
  auto lease=reserve_raster_bytes(quint64(w)*h*21+quint64(values->size())+64ull*1024*1024);
  if(!lease){fail("Map memory budget exceeded");return;}
  const QJsonArray key{int(spec.mode),spec.lon0,spec.lat0,spec.lat1,spec.lat2,spec.lat_ts,spec.west,spec.span,spec.south,spec.north,spec.earth.a,spec.earth.b,viewport.xmin,viewport.xmax,viewport.ymin,viewport.ymax,w,h,dpr,coastline->revision};
  const QString current_key=QString::fromUtf8(QJsonDocument(key).toJson(QJsonDocument::Compact));
  MapRender::Input input;input.grid=grid;input.values=values;input.original=original;input.source=source;input.source_lease=source_lease;input.output_lease=lease;input.coastline=coastline;input.spec=spec;input.viewport=viewport;
  if(current_key==geometry_key)input.geometry=geometry;
  const auto revision=generation->load();const auto token=generation;const auto coordinates_lease=grid_lease;const auto coastline_lease=coast_lease;
  pending=[input,token,revision,coordinates_lease,coastline_lease]{return MapRender::render(input,[token,revision]{return token->load()!=revision;});};pending_revision=revision;
  setProperty("pendingGeometryKey",current_key);pump();
}
void MapDisplay::pump(){if(running||!pending)return;if(!ReadScheduler::instance()->acquire())return;auto task=std::move(pending);pending={};const auto revision=pending_revision;const auto key=property("pendingGeometryKey").toString();running=true;++active_map_tasks;const QPointer<MapDisplay> guard(this);
  QThreadPool::globalInstance()->start([guard,task,revision,key]{auto result=task();QMetaObject::invokeMethod(QCoreApplication::instance(),[guard,result,revision,key]{--active_map_tasks;ReadScheduler::instance()->release();if(!guard)return;guard->running=false;
    if(guard->generation->load()==revision){if(result->error.isEmpty()){guard->presented=guard->painted=result;guard->painted_spec=guard->spec;guard->painted_model_known=guard->grid->earth.valid();guard->geometry=result->geometry;guard->geometry_lease=result->lease;guard->geometry_key=key;guard->setProperty("mapReadyRevision",qulonglong(revision));guard->set_status(result->covered?QString("%1; source-grid nearest neighbour; %2").arg(MapProjection::name(guard->spec.mode),guard->grid->earth.valid()?guard->grid->earth.evidence:guard->spec.earth.evidence):"Current extent has no source data");emit guard->frame_ready();}else {guard->presented.reset();guard->painted.reset();guard->set_status(result->error);}}
    guard->pump();},Qt::QueuedConnection);});
}
void MapDisplay::paintEvent(QPaintEvent*){QPainter painter(this);painter.fillRect(rect(),QColor("#f7f7f7"));
  if(painted){const auto &out=*painted;const auto &spec=painted_spec;painter.setRenderHint(QPainter::SmoothPixmapTransform,false);painter.drawImage(rect(),out.image);
    painter.save();painter.scale(1/out.viewport.dpr,1/out.viewport.dpr);painter.setRenderHint(QPainter::Antialiasing,true);
    // Same geographic clipping as pixel sampling, including the polar circular boundary.
    if(spec.mode==MapProjection::Mode::NorthPolar||spec.mode==MapProjection::Mode::SouthPolar){MapProjection::Transform transform(spec);QPointF center,edge;transform.forward(spec.lon0,spec.lat0,center);transform.forward(spec.lon0,spec.mode==MapProjection::Mode::NorthPolar?spec.south:spec.north,edge);center=out.viewport.pixel(center);edge=out.viewport.pixel(edge);const double radius=QLineF(center,edge).length();QPainterPath path;path.addEllipse(center,radius,radius);painter.setClipPath(path);}
    if(out.geometry){painter.setPen(QPen(QColor(100,110,120,130),.6*out.viewport.dpr));for(const auto &line:out.geometry->graticule)painter.drawLine(line);
      if(coast_enabled){painter.setPen(QPen(coast_color,coast_width*out.viewport.dpr));for(const auto &line:out.geometry->coast)painter.drawLine(line);}}
    painter.restore();
    MapProjection::Transform label_transform(spec);
    // Choose a contrasting text colour from the raster, without boxes or hollow outlines.
    auto text_without_box=[&](const QRectF &box,int flags,const QString &text){const auto point=box.center();const int x=std::clamp(qRound(point.x()*out.image.width()/width()),0,out.image.width()-1),y=std::clamp(qRound(point.y()*out.image.height()/height()),0,out.image.height()-1);painter.setPen(qGray(out.image.pixel(x,y))<140?QColor("#f4f4f4"):QColor("#303840"));painter.drawText(box,flags,text);};
    QVector<QRectF> label_boxes;
    auto label=[&](double lon,double lat,const QString &text){QPointF projected;if(!MapProjection::contains(spec,lon,lat)||!label_transform.forward(lon,lat,projected))return;const auto pixel=out.viewport.pixel(projected)/out.viewport.dpr;if(pixel.x()<12||pixel.y()<72||pixel.x()>width()-48||pixel.y()>height()-28)return;const QRectF box(pixel.x()-24,pixel.y()-8,48,16);for(const auto &placed:label_boxes)if(placed.intersects(box.adjusted(-2,-2,2,2)))return;label_boxes.append(box);text_without_box(box,Qt::AlignCenter,text);};
    for(int lat=-60;lat<=60;lat+=30)label(spec.lon0,lat,QString::number(lat)+"°");
    const double midlat=(spec.south+spec.north)/2;
    const double label_lat=spec.mode==MapProjection::Mode::NorthPolar?std::max(spec.south+5.,30.):spec.mode==MapProjection::Mode::SouthPolar?std::min(spec.north-5.,-30.):midlat;
    for(int lon=-180;lon<180;lon+=30)label(lon,label_lat,QString::number(lon)+"°");
    const QString note=!painted_model_known?"Unit-sphere display · no physical distances · reference coastlines only":"30° graticule · reference coastlines (low); angular alignment; no datum transformation";
    const QRect note_box=painter.fontMetrics().boundingRect(QRect(8,property("externalStatus").toBool()?4:24,width()-16,40),Qt::TextWordWrap,note);
    text_without_box(note_box,Qt::TextWordWrap,note);
    if(out.geometry&&!out.geometry->diagnostic.isEmpty())painter.drawText(QRect(8,height()-42,width()-16,40),Qt::TextWordWrap,out.geometry->diagnostic);
  }
  if(!property("externalStatus").toBool()){painter.setPen(Qt::black);painter.fillRect(QRect(0,0,width(),22),QColor(255,255,255,225));painter.drawText(QRect(8,2,width()-16,20),Qt::AlignLeft|Qt::AlignVCenter,status);}
}
void MapDisplay::resizeEvent(QResizeEvent *event){QWidget::resizeEvent(event);if(active())schedule();}
void MapDisplay::wheelEvent(QWheelEvent *event){zoom(std::pow(1.25,std::clamp(event->angleDelta().y()/120.0,-8.0,8.0)));event->accept();}
void MapDisplay::mousePressEvent(QMouseEvent *event){if(event->button()==Qt::LeftButton&&presented){dragging=true;drag_start=event->pos();drag_viewport=viewport;setCursor(Qt::ClosedHandCursor);}}
void MapDisplay::mouseReleaseEvent(QMouseEvent*){dragging=false;unsetCursor();}
void MapDisplay::mouseMoveEvent(QMouseEvent *event){if(dragging){const double dx=(event->pos().x()-drag_start.x())*(drag_viewport.xmax-drag_viewport.xmin)/width(),dy=(event->pos().y()-drag_start.y())*(drag_viewport.ymax-drag_viewport.ymin)/height();viewport=drag_viewport;viewport.xmin-=dx;viewport.xmax-=dx;viewport.ymin+=dy;viewport.ymax+=dy;schedule();return;}
  if(!presented||!grid||!values)return;const auto &out=*presented;const int x=int(event->position().x()*out.viewport.dpr),y=int(event->position().y()*out.viewport.dpr);if(x<0||y<0||x>=out.viewport.width||y>=out.viewport.height)return;
  const size_t pixel=size_t(y)*out.viewport.width+x;double lon=0,lat=0;MapProjection::Transform transform(spec);QString text;
  if(transform.inverse(out.viewport.at(x+.5,y+.5),lon,lat))text=QString("Pixel lon %1°, lat %2°; ").arg(lon,0,'g',10).arg(lat,0,'g',10);
  text+=MapRender::state_name(MapRender::State(out.states[pixel]));const auto index=out.indices[pixel];
  if(index>=0){const size_t cols=grid->longitude.centers.size(),r=size_t(index)/cols,c=size_t(index)%cols;text+=QString("; source [%1,%2], lon %3°, lat %4°").arg(r).arg(c).arg(grid->longitude.centers[c],0,'g',10).arg(grid->latitude.centers[r],0,'g',10);if(out.states[pixel]==quint8(MapRender::State::Valid))text+=QString("; decoded %1").arg((*values)[size_t(index)],0,'g',15);if(std::abs(std::abs(lat)-90)<1e-9)text+="; pole longitude undefined (lon0 sampling convention)";}
  setToolTip(text);setProperty("mapProbe",text);
}
QString MapDisplay::apply_projection(const MapProjection::Spec &proposed){
  QString reason=MapProjection::validate(proposed);
  if(proposed.mode!=MapProjection::Mode::Source&&(!grid||!grid->error.isEmpty()))reason=grid?grid->error:"Coordinates not ready";
  if(!reason.isEmpty())return reason;
  const bool same_geometry=proposed.mode==spec.mode&&proposed.lon0==spec.lon0&&proposed.lat0==spec.lat0&&proposed.lat1==spec.lat1&&proposed.lat2==spec.lat2&&proposed.lat_ts==spec.lat_ts&&proposed.west==spec.west&&proposed.span==spec.span&&proposed.south==spec.south&&proposed.north==spec.north;
  spec=proposed;setProperty("mapOperation",MapProjection::Transform(spec).operation_definition());emit mode_changed(active());
  if(!active()){generation->fetch_add(1);pending={};timer.stop();presented.reset();painted.reset();setToolTip({});setProperty("mapProbe",{});set_status("Source grid — choose a projection to redraw the image.");}
  else if(same_geometry&&presented)update();else schedule(true);
  return {};
}
QString MapDisplay::select_projection(MapProjection::Mode mode){
  const auto reason=apply_projection(MapProjection::preset(mode,display_model(grid)));
  if(reason.isEmpty())save_preferences();
  return reason;
}
void MapDisplay::load_preferences(){
  // Explicit INI path keeps a portable install self-contained. The override isolates tests.
  settings_file=qEnvironmentVariable("HDFSTUDIO_SETTINGS_FILE",QCoreApplication::applicationDirPath()+"/HDFStudio.ini");
  setProperty("settingsFile",settings_file);
  QSettings settings(settings_file,QSettings::IniFormat);settings.beginGroup("MapProjection");
  if(settings.childKeys().isEmpty()&&settings.status()==QSettings::NoError)return;
  Preferences loaded;bool valid=true,converted=false;
  const int version=settings.value("version").toInt(&converted);valid=converted&&version==1;
  const int mode=projection_keys.indexOf(settings.value("mode").toString());valid=valid&&mode>=0;
  if(mode>=0)loaded.projection=MapProjection::preset(MapProjection::Mode(mode));
  const QStringList names={"lon0","lat0","lat1","lat2","lat_ts","west","span","south","north"};
  double *targets[]={&loaded.projection.lon0,&loaded.projection.lat0,&loaded.projection.lat1,&loaded.projection.lat2,&loaded.projection.lat_ts,&loaded.projection.west,&loaded.projection.span,&loaded.projection.south,&loaded.projection.north};
  for(int i=0;i<names.size();++i){const double value=settings.value(names[i]).toDouble(&converted);valid=valid&&converted&&std::isfinite(value);*targets[i]=value;}
  const auto coast=settings.value("coast_enabled").toString();valid=valid&&(coast=="true"||coast=="false");loaded.coast_enabled=coast=="true";
  loaded.coast_color=QColor(settings.value("coast_color").toString());loaded.coast_width=settings.value("coast_width").toDouble(&converted);
  valid=valid&&converted&&loaded.coast_color.isValid()&&std::isfinite(loaded.coast_width)&&loaded.coast_width>=.2&&loaded.coast_width<=5;
  // Check only preference geometry here. The real source model is required again at restore.
  auto geometry_check=loaded.projection;if(geometry_check.mode==MapProjection::Mode::Source)geometry_check.mode=MapProjection::Mode::LonLat;
  geometry_check.earth={1,1,"Preference geometry validation only"};
  valid=valid&&MapProjection::validate(geometry_check).isEmpty()&&settings.status()==QSettings::NoError;
  if(!valid){setProperty("settingsError",QString("Cannot restore invalid projection preferences from %1; using Source grid.").arg(settings_file));return;}
  saved_preferences=loaded;coast_enabled=loaded.coast_enabled;coast_color=loaded.coast_color;coast_width=loaded.coast_width;
}
QString MapDisplay::save_preferences(){
  QSettings settings(settings_file,QSettings::IniFormat);settings.beginGroup("MapProjection");
  settings.setValue("version",1);settings.setValue("mode",projection_keys.value(int(spec.mode)));
  const QStringList names={"lon0","lat0","lat1","lat2","lat_ts","west","span","south","north"};
  const double values[]={spec.lon0,spec.lat0,spec.lat1,spec.lat2,spec.lat_ts,spec.west,spec.span,spec.south,spec.north};
  for(int i=0;i<names.size();++i)settings.setValue(names[i],values[i]);
  settings.setValue("coast_enabled",coast_enabled);settings.setValue("coast_color",coast_color.name());settings.setValue("coast_width",coast_width);
  settings.endGroup();settings.sync();
  const QString reason=settings.status()==QSettings::NoError?QString():QString("Cannot save projection preferences to %1. Check folder write permission.").arg(settings_file);
  setProperty("settingsError",reason);emit preferences_error(reason);
  if(reason.isEmpty())saved_preferences=Preferences{spec,coast_enabled,coast_color,coast_width};
  return reason;
}
QString MapDisplay::restore_preferences(){
  if(!saved_preferences)return {};
  auto restored=saved_preferences->projection;restored.earth=display_model(grid);
  const auto reason=apply_projection(restored);
  if(reason.isEmpty()){coast_enabled=saved_preferences->coast_enabled;coast_color=saved_preferences->coast_color;coast_width=saved_preferences->coast_width;update();}
  return reason;
}
void MapDisplay::show_settings(){
  if(settings_dialog){settings_dialog->show();settings_dialog->raise();settings_dialog->activateWindow();return;}
  // Window-modal live preview: OK keeps it; every rejection restores the opening state.
  const auto opening_spec=spec;
  const auto opening_viewport=viewport;
  const bool opening_needs_fit=needs_fit,opening_coast=coast_enabled;
  const auto opening_color=coast_color;
  const double opening_width=coast_width;
  auto *dialog=new QDialog(this);settings_dialog=dialog;
  dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);
  dialog->setObjectName("projectionDialog");dialog->setWindowTitle("Projection and reference coastlines");
  auto *layout=new QVBoxLayout(dialog);auto *form=new QFormLayout;layout->addLayout(form);
  auto *mode=new QComboBox(dialog);mode->setObjectName("projectionMode");
  for(auto value:{MapProjection::Mode::Source,MapProjection::Mode::LonLat,MapProjection::Mode::NorthPolar,MapProjection::Mode::SouthPolar,MapProjection::Mode::Lambert,MapProjection::Mode::Mercator})mode->addItem(MapProjection::name(value),int(value));
  mode->setCurrentIndex(int(spec.mode));form->addRow("Projection",mode);
  QList<QDoubleSpinBox*> fields;
  const QStringList names={"Central longitude","Origin latitude","First standard parallel","Second standard parallel","True-scale latitude","West longitude","Longitude span","South latitude","North latitude"};
  const QStringList ids={"projectionLon0","projectionLat0","projectionLat1","projectionLat2","projectionLatTs","projectionWest","projectionSpan","projectionSouth","projectionNorth"};
  for(int i=0;i<names.size();++i){auto *spin=new QDoubleSpinBox(dialog);spin->setRange(-1000000,1000000);spin->setDecimals(6);spin->setKeyboardTracking(false);spin->setObjectName(ids[i]);form->addRow(names[i],spin);fields<<spin;}
  auto *coast=new QCheckBox("Coastlines (Basemap low)",dialog);coast->setObjectName("projectionCoastlines");coast->setChecked(coast_enabled);layout->addWidget(coast);
  auto *color=new QPushButton(coast_color.name(),dialog);color->setObjectName("projectionCoastColor");color->setAutoDefault(false);form->addRow("Coastline color",color);
  auto *line_width=new QDoubleSpinBox(dialog);line_width->setObjectName("projectionCoastWidth");line_width->setRange(.2,5);line_width->setSingleStep(.1);line_width->setKeyboardTracking(false);line_width->setValue(coast_width);form->addRow("Line width (logical pixels)",line_width);
  auto *evidence=new QLabel(grid?grid->evidence+"\n"+display_model(grid).evidence:"Coordinates not ready.",dialog);evidence->setWordWrap(true);
  if(grid)evidence->setToolTip(QString::fromUtf8(QJsonDocument(grid->source_definition).toJson(QJsonDocument::Indented)));layout->addWidget(evidence);
  auto *error=new QLabel(dialog);error->setObjectName("projectionError");error->setWordWrap(true);layout->addWidget(error);
  auto *hint=new QLabel("The image changes as you select; OK is not needed to redraw. OK saves detailed options; Cancel restores the previous view.",dialog);hint->setWordWrap(true);layout->addWidget(hint);
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);buttons->setObjectName("projectionButtons");
  auto *ok=buttons->button(QDialogButtonBox::Ok);ok->setObjectName("projectionOk");ok->setDefault(true);
  buttons->button(QDialogButtonBox::Cancel)->setObjectName("projectionCancel");layout->addWidget(buttons);
  auto fill=[fields](const MapProjection::Spec &s){const double values[]={s.lon0,s.lat0,s.lat1,s.lat2,s.lat_ts,s.west,s.span,s.south,s.north};for(int i=0;i<fields.size();++i){QSignalBlocker block(fields[i]);fields[i]->setValue(values[i]);}};
  auto enable=[fields,form](MapProjection::Mode m){const bool polar=m==MapProjection::Mode::NorthPolar||m==MapProjection::Mode::SouthPolar,lcc=m==MapProjection::Mode::Lambert;const bool visible[]={m!=MapProjection::Mode::Source,lcc,lcc,lcc,polar||m==MapProjection::Mode::Mercator,!polar,m!=MapProjection::Mode::Source&&!polar,m!=MapProjection::Mode::Source&&m!=MapProjection::Mode::SouthPolar,m!=MapProjection::Mode::Source&&m!=MapProjection::Mode::NorthPolar};for(int i=0;i<fields.size();++i){fields[i]->setVisible(visible[i]);if(auto *label=form->labelForField(fields[i]))label->setVisible(visible[i]);}};
  fill(spec);enable(spec.mode);
  auto preview=[this,mode,fields,coast,color,line_width,error,ok]{
    auto proposed=MapProjection::preset(MapProjection::Mode(mode->currentData().toInt()),display_model(grid));
    double *targets[]={&proposed.lon0,&proposed.lat0,&proposed.lat1,&proposed.lat2,&proposed.lat_ts,&proposed.west,&proposed.span,&proposed.south,&proposed.north};
    for(int i=0;i<fields.size();++i)*targets[i]=fields[i]->value();
    const auto reason=apply_projection(proposed);error->setText(reason);ok->setEnabled(reason.isEmpty());
    if(!reason.isEmpty())return false;
    coast_enabled=coast->isChecked();coast_color=QColor(color->text());coast_width=line_width->value();update();return true;
  };
  connect(mode,&QComboBox::currentIndexChanged,dialog,[this,mode,fill,enable,preview]{const auto selected=MapProjection::Mode(mode->currentData().toInt());fill(MapProjection::preset(selected,display_model(grid)));enable(selected);preview();});
  connect(this,&MapDisplay::mode_changed,dialog,[this,mode,fill,enable]{if(mode->currentData().toInt()==int(spec.mode))return;QSignalBlocker block(mode);mode->setCurrentIndex(int(spec.mode));fill(spec);enable(spec.mode);});
  for(auto *field:fields)connect(field,&QDoubleSpinBox::valueChanged,dialog,[mode,fields,field,preview]{
    const auto selected=MapProjection::Mode(mode->currentData().toInt());
    if(field==fields[0]&&(selected==MapProjection::Mode::LonLat||selected==MapProjection::Mode::Mercator)&&fields[6]->value()==360){QSignalBlocker block(fields[5]);fields[5]->setValue(fields[0]->value()-180);}
    preview();
  });
  connect(coast,&QCheckBox::toggled,dialog,[preview]{preview();});
  connect(line_width,&QDoubleSpinBox::valueChanged,dialog,[preview]{preview();});
  connect(color,&QPushButton::clicked,dialog,[color,dialog,preview]{const auto chosen=QColorDialog::getColor(QColor(color->text()),dialog);if(chosen.isValid()){color->setText(chosen.name());preview();}});
  connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,preview,error]{if(!preview())return;const auto reason=save_preferences();if(reason.isEmpty())dialog->accept();else error->setText(reason);});
  connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
  connect(dialog,&QDialog::finished,this,[this,opening_spec,opening_viewport,opening_needs_fit,opening_coast,opening_color,opening_width](int result){
    settings_dialog.clear();
    if(result==QDialog::Accepted)return;
    coast_enabled=opening_coast;coast_color=opening_color;coast_width=opening_width;
    auto restored=opening_spec;restored.earth=display_model(grid);
    const auto reason=apply_projection(restored);
    if(!reason.isEmpty()){apply_projection(MapProjection::preset(MapProjection::Mode::Source));set_status(reason);return;}
    viewport=opening_viewport;needs_fit=opening_needs_fit;
    if(active())schedule();
  });
  dialog->resize(480,dialog->sizeHint().height());dialog->show();
}
