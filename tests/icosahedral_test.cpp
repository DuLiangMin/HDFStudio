// ICO-001: numerical, export and direct consumer/lifecycle checks only.
#include <QtTest>
#include <QtWidgets>
#include <QScopeGuard>
#include <netcdf.h>
#include "icosahedral.hpp"
#include "icosahedral_view.hpp"
#include "ico_3d_canvas.hpp"
#include "raster_view.hpp"
#include "hdf_explorer.hpp"
#include "color_map.hpp"
#include "grib/grib_data.hpp"
#include "layer_playback.hpp"
#include "browser_item.hpp"
#include "data_read_controller.hpp"
#include "geo_grid_controller.hpp"
#include "ico_atlas_render.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace {
constexpr double pi=3.14159265358979323846;
void nc_ok(int code){if(code!=NC_NOERR)qFatal("NC fixture: %s",nc_strerror(code));}
void fixture(const QString &path){
  int root,dt,dy,dx,t,y,x,value;nc_ok(nc_create(path.toLocal8Bit().constData(),NC_CLOBBER,&root));
  nc_ok(nc_def_dim(root,"time",2,&dt));nc_ok(nc_def_dim(root,"lat",19,&dy));nc_ok(nc_def_dim(root,"lon",36,&dx));
  nc_ok(nc_def_var(root,"time",NC_INT,1,&dt,&t));nc_ok(nc_def_var(root,"lat",NC_DOUBLE,1,&dy,&y));nc_ok(nc_def_var(root,"lon",NC_DOUBLE,1,&dx,&x));int dims[]={dt,dy,dx};nc_ok(nc_def_var(root,"t",NC_DOUBLE,3,dims,&value));
  nc_ok(nc_put_att_text(root,y,"units",13,"degrees_north"));nc_ok(nc_put_att_text(root,x,"units",12,"degrees_east"));nc_ok(nc_put_att_text(root,value,"units",1,"K"));nc_ok(nc_enddef(root));
  double lat[19],lon[36],values[2*19*36];for(int r=0;r<19;++r)lat[r]=-90+10*r;for(int c=0;c<36;++c)lon[c]=10*c;
  for(int i=0;i<2*19*36;++i)values[i]=i/(19*36)+std::sin(lat[(i/36)%19]*pi/180)+std::cos(lon[i%36]*pi/180);
  nc_ok(nc_put_var_double(root,y,lat));nc_ok(nc_put_var_double(root,x,lon));nc_ok(nc_put_var_double(root,value,values));nc_ok(nc_close(root));
}
void regional_fixture(const QString &path,bool global=false){
  int root,dt,dy,dx,y,x,value;nc_ok(nc_create(path.toLocal8Bit().constData(),NC_CLOBBER,&root));
  nc_ok(nc_def_dim(root,"time",3,&dt));nc_ok(nc_def_dim(root,"lat",61,&dy));nc_ok(nc_def_dim(root,"lon",71,&dx));
  nc_ok(nc_def_var(root,"lat",NC_DOUBLE,1,&dy,&y));nc_ok(nc_def_var(root,"lon",NC_DOUBLE,1,&dx,&x));int dims[]={dt,dy,dx};nc_ok(nc_def_var(root,"field",NC_DOUBLE,3,dims,&value));
  nc_ok(nc_put_att_text(root,y,"units",13,"degrees_north"));nc_ok(nc_put_att_text(root,x,"units",12,"degrees_east"));nc_ok(nc_enddef(root));
  double lat[61],lon[71];std::vector<double> values(3*61*71);
  for(int row=0;row<61;++row)lat[row]=global?-90+3*row:row;for(int col=0;col<71;++col)lon[col]=global?360.*col/71:70+col;
  for(int frame=0;frame<3;++frame)for(int row=0;row<61;++row)for(int col=0;col<71;++col)
    values[(frame*61+row)*71+col]=2+std::sin((col-frame*15)*pi/35)*std::cos(row*pi/60)+frame*.2;
  nc_ok(nc_put_var_double(root,y,lat));nc_ok(nc_put_var_double(root,x,lon));nc_ok(nc_put_var_double(root,value,values.data()));nc_ok(nc_close(root));
}
bool open_context_analysis(FileTreeWidget *tree,QTreeWidgetItem *item,QString &error){
  for(auto *parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
  tree->setCurrentItem(item);tree->scrollToItem(item);
  bool found=false;QTimer timer;timer.setInterval(20);
  QObject::connect(&timer,&QTimer::timeout,tree,[&]{
    auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
    QMenu *visual=nullptr;for(auto *entry:menu->actions())if(entry->menu()&&entry->menu()->objectName()=="visualAnalysisMenu")visual=entry->menu();
    if(!visual)error="Missing Visual Analysis submenu";
    else {auto *ico=visual->findChild<QAction*>("actionIcosahedralAnalysis");if(ico&&ico->isEnabled()&&ico->text()=="Icosahedral 5 slices"){found=true;
      const auto evidence=qEnvironmentVariable("HDFSTUDIO_ICO_EVIDENCE_DIR");if(!evidence.isEmpty()){visual->popup(menu->mapToGlobal(QPoint(menu->width(),0)));visual->grab().save(QDir(evidence).filePath("visual-analysis-menu.png"));}
      ico->trigger();}else error="Missing/enabled Icosahedral 5 slices action";
      for(auto *entry:menu->actions())if(entry->objectName()=="actionIcosahedralAnalysis"||entry->text().startsWith("Histogram")||entry->text().startsWith("Texture Entropy")||entry->text().startsWith("Space-Time"))error="Analysis item remains in root menu";}
    if(visual)visual->close();menu->close();
  });timer.start();
  if(!QMetaObject::invokeMethod(tree,"show_context_menu",Qt::DirectConnection,Q_ARG(QPoint,tree->visualItemRect(item).center())))error="Cannot open context menu";
  timer.stop();return found&&error.isEmpty();
}
}
class IcosahedralTest:public QObject {
  Q_OBJECT
private slots:
  void meshTopologyAreaAndContainment();
  void constantSmallSignalAndCsr();
  void axesMaskSeamAndRejection();
  void gaussianLatitudeAndCancellation();
  void netcdfRoundtripAndSourceProtection();
  void newPalettesAndOldDefault();
  void guiDemoModesLatestCancelAndClose();
  void guiSnapshotAndMenu();
  void guiRealGaussianCoordinatesAndSnapshot();
  void glMeshBuffersAndPicking();
  void glNativeRenderingInteractionAndLifetime();
  void regionalOutsideNanAndDateline();
  void contextAnimationAndSourceReuse();
  void contextAnimationAndSourceReuse_data();
  void realRegionalSample();
  void fiveChartsTopologyRenderAndExport();
  void gribContextAndMissingFrameStops();
  void levelAndModeClicks_data();
  void levelAndModeClicks();
};
void IcosahedralTest::meshTopologyAreaAndContainment(){
  for(int level=0;level<=5;++level){
    Icosahedral::Mesh mesh;QVERIFY(Icosahedral::build_mesh(level,mesh).isEmpty());const size_t power=size_t(1)<<(2*level);
    QCOMPARE(mesh.xyz.size(),10*power+2);QCOMPARE(mesh.faces.size(),20*power);QCOMPARE(mesh.edges.size(),30*power);QCOMPARE(mesh.xyz.size()+mesh.faces.size()-mesh.edges.size(),size_t(2));
    QVERIFY(std::abs(std::accumulate(mesh.area.begin(),mesh.area.end(),0.)-4*pi)<4e-12);
    for(const auto &v:mesh.xyz)QVERIFY(std::abs(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]-1)<8e-16);
    for(size_t i=0;i<mesh.xyz.size();i+=std::max(size_t(1),mesh.xyz.size()/97)){size_t face;Icosahedral::Vec weights;QVERIFY2(Icosahedral::locate(mesh,mesh.xyz[i],face,weights).isEmpty(),"hierarchical containment");QVERIFY(face<mesh.faces.size());QVERIFY(std::abs(weights[0]+weights[1]+weights[2]-1)<5e-16);}
  }
  Icosahedral::Mesh invalid;QVERIFY(!Icosahedral::build_mesh(7,invalid).isEmpty());
}
void IcosahedralTest::constantSmallSignalAndCsr(){
  for(double constant:{0.,7.25,2e-180}){
    GeoGrid::Grid grid;auto source=Icosahedral::demo(grid);source.values=std::make_shared<const std::vector<double>>(source.rows*source.cols,constant);
    const auto out=Icosahedral::analyze(source,grid,2);QVERIFY2(out->error.isEmpty(),qPrintable(out->error));QCOMPARE(out->compared,source.rows*source.cols);
    for(double v:out->node_values)QVERIFY(constant==0?v==0:std::abs(v/constant-1)<7e-16);
    for(double v:out->restored)QVERIFY(constant==0?v==0:std::abs(v/constant-1)<9e-16);
    for(size_t row=0;row<out->node_values.size();++row){double sum=0,value=0;for(size_t j=out->forward_offset[row];j<out->forward_offset[row+1];++j){sum+=out->forward_weight[j];value+=out->forward_weight[j]*(*source.values)[out->forward_source[j]];}QVERIFY(std::abs(sum-1)<4e-16);QCOMPARE(value,out->node_values[row]);}
  }
}
void IcosahedralTest::axesMaskSeamAndRejection(){
  GeoGrid::Grid grid;auto source=Icosahedral::demo(grid);const auto original=Icosahedral::analyze(source,grid,2);QVERIFY(original->error.isEmpty());
  auto descending=grid;std::reverse(descending.latitude.centers.begin(),descending.latitude.centers.end());std::reverse(descending.longitude.centers.begin(),descending.longitude.centers.end());
  auto reversed=std::make_shared<std::vector<double>>(*source.values);for(size_t r=0;r<source.rows;++r)for(size_t c=0;c<source.cols;++c)(*reversed)[r*source.cols+c]=(*source.values)[(source.rows-1-r)*source.cols+source.cols-1-c];
  auto reverse_source=source;reverse_source.values=reversed;const auto result=Icosahedral::analyze(reverse_source,descending,2);QVERIFY2(result->error.isEmpty(),qPrintable(result->error));
  for(size_t i=0;i<result->node_values.size();++i)QCOMPARE(result->node_values[i],original->node_values[i]);
  auto duplicate=source;duplicate.cols++;auto values=std::make_shared<std::vector<double>>();for(size_t r=0;r<source.rows;++r){values->insert(values->end(),source.values->begin()+r*source.cols,source.values->begin()+(r+1)*source.cols);values->push_back((*source.values)[r*source.cols]);}duplicate.values=values;auto dup_grid=grid;dup_grid.longitude.centers.push_back(360);
  QVERIFY(Icosahedral::analyze(duplicate,dup_grid,1)->error.isEmpty());(*values)[source.cols]=999;QVERIFY(Icosahedral::analyze(duplicate,dup_grid,1)->error.contains("endpoint"));
  auto regional=grid;for(auto &v:regional.longitude.centers)v*=.5;QVERIFY(Icosahedral::analyze(source,regional,1)->error.isEmpty());
  auto missing=std::make_shared<std::vector<double>>(*source.values);(*missing)[source.cols*36]=NAN;auto masked=source;masked.values=missing;const auto mask=Icosahedral::analyze(masked,grid,2);QVERIFY(mask->error.isEmpty());QVERIFY(std::isnan(mask->restored[source.cols*36]));
  masked.values=std::make_shared<const std::vector<double>>(missing->size(),NAN);const auto all=Icosahedral::analyze(masked,grid,1);QVERIFY(all->error.isEmpty());QCOMPARE(all->compared,size_t(0));QVERIFY(std::all_of(all->node_values.begin(),all->node_values.end(),[](double v){return std::isnan(v);}));
  auto nonuniform=grid;nonuniform.longitude.centers[1]+=.1;const auto irregular=Icosahedral::analyze(source,nonuniform,1);QVERIFY(irregular->error.isEmpty());QVERIFY(irregular->forward_method.contains("Regional"));
}
void IcosahedralTest::gaussianLatitudeAndCancellation(){
  GeoGrid::Grid grid;auto source=Icosahedral::demo(grid);for(auto &v:grid.latitude.centers)v=std::asin(v/90)*180/pi;
  const auto out=Icosahedral::analyze(source,grid,2);QVERIFY2(out->error.isEmpty(),qPrintable(out->error));QCOMPARE(out->compared,source.rows*source.cols);
  QVERIFY(Icosahedral::analyze(source,grid,4,[]{return true;})->error=="Cancelled");
  grid.latitude.centers.front()=-85;grid.latitude.centers.back()=85;const auto partial=Icosahedral::analyze(source,grid,2);QVERIFY(partial->error.isEmpty());QVERIFY(std::any_of(partial->node_values.begin(),partial->node_values.end(),[](double v){return std::isnan(v);}));
}
void IcosahedralTest::netcdfRoundtripAndSourceProtection(){
  QTemporaryDir dir;GeoGrid::Grid grid;auto source=Icosahedral::demo(grid);const auto out=Icosahedral::analyze(source,grid,2);const QString path=dir.filePath("mesh.nc");QVERIFY2(Icosahedral::export_netcdf(path,*out).isEmpty(),"export");
  int file,var;nc_ok(nc_open(path.toLocal8Bit().constData(),NC_NOWRITE,&file));nc_ok(nc_inq_varid(file,"node_value",&var));std::vector<double> values(out->node_values.size());nc_ok(nc_get_var_double(file,var,values.data()));QCOMPARE(values,out->node_values);nc_ok(nc_inq_varid(file,"face_node",&var));std::vector<int> faces(3*out->mesh.faces.size());nc_ok(nc_get_var_int(file,var,faces.data()));for(size_t i=0;i<faces.size();++i)QCOMPARE(faces[i],out->mesh.faces[i/3][i%3]);nc_ok(nc_close(file));
  QVERIFY(Icosahedral::export_netcdf(path,*out).contains("exists"));out->source.file_path=dir.filePath("source.nc");QVERIFY(Icosahedral::export_netcdf(out->source.file_path,*out).contains("source"));
}
void IcosahedralTest::newPalettesAndOldDefault(){
  QString error;const auto palettes=HdfColorMap::load_registry(error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(HdfColorMap::default_palette_id,QString("matplotlib:jet"));
  for(const QString &id:{"hdfstudio:s2s-ocean","hdfstudio:s2s-anomaly","hdfstudio:s2s-rain"}){const auto it=std::find_if(palettes.begin(),palettes.end(),[&](const auto &p){return p.id==id;});QVERIFY(it!=palettes.end());QVERIFY(HdfColorMap::validate(*it).isEmpty());QCOMPARE(it->source_hash.size(),64);QCOMPARE(it->license,QString("GPL-3.0-or-later"));}
}
void IcosahedralTest::guiDemoModesLatestCancelAndClose(){
  IcosahedralView view;view.resize(920,780);view.show();QTRY_VERIFY_WITH_TIMEOUT(view.result(),15000);
  auto *mode=view.findChild<QComboBox*>("icoDisplayMode");auto *level=view.findChild<QSpinBox*>("icoLevel");
  const QString evidence=qEnvironmentVariable("HDFSTUDIO_ICO_EVIDENCE_DIR");
  for(int i=0;i<6;++i){mode->setCurrentIndex(i);if(i<2&&!view.findChild<Ico3DCanvas*>()){QVERIFY(view.image_for_export().isNull());continue;}const auto image=view.image_for_export();QVERIFY(!image.isNull());if(!evidence.isEmpty())QVERIFY(image.save(QDir(evidence).filePath(QString("rendered-mode-%1.png").arg(i))));}
  level->setValue(5);view.run_analysis();level->setValue(1);view.run_analysis();QTRY_VERIFY_WITH_TIMEOUT(view.result()&&view.result()->mesh.level==1,20000);
  auto *closing=new IcosahedralView;closing->findChild<QSpinBox*>("icoLevel")->setValue(6);closing->run_analysis();QTest::qWait(5);delete closing;QTRY_VERIFY_WITH_TIMEOUT(!IcosahedralView::any_busy(),15000);
  level->setValue(5);view.run_analysis();view.findChild<QPushButton*>("icoCancel")->click();QTRY_VERIFY_WITH_TIMEOUT(!IcosahedralView::any_busy(),15000);QVERIFY(!view.result());
}
void IcosahedralTest::guiSnapshotAndMenu(){
  QTemporaryDir dir;const auto old=qgetenv("HDFSTUDIO_SETTINGS_FILE");qputenv("HDFSTUDIO_SETTINGS_FILE",QFile::encodeName(dir.filePath("settings.ini")));const auto restore=qScopeGuard([old]{if(old.isNull())qunsetenv("HDFSTUDIO_SETTINGS_FILE");else qputenv("HDFSTUDIO_SETTINGS_FILE",old);});
  const auto file=dir.filePath("source.nc");fixture(file);auto *raster=new RasterView(file.toLocal8Bit().toStdString(),"/t",{2,19,36});raster->resize(800,600);raster->show();QTRY_VERIFY_WITH_TIMEOUT(raster->plane_snapshot().values&&!raster->render_pending(),10000);
  IcosahedralView view(raster);view.resize(850,750);view.show();QTRY_VERIFY_WITH_TIMEOUT(view.result(),20000);QCOMPARE(view.result()->source.units,QString("K"));const auto snapshot=raster->plane_snapshot();QCOMPARE(*view.result()->source.values,*snapshot.values);
  raster->set_layer_index(0,1);QTRY_VERIFY_WITH_TIMEOUT(!raster->render_pending()&&raster->plane_snapshot().leading_indices[0]==1,10000);QVERIFY(!view.result());view.run_analysis();QTRY_VERIFY_WITH_TIMEOUT(view.result(),15000);QCOMPARE(view.result()->source.leading_indices[0],size_t(1));
  auto *mask_start=raster->findChild<QLineEdit*>("maskStart"),*mask_end=raster->findChild<QLineEdit*>("maskEnd");QVERIFY(mask_start&&mask_end);mask_start->setText("1");mask_end->setText("1.5");QMetaObject::invokeMethod(mask_end,"editingFinished",Qt::DirectConnection);
  QTRY_VERIFY_WITH_TIMEOUT(!raster->render_pending(),10000);QVERIFY(!view.result());view.run_analysis();QTRY_VERIFY_WITH_TIMEOUT(view.result(),15000);
  const auto masked=raster->plane_snapshot();size_t missing=0;for(size_t i=0;i<masked.values->size();++i)if(!std::isfinite(masked.values->at(i))){++missing;QVERIFY(std::isnan(view.result()->restored[i]));}QVERIFY(missing>0);
  const QString output=dir.filePath("export.nc");QVERIFY(view.export_data(output).isEmpty());
  QFile changed(file);QVERIFY(changed.open(QIODevice::Append));QCOMPARE(changed.write("source changed"),qint64(14));changed.close();QVERIFY(view.export_data(dir.filePath("stale.nc")).contains("SourceChanged"));QVERIFY(view.image_for_export().isNull());view.run_analysis();QTRY_VERIFY(!view.result());QVERIFY(view.findChild<QLabel*>("icoStatus")->text().contains("SourceChanged"));
  delete raster;QVERIFY(!view.result());QVERIFY(view.export_data(dir.filePath("closed.nc")).contains("No current"));
  MainWindow window;window.resize(1200,900);window.show();QVERIFY(!window.findChild<QAction*>("actionIcosahedralAnalysis"));const auto menu_file=dir.filePath("menu.nc");fixture(menu_file);QCOMPARE(window.read_file(menu_file),0);
  auto *tree=window.findChild<FileTreeWidget*>();QVERIFY(tree);const auto nodes=tree->findItems("t",Qt::MatchExactly|Qt::MatchRecursive);QCOMPARE(nodes.size(),1);QString menu_error;QVERIFY2(open_context_analysis(tree,nodes.first(),menu_error),qPrintable(menu_error));QTRY_VERIFY_WITH_TIMEOUT(window.findChild<IcosahedralView*>()->result(),15000);
  const auto *analysis=window.findChild<IcosahedralView*>();auto *gl=analysis->findChild<Ico3DCanvas*>();if(gl){analysis->findChild<QComboBox*>("icoDisplayMode")->setCurrentIndex(0);QTRY_VERIFY_WITH_TIMEOUT(gl->ready(),10000);QVERIFY(!gl->grabFramebuffer().isNull());}
  for(const QString &name:QStringList{"icoLevel","icoRun","icoDisplayMode","icoPalette",gl?"ico3DCanvas":"icosahedralCanvas","icoColorBar","icoStatus","icoExportNetcdf","icoExportPng"}){const auto *control=analysis->findChild<QWidget*>(name);QVERIFY(control);QVERIFY(control->isVisible());QVERIFY(window.rect().contains(QRect(control->mapTo(&window,QPoint()),control->size())));}
  const QString evidence=qEnvironmentVariable("HDFSTUDIO_ICO_EVIDENCE_DIR");if(!evidence.isEmpty())QVERIFY(window.grab().save(QDir(evidence).filePath(gl?"native-main-layout.png":"offscreen-main-layout.png")));
  window.close();QTRY_VERIFY_WITH_TIMEOUT(!IcosahedralView::any_busy(),15000);
}
void IcosahedralTest::guiRealGaussianCoordinatesAndSnapshot(){
  const QString file=qEnvironmentVariable("HDFSTUDIO_GAUSSIAN_SAMPLE");if(file.isEmpty())QSKIP("Optional read-only real Gaussian fixture not configured");
  QTemporaryDir dir;const auto old=qgetenv("HDFSTUDIO_SETTINGS_FILE");qputenv("HDFSTUDIO_SETTINGS_FILE",QFile::encodeName(dir.filePath("settings.ini")));const auto restore=qScopeGuard([old]{if(old.isNull())qunsetenv("HDFSTUDIO_SETTINGS_FILE");else qputenv("HDFSTUDIO_SETTINGS_FILE",old);});
  const auto info=GribData::describe(file,"/field/0");QVERIFY2(info.error.isEmpty(),qPrintable(info.error));
  RasterView raster(file,"/field/0",info);raster.resize(850,650);raster.show();QTRY_VERIFY_WITH_TIMEOUT(raster.plane_snapshot().values&&!raster.render_pending(),20000);
  IcosahedralView view(&raster);view.resize(850,750);view.show();QTRY_VERIFY_WITH_TIMEOUT(view.result(),30000);
  const auto result=view.result();QCOMPARE(result->source.rows,size_t(190));QCOMPARE(result->source.cols,size_t(384));QCOMPARE(result->source.units,info.units);
  const auto current=raster.plane_snapshot();QCOMPARE(*result->source.values,*current.values);QCOMPARE(result->source.revision,current.revision);
  QVERIFY(result->compared>0);QVERIFY(result->rmse>0);QVERIFY(result->rmse<.01);
  QVERIFY(result->grid.longitude.periodic);
  const auto evidence=qEnvironmentVariable("HDFSTUDIO_ICO_EVIDENCE_DIR");
  if(!evidence.isEmpty()){
    QVERIFY(view.export_data(QDir(evidence).filePath("gui-global.nc")).isEmpty());
    for(int display:{0,2,4,5}){view.findChild<QComboBox*>("icoDisplayMode")->setCurrentIndex(display);if(display==0&&!view.findChild<Ico3DCanvas*>())continue;QTRY_VERIFY(view.ready());const auto image=view.image_for_export();QVERIFY(!image.isNull());QVERIFY(image.save(QDir(evidence).filePath(QString("global-mode-%1.png").arg(display))));if(display==0){view.findChild<QComboBox*>("ico3DShapeCombo")->setCurrentIndex(1);QVERIFY(view.image_for_export().save(QDir(evidence).filePath("global-polyhedron.png")));view.findChild<QComboBox*>("ico3DShapeCombo")->setCurrentIndex(0);}}
  }
  QTRY_VERIFY_WITH_TIMEOUT(!IcosahedralView::any_busy(),15000);GribData::release(file);
}
void IcosahedralTest::glMeshBuffersAndPicking(){
  GeoGrid::Grid grid;auto source=Icosahedral::demo(grid);auto result=Icosahedral::analyze(source,grid,2);QVERIFY(result->error.isEmpty());
  const auto original=result->mesh.xyz;Ico3DBuffers buffers;QVERIFY(ico_build_3d_topology_buffers(result->mesh,buffers));
  QCOMPARE(buffers.fill_vertices.size(),3*result->mesh.faces.size());QCOMPARE(buffers.sub_edge_lines.size(),2*result->mesh.edges.size());QVERIFY(!buffers.base_edge_lines.empty());QCOMPARE(buffers.graticule_lines.size(),size_t(2160));QCOMPARE(result->mesh.xyz,original);
  double minimum=2;for(const auto &p:buffers.poly_xyz){double norm=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);QVERIFY(norm<=1+1e-15);minimum=std::min(minimum,norm);}QVERIFY(minimum<.9);
  QString error;const auto palettes=HdfColorMap::load_registry(error);QVERIFY(error.isEmpty());HdfColorMap::Mapping mapping;mapping.minimum=-2;mapping.maximum=2;
  result->node_values[0]=NAN;QVERIFY(ico_update_3d_fill_colors(*result,false,palettes.front(),mapping,buffers));
  const auto missing=QColor::fromRgba(HdfColorMap::missing_color);for(size_t f=0;f<result->mesh.faces.size();++f)if(std::find(result->mesh.faces[f].begin(),result->mesh.faces[f].end(),0)!=result->mesh.faces[f].end())for(int k=0;k<3;++k){QCOMPARE(buffers.fill_vertices[3*f+k].color[0],qRed(missing.rgb())/255.f);QCOMPARE(buffers.fill_vertices[3*f+k].color[1],qGreen(missing.rgb())/255.f);QCOMPARE(buffers.fill_vertices[3*f+k].color[2],qBlue(missing.rgb())/255.f);}
  for(double morph:{0.,.5,1.}){const auto hit=ico_pick_3d_ray({0,0,3},{0,0,-1},morph,*result,buffers,false);QVERIFY(hit.hit);QVERIFY(hit.face>=0);QVERIFY(hit.nearest_node>=0);QVERIFY(std::abs(hit.lat_deg-90)<1e-6);}
  QVERIFY(!ico_pick_3d_ray({3,3,3},{0,0,1},1,*result,buffers,false).hit);
}
void IcosahedralTest::glNativeRenderingInteractionAndLifetime(){
  if(QGuiApplication::platformName()!="windows")QSKIP("Actual Windows OpenGL context verified in separate native run");
  IcosahedralView view;view.resize(1100,880);view.show();QTRY_VERIFY_WITH_TIMEOUT(view.result(),15000);
  auto *canvas=view.findChild<Ico3DCanvas*>();QVERIFY(canvas);QTRY_VERIFY_WITH_TIMEOUT(canvas->ready()||!canvas->gl_error().isEmpty(),15000);QVERIFY2(canvas->ready(),qPrintable(canvas->gl_error()));QVERIFY(canvas->isValid());
  const auto initial=canvas->grabFramebuffer();QVERIFY(!initial.isNull());QSet<QRgb> colors;for(int y=0;y<initial.height();y+=8)for(int x=0;x<initial.width();x+=8)colors.insert(initial.pixel(x,y));QVERIFY(colors.size()>30);
  const QString evidence=qEnvironmentVariable("HDFSTUDIO_ICO_EVIDENCE_DIR");if(!evidence.isEmpty())QVERIFY(initial.save(QDir(evidence).filePath("gl-sphere.png")));
  const auto result=view.result();const auto values=result->node_values;const auto uploads=canvas->property("icoUploadCount").toULongLong();const auto rotation=canvas->rotation();
  const QPoint from=canvas->rect().center(),to=from+QPoint(110,65);QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,from);QMouseEvent move(QEvent::MouseMove,to,canvas->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&move);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,to);QVERIFY(canvas->rotation()!=rotation);
  const auto rotated=canvas->grabFramebuffer();QVERIFY(rotated!=initial);if(!evidence.isEmpty())QVERIFY(rotated.save(QDir(evidence).filePath("gl-rotated.png")));QCOMPARE(canvas->property("icoUploadCount").toULongLong(),uploads);
  canvas->reset_camera();QVERIFY(canvas->pick_at(canvas->rect().center()).hit);QWheelEvent wheel(QPointF(from),QPointF(canvas->mapToGlobal(from)),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(canvas,&wheel);QVERIFY(canvas->zoom()>.82);canvas->reset_camera();
  view.findChild<QComboBox*>("ico3DShapeCombo")->setCurrentIndex(1);QCOMPARE(canvas->morph(),0.);const auto poly=canvas->grabFramebuffer();QVERIFY(poly!=initial);if(!evidence.isEmpty())QVERIFY(poly.save(QDir(evidence).filePath("gl-polyhedron.png")));view.findChild<QSlider*>("ico3DMorphSlider")->setValue(50);QCOMPARE(canvas->morph(),.5);view.findChild<QSlider*>("ico3DMorphSlider")->setValue(100);
  QTest::mousePress(canvas,Qt::RightButton,Qt::NoModifier,from);QMouseEvent pan(QEvent::MouseMove,to,canvas->mapToGlobal(to),Qt::NoButton,Qt::RightButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&pan);QTest::mouseRelease(canvas,Qt::RightButton,Qt::NoModifier,to);QVERIFY(canvas->grabFramebuffer()!=initial);canvas->reset_camera();
  for(int preset=1;preset<=5;++preset){canvas->reset_camera(preset);QVERIFY(canvas->pick_at(canvas->rect().center()).hit);}canvas->reset_camera();
  for(const QString &id:{"ico3DBaseEdgesCheck","ico3DGraticuleCheck","ico3DShadingCheck","icoEdges"}){auto *box=view.findChild<QCheckBox*>(id);QVERIFY(box);const bool state=box->isChecked();box->setChecked(!state);QVERIFY(!canvas->grabFramebuffer().isNull());box->setChecked(state);}
  view.findChild<QComboBox*>("icoPalette")->setCurrentIndex(view.findChild<QComboBox*>("icoPalette")->findData("hdfstudio:s2s-rain"));QVERIFY(canvas->grabFramebuffer()!=initial);QCOMPARE(view.result(),result);QCOMPARE(view.result()->node_values,values);
  const auto exported=view.image_for_export();QVERIFY(!exported.isNull());QVERIFY(exported.height()>canvas->grabFramebuffer().height());if(!evidence.isEmpty())QVERIFY(exported.save(QDir(evidence).filePath("gl-export-with-colorbar.png")));
  auto *level=view.findChild<QSpinBox*>("icoLevel");level->setValue(6);view.run_analysis();QTRY_VERIFY_WITH_TIMEOUT(view.result(),30000);QCOMPARE(view.result()->mesh.faces.size(),size_t(81920));QVERIFY(!canvas->grabFramebuffer().isNull());QVERIFY(canvas->pick_at(canvas->rect().center()).hit);QVERIFY2(canvas->gl_error().isEmpty(),qPrintable(canvas->gl_error()));
  qInfo()<<"GL vendor"<<canvas->property("icoGlVendor")<<"version"<<canvas->property("icoGlVersion");view.close();QTRY_VERIFY_WITH_TIMEOUT(!IcosahedralView::any_busy(),15000);
}
void IcosahedralTest::regionalOutsideNanAndDateline(){
  GeoGrid::Grid grid;PlaneSnapshot source;source.rows=7;source.cols=9;
  for(int row=0;row<7;++row)grid.latitude.centers.push_back(-30+10*row);
  for(int col=0;col<9;++col)grid.longitude.centers.push_back(160+5*col);
  auto values=std::make_shared<std::vector<double>>();
  for(double lat:grid.latitude.centers)for(double lon:grid.longitude.centers)values->push_back(2+lat*.01+(lon-180)*.02);
  source.values=values;const auto result=Icosahedral::analyze(source,grid,4);QVERIFY2(result->error.isEmpty(),qPrintable(result->error));QVERIFY(result->forward_method.contains("Regional"));
  size_t finite=0,missing=0;
  for(size_t node=0;node<result->mesh.xyz.size();++node){
    const auto &xyz=result->mesh.xyz[node];double lon=std::atan2(xyz[1],xyz[0])*180/pi;if(lon<160)lon+=360;const double lat=std::asin(std::clamp(xyz[2],-1.,1.))*180/pi;
    const bool inside=lon>=160&&lon<=200&&lat>=-30&&lat<=30;
    if(!inside){++missing;QVERIFY(std::isnan(result->node_values[node]));QCOMPARE(result->forward_offset[node],result->forward_offset[node+1]);continue;}
    ++finite;const double expected=2+lat*.01+(lon-180)*.02;QVERIFY(std::abs(result->node_values[node]/expected-1)<2e-14);
    double sum=0,value=0;QCOMPARE(result->forward_offset[node+1]-result->forward_offset[node],size_t(4));
    for(size_t link=result->forward_offset[node];link<result->forward_offset[node+1];++link){sum+=result->forward_weight[link];value+=result->forward_weight[link]*values->at(size_t(result->forward_source[link]));}
    QVERIFY(std::abs(sum-1)<5e-16);QCOMPARE(value,result->node_values[node]);
  }
  QVERIFY(finite>100);QVERIFY(missing>finite);QVERIFY(result->compared>0);
  auto reversed=std::make_shared<std::vector<double>>(values->size());
  for(size_t row=0;row<source.rows;++row)for(size_t col=0;col<source.cols;++col)(*reversed)[row*source.cols+col]=values->at((source.rows-1-row)*source.cols+source.cols-1-col);
  auto descending=grid;std::reverse(descending.latitude.centers.begin(),descending.latitude.centers.end());std::reverse(descending.longitude.centers.begin(),descending.longitude.centers.end());
  for(auto &lon:descending.longitude.centers)if(lon>180)lon-=360;
  source.values=reversed;const auto flipped=Icosahedral::analyze(source,descending,4);QVERIFY2(flipped->error.isEmpty(),qPrintable(flipped->error));
  for(size_t node=0;node<result->node_values.size();++node){if(std::isnan(result->node_values[node]))QVERIFY(std::isnan(flipped->node_values[node]));else QCOMPARE(flipped->node_values[node],result->node_values[node]);}
  (*values)[3*source.cols+4]=NAN;source.values=values;const auto masked=Icosahedral::analyze(source,grid,4);QVERIFY(masked->error.isEmpty());QVERIFY(std::isnan(masked->restored[3*source.cols+4]));
  const auto masked_finite=std::count_if(masked->node_values.begin(),masked->node_values.end(),[](double value){return std::isfinite(value);});QVERIFY(size_t(masked_finite)<finite);
}
void IcosahedralTest::contextAnimationAndSourceReuse_data(){QTest::addColumn<bool>("global");QTest::newRow("regional")<<false;QTest::newRow("global")<<true;}
void IcosahedralTest::contextAnimationAndSourceReuse(){
  QFETCH(bool,global);
  QTemporaryDir dir;const auto file=dir.filePath("animation.nc");regional_fixture(file,global);
  auto *window=new MainWindow;window->resize(1200,900);window->show();QVERIFY(!window->findChild<QAction*>("actionIcosahedralAnalysis"));QCOMPARE(window->read_file(file),0);
  auto *tree=window->findChild<FileTreeWidget*>();const auto items=tree->findItems("field",Qt::MatchExactly|Qt::MatchRecursive);QCOMPARE(items.size(),1);QString error;
  QVERIFY2(open_context_analysis(tree,items.first(),error),qPrintable(error));auto *view=window->findChild<IcosahedralView*>();QVERIFY(view);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),20000);QCOMPARE(view->layer_axis_count(),1);QCOMPARE(view->layer_count(0),3);
  auto *owned=view->findChild<RasterView*>();QVERIFY(owned);QCOMPARE(owned->parentWidget(),view);
  auto *player=window->findChild<LayerPlayback*>("layerPlayback");auto *play=window->findChild<QAction*>("actionLayerPlay");QVERIFY(player&&play&&play->isEnabled());
  window->findChild<QSpinBox*>("playbackIntervalSpin")->setValue(10000);
  auto *display=view->findChild<QComboBox*>("icoDisplayMode");QCOMPARE(display->currentIndex(),4);QVERIFY(!view->image_for_export().isNull());
  auto *gl=view->findChild<Ico3DCanvas*>();QQuaternion rotation;QImage first;
  if(gl){display->setCurrentIndex(0);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),10000);}
  if(gl){gl->center_on(111,26);rotation=gl->rotation();first=gl->grabFramebuffer();QVERIFY(!first.isNull());}
  const auto first_values=view->result()->node_values;
  play->trigger();QVERIFY(player->is_playing());QCOMPARE(player->target(),view);
  // Level clicks during Play update automatically and gate advance until ready.
  auto *level=view->findChild<QSpinBox*>("icoLevel");level->setFocus();QTest::keyClick(level,Qt::Key_Up);QCOMPARE(level->value(),4);player->advance_frame();QCOMPARE(view->layer_index(0),0);QVERIFY(player->is_playing());QTRY_VERIFY_WITH_TIMEOUT(view->ready()&&view->result()->mesh.level==4,20000);
  QTest::keyClick(level,Qt::Key_Down);QCOMPARE(level->value(),3);QTRY_VERIFY_WITH_TIMEOUT(view->ready()&&view->result()->mesh.level==3,20000);
  if(gl){gl->set_highlight(-1);first=gl->grabFramebuffer();}
  player->advance_frame();QCOMPARE(view->layer_index(0),1);
  QVERIFY(!view->ready());QVERIFY(!view->result());QVERIFY(view->image_for_export().isNull());QVERIFY(!view->findChild<QPushButton*>("icoExportNetcdf")->isEnabled());
  if(gl){QVERIFY(!gl->pick_at(gl->rect().center()).hit);QCOMPARE(gl->grabFramebuffer(),first);}
  player->advance_frame();QCOMPARE(view->layer_index(0),1);play->trigger();QVERIFY(!player->is_playing());
  QTRY_VERIFY_WITH_TIMEOUT(view->ready(),20000);QCOMPARE(view->result()->source.leading_indices[0],size_t(1));QCOMPARE(view->layer_index(0),1);
  bool changed=false;for(size_t node=0;node<first_values.size();++node)if(std::isfinite(first_values[node])&&std::abs(first_values[node]-view->result()->node_values[node])>.01)changed=true;QVERIFY(changed);
  const QString base_evidence=qEnvironmentVariable("HDFSTUDIO_ICO_EVIDENCE_DIR");QString evidence;
  if(!base_evidence.isEmpty()){evidence=QDir(base_evidence).filePath(global?"global-animation":"regional-animation");QVERIFY(QDir().mkpath(evidence));}
  if(gl){QCOMPARE(gl->rotation(),rotation);const auto second=gl->grabFramebuffer();QVERIFY(second!=first);if(!evidence.isEmpty()){QVERIFY(first.save(QDir(evidence).filePath("animation-frame-0.png")));QVERIFY(second.save(QDir(evidence).filePath("animation-frame-1.png")));}}
  window->findChild<QSpinBox*>("playbackIntervalSpin")->setValue(100);play->trigger();QTRY_VERIFY_WITH_TIMEOUT(!player->is_playing(),20000);QVERIFY(view->ready());QCOMPARE(view->layer_index(0),2);QCOMPARE(view->result()->source.leading_indices[0],size_t(2));
  if(gl){QCOMPARE(gl->rotation(),rotation);const auto third=gl->grabFramebuffer();QVERIFY(third!=first);if(!evidence.isEmpty())QVERIFY(third.save(QDir(evidence).filePath("animation-frame-2.png")));}
  for(int layout=4;layout<=5;++layout){display->setCurrentIndex(layout);QTRY_VERIFY(view->ready());const auto last=view->image_for_export();QVERIFY(!last.isNull());view->set_layer_index(0,0);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),20000);const auto start=view->image_for_export();QVERIFY(start!=last);play->trigger();QTRY_VERIFY_WITH_TIMEOUT(!player->is_playing(),20000);QVERIFY(view->ready());QCOMPARE(view->result()->source.leading_indices[0],size_t(2));QVERIFY(view->image_for_export()!=start);if(!evidence.isEmpty())QVERIFY(view->image_for_export().save(QDir(evidence).filePath(QString("animation-five-slices-%1.png").arg(layout))));}
  view->set_layer_index(0,0);view->set_layer_index(0,2);view->set_layer_index(0,1);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),20000);QCOMPARE(view->result()->source.leading_indices[0],size_t(1));
  play->trigger();QVERIFY(player->is_playing());view->findChild<QPushButton*>("icoCancel")->click();QVERIFY(!player->is_playing());QVERIFY(!view->result());view->run_analysis();QTRY_VERIFY_WITH_TIMEOUT(view->ready(),20000);
  auto *item=items.first()->data(0,Qt::UserRole).value<ItemData*>();QVERIFY(item);window->add_image(item);
  auto *mdi=window->findChild<QMdiArea*>();auto *raster=qobject_cast<RasterView*>(mdi->currentSubWindow()->widget());QVERIFY(raster);raster->set_layer_index(0,1);QTRY_VERIFY_WITH_TIMEOUT(raster->plane_snapshot().values&&!raster->render_pending(),10000);
  error.clear();QVERIFY2(open_context_analysis(tree,items.first(),error),qPrintable(error));auto *following=qobject_cast<IcosahedralView*>(mdi->currentSubWindow()->widget());QVERIFY(following&&following!=view);QVERIFY(!following->findChild<RasterView*>());QTRY_VERIFY_WITH_TIMEOUT(following->ready(),20000);QCOMPARE(following->result()->source.values,raster->plane_snapshot().values);
  QPointer<IcosahedralView> guard(following);delete raster;QVERIFY(!following->result());QVERIFY(!following->ready());
  delete window;QVERIFY(guard.isNull());QTRY_VERIFY_WITH_TIMEOUT(!IcosahedralView::any_busy()&&!DataReadController::any_busy()&&!GeoGridController::any_busy(),20000);
}
void IcosahedralTest::realRegionalSample(){
  const auto file=qEnvironmentVariable("HDFSTUDIO_ICO_REGIONAL_SAMPLE");if(file.isEmpty())QSKIP("Read-only regional sample not configured");
  QFile input(file);QVERIFY(input.open(QIODevice::ReadOnly));const auto hash=QCryptographicHash::hash(input.readAll(),QCryptographicHash::Sha256);input.close();
  MainWindow window;window.resize(1200,900);window.show();QCOMPARE(window.read_file(file),0);auto *tree=window.findChild<FileTreeWidget*>();const auto items=tree->findItems("precipitation",Qt::MatchExactly|Qt::MatchRecursive);QCOMPARE(items.size(),1);
  QString error;QVERIFY2(open_context_analysis(tree,items.first(),error),qPrintable(error));auto *view=window.findChild<IcosahedralView*>();QVERIFY(view);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),90000);
  const auto result=view->result();QCOMPARE(result->source.rows,size_t(1201));QCOMPARE(result->source.cols,size_t(1401));QVERIFY(!result->grid.longitude.periodic);QVERIFY(result->forward_method.contains("Regional"));
  size_t finite=0,missing=0;for(size_t node=0;node<result->mesh.xyz.size();++node){const auto &xyz=result->mesh.xyz[node];const double lon=std::atan2(xyz[1],xyz[0])*180/pi,lat=std::asin(std::clamp(xyz[2],-1.,1.))*180/pi;
    if(lon<70||lon>140||lat<0||lat>60){++missing;QVERIFY(std::isnan(result->node_values[node]));}else {++finite;QVERIFY(std::isfinite(result->node_values[node]));}}
  QVERIFY(finite>30);QVERIFY(missing>finite);QVERIFY(result->compared>10000);QCOMPARE(view->layer_count(0),1);QVERIFY(!window.findChild<QAction*>("actionLayerPlay")->isEnabled());
  const auto evidence=qEnvironmentVariable("HDFSTUDIO_ICO_EVIDENCE_DIR");QTemporaryDir temporary;const auto output=evidence.isEmpty()?temporary.path():evidence;
  QVERIFY2(view->export_data(QDir(output).filePath("gui-regional.nc")).isEmpty(),"regional export");
  auto *display=view->findChild<QComboBox*>("icoDisplayMode");QCOMPARE(display->currentIndex(),4);QVERIFY(view->image_for_export().save(QDir(output).filePath("regional-five-strips.png")));
  display->setCurrentIndex(5);QTRY_VERIFY(view->ready());QVERIFY(view->image_for_export().save(QDir(output).filePath("regional-five-cards.png")));QVERIFY(window.grab().save(QDir(output).filePath("regional-five-slices-layout.png")));
  auto *gl=view->findChild<Ico3DCanvas*>();if(gl){display->setCurrentIndex(0);QTRY_VERIFY(view->ready());const auto hit=gl->pick_at(QPointF(gl->width()/2.,gl->height()/2.));QVERIFY(hit.hit);QVERIFY2(std::abs(hit.lon_deg-105)<.001,qPrintable(QString::number(hit.lon_deg,'g',12)));QVERIFY2(std::abs(hit.lat_deg-30)<.001,qPrintable(QString::number(hit.lat_deg,'g',12)));QVERIFY(std::isfinite(hit.value));const auto image=view->image_for_export();QVERIFY(!image.isNull());QVERIFY(image.save(QDir(output).filePath("regional-china-gl.png")));QVERIFY(window.grab().save(QDir(output).filePath("regional-main-layout.png")));view->findChild<QComboBox*>("ico3DShapeCombo")->setCurrentIndex(1);QVERIFY(view->image_for_export().save(QDir(output).filePath("regional-china-polyhedron.png")));}
  view->findChild<QComboBox*>("icoDisplayMode")->setCurrentIndex(2);QVERIFY(!view->image_for_export().isNull());
  QVERIFY(input.open(QIODevice::ReadOnly));QCOMPARE(QCryptographicHash::hash(input.readAll(),QCryptographicHash::Sha256),hash);input.close();
  window.close();QTRY_VERIFY_WITH_TIMEOUT(!IcosahedralView::any_busy(),20000);
}
void IcosahedralTest::fiveChartsTopologyRenderAndExport(){
  for(int level=0;level<=6;++level){Icosahedral::Mesh mesh;QVERIFY2(Icosahedral::build_mesh(level,mesh).isEmpty(),"atlas build");const size_t n=size_t(1)<<level;QCOMPARE(mesh.atlas.node_chart.size(),10*n*n);QCOMPARE(mesh.atlas.triangles.size(),20*n*n);std::set<int32_t> owners(mesh.atlas.node_chart.begin(),mesh.atlas.node_chart.end());QCOMPARE(owners.size(),mesh.xyz.size()-2);QVERIFY(!owners.count(-1)&&!owners.count(5)&&!owners.count(6));
    std::array<size_t,5> counts{};for(const auto &tri:mesh.atlas.triangles){QVERIFY(tri.chart>=0&&tri.chart<5);QVERIFY(tri.quad>=0&&tri.quad<4);++counts[tri.chart];for(const auto &p:tri.uv){QVERIFY(p.u>=0&&p.u<=2);QVERIFY(p.v>=0&&p.v<=1);}}for(auto count:counts)QCOMPARE(count,4*n*n);
    if(level==3){Ico3DBuffers buffers;QVERIFY(ico_build_3d_topology_buffers(mesh,buffers));QCOMPARE(buffers.chart_seam_lines.size(),30*n);}
  }
  GeoGrid::Grid grid;auto source=Icosahedral::demo(grid);auto result=Icosahedral::analyze(source,grid,3);QVERIFY(result->error.isEmpty());const auto values=result->node_values;const auto xyz=result->mesh.xyz;
  QString error;const auto palettes=HdfColorMap::load_registry(error);QVERIFY(error.isEmpty());HdfColorMap::Mapping mapping;mapping.minimum=-2;mapping.maximum=2;IcoAtlasRenderer renderer;renderer.mesh_lines=false;renderer.seams=false;
  const auto strips=renderer.render(*result,palettes.front(),mapping);QCOMPARE(strips.size(),QSize(1200,1570));
  renderer.rectangular=true;const auto cards=renderer.render(*result,palettes.front(),mapping);QVERIFY(cards!=strips);const int n=8;
  for(int chart=0;chart<5;++chart)for(int row=0;row<n;++row)for(int col=0;col<2*n;++col){const int x=int(65+(col+.5)*820/(2*n)),y=int(55+chart*300+(n-row-.5)*215/n);const auto hit=renderer.pick(x,y,*result);const int node=result->mesh.atlas.node_chart[size_t(chart*2*n*n+row*2*n+col)];QCOMPARE(hit.node,node);QCOMPARE(hit.value,result->node_values[node]);QCOMPARE(cards.pixel(x,y),HdfColorMap::map_value(palettes.front(),mapping,result->node_values[node]));}
  QCOMPARE(renderer.pick(920,80,*result).node,5);QCOMPARE(renderer.pick(920,180,*result).node,6);QCOMPARE(result->node_values,values);QCOMPARE(result->mesh.xyz,xyz);
  QTemporaryDir directory;const auto file=directory.filePath("atlas.nc");QVERIFY(Icosahedral::export_netcdf(file,*result).isEmpty());int root,var;nc_ok(nc_open(file.toLocal8Bit().constData(),NC_NOWRITE,&root));nc_ok(nc_inq_varid(root,"chart_node",&var));std::vector<int> owner(result->mesh.atlas.node_chart.size());nc_ok(nc_get_var_int(root,var,owner.data()));QCOMPARE(owner,result->mesh.atlas.node_chart);nc_ok(nc_inq_varid(root,"chart_value",&var));std::vector<double> tensor(owner.size());nc_ok(nc_get_var_double(root,var,tensor.data()));for(size_t i=0;i<owner.size();++i)QCOMPARE(tensor[i],result->node_values[owner[i]]);nc_ok(nc_close(root));
}
void IcosahedralTest::gribContextAndMissingFrameStops(){
  const auto file=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../../testdata/grib/mini_missing_slice.grb2");QVERIFY(QFileInfo::exists(file));
  MainWindow window;window.resize(1200,900);window.show();QCOMPARE(window.read_file(file),0);auto *tree=window.findChild<FileTreeWidget*>();QVERIFY(tree);QTreeWidgetItem *item=nullptr;
  QTreeWidgetItemIterator iterator(tree);while(*iterator){auto *candidate=*iterator;auto *data=candidate->data(0,Qt::UserRole).value<ItemData*>();if(data&&data->m_grib&&data->m_kind==ItemData::Variable&&data->m_object.object_id.startsWith("/cube/")){item=candidate;break;}++iterator;}
  QVERIFY(item);QString error;QVERIFY2(open_context_analysis(tree,item,error),qPrintable(error));auto *view=window.findChild<IcosahedralView*>();QVERIFY(view);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),20000);
  auto *player=window.findChild<LayerPlayback*>("layerPlayback");auto *play=window.findChild<QAction*>("actionLayerPlay");QVERIFY(player&&play&&play->isEnabled());window.findChild<QSpinBox*>("playbackIntervalSpin")->setValue(10000);
  play->trigger();QVERIFY(player->is_playing());player->advance_frame();QTRY_VERIFY_WITH_TIMEOUT(!player->is_playing(),20000);QVERIFY(!view->result());QVERIFY(view->image_for_export().isNull());QVERIFY(view->findChild<QLabel*>("icoStatus")->text().contains("MissingSlice"));
  view->set_layer_index(0,0);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),20000);play->trigger();player->advance_frame();
  QPointer<IcosahedralView> guard(view);window.close_file_of(item);QVERIFY(guard.isNull());QTRY_VERIFY_WITH_TIMEOUT(!DataReadController::any_busy()&&!GeoGridController::any_busy()&&!IcosahedralView::any_busy(),20000);
}
void IcosahedralTest::levelAndModeClicks_data(){
  QTest::addColumn<bool>("global");QTest::addColumn<bool>("real");
  QTest::newRow("regional")<<false<<false;QTest::newRow("global")<<true<<false;
  QTest::newRow("regional-real")<<false<<true;QTest::newRow("global-real")<<true<<true;
}
void IcosahedralTest::levelAndModeClicks(){
  QFETCH(bool,global);QFETCH(bool,real);QTemporaryDir temporary;
  QString path=temporary.filePath("clicks.nc"),variable="/field";std::vector<hsize_t> dimensions{3,61,71};
  if(real){path=qEnvironmentVariable(global?"HDFSTUDIO_GAUSSIAN_SAMPLE":"HDFSTUDIO_ICO_REGIONAL_SAMPLE");QVERIFY2(!path.isEmpty(),"Real sample must be configured for click regression");variable=global?"/field/0":"/precipitation";dimensions={1,1201,1401};}
  else regional_fixture(path,global);
  auto *raster=real&&global?new RasterView(path,variable,GribData::describe(path,variable)):new RasterView(path.toLocal8Bit().toStdString(),variable.toStdString(),dimensions);
  auto *view=new IcosahedralView(raster,true);view->resize(1100,900);view->show();
  const auto cleanup=qScopeGuard([&]{delete view;});
  auto *mode=view->findChild<QComboBox*>("icoDisplayMode");auto *spin=view->findChild<QSpinBox*>("icoLevel");auto *run=view->findChild<QPushButton*>("icoRun");auto *cancel=view->findChild<QPushButton*>("icoCancel");
  mode->setCurrentIndex(4);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),90000);
  auto choose_mode=[&](int index){QTest::mouseClick(mode,Qt::LeftButton,Qt::NoModifier,QPoint(mode->width()-10,mode->height()/2));auto *list=mode->view();QTest::keyClick(list,Qt::Key_Home);for(int i=0;i<index;++i)QTest::keyClick(list,Qt::Key_Down);QTest::keyClick(list,Qt::Key_Return);};
  auto step_level=[&](bool up){QStyleOptionSpinBox option;option.initFrom(spin);option.rect=spin->rect();option.buttonSymbols=spin->buttonSymbols();const auto area=spin->style()->subControlRect(QStyle::CC_SpinBox,&option,up?QStyle::SC_SpinBoxUp:QStyle::SC_SpinBoxDown,spin);QTest::mouseClick(spin,Qt::LeftButton,Qt::NoModifier,area.center());};
  const auto base=qEnvironmentVariable("HDFSTUDIO_ICO_EVIDENCE_DIR");QString output;
  if(!base.isEmpty()){output=QDir(base).filePath(QString::fromLatin1(QTest::currentDataTag()));QVERIFY(QDir().mkpath(output));}
  auto record=[&](const QString &step){if(output.isEmpty())return;QFile log(QDir(output).filePath("steps.txt"));if(log.open(QIODevice::Append|QIODevice::Text)){QTextStream stream(&log);stream<<QDateTime::currentDateTime().toString(Qt::ISODateWithMs)<<" "<<step<<" | "<<view->findChild<QLabel*>("icoStatus")->text()<<"\n";}};
  choose_mode(0);QCOMPARE(mode->currentIndex(),0);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),15000);
  auto *gl=view->findChild<Ico3DCanvas*>();QVERIFY2(gl,"Native GL canvas required for click regression");QVERIFY2(gl->gl_error().isEmpty(),qPrintable(gl->gl_error()));
  auto has_mesh=[&](){const auto image=gl->grabFramebuffer();if(image.isNull())return false;QSet<QRgb> colors;for(int y=0;y<image.height();y+=8)for(int x=0;x<image.width();x+=8)colors.insert(image.pixel(x,y));return colors.size()>5;};
  QVERIFY(has_mesh());const auto camera=gl->rotation();
  step_level(true);QCOMPARE(spin->value(),4);
  QVERIFY(!view->ready());QVERIFY(!view->result());QVERIFY(!view->findChild<QPushButton*>("icoExportPng")->isEnabled());QVERIFY(!gl->pick_at(QPointF(gl->width()/2.,gl->height()/2.)).hit);QVERIFY(has_mesh());
  if(!output.isEmpty())QVERIFY(view->grab().save(QDir(output).filePath("immediate-level-change.png")));
  QTRY_VERIFY_WITH_TIMEOUT(view->ready()&&view->result()->mesh.level==4,20000);
  QVERIFY(has_mesh());QCOMPARE(gl->rotation(),camera);
  for(int pass=0;pass<(real?1:2);++pass){
    for(int index:{5,0,4,1,0}){record(QString("pass %1 mode %2").arg(pass).arg(index));choose_mode(index);QCOMPARE(mode->currentIndex(),index);step_level(false);QCOMPARE(spin->value(),3);choose_mode((index+4)%6);step_level(true);QCOMPARE(spin->value(),4);choose_mode(index);record("waiting latest L4");QTRY_VERIFY_WITH_TIMEOUT(view->ready()&&view->result()->mesh.level==4,90000);QVERIFY(!view->image_for_export().isNull());if(index<2)QVERIFY(has_mesh());record("displayed latest L4");}
  }
  choose_mode(0);step_level(true);step_level(false);step_level(false);QCOMPARE(spin->value(),3);QTest::mouseClick(run,Qt::LeftButton);QTest::mouseClick(run,Qt::LeftButton);QTest::mouseClick(run,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(view->ready()&&view->result()->mesh.level==3,90000);QVERIFY(has_mesh());
  QTest::mouseClick(cancel,Qt::LeftButton);QVERIFY(!view->result());QTest::mouseClick(run,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),90000);QVERIFY(has_mesh());
  step_level(true);QTest::mouseClick(cancel,Qt::LeftButton);QTest::qWait(350);QVERIFY(!view->result());QTest::mouseClick(run,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(view->ready()&&view->result()->mesh.level==4,90000);QVERIFY(has_mesh());
  if(!real){
    // Exercise every level through keyboard events, without a manual Run.
    for(int target:{0,1,2,3,4,5,6,2}){spin->setFocus();QTest::keyClick(spin,Qt::Key_Home,Qt::ControlModifier);QTest::keyClick(spin,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(spin,QString::number(target));QTest::keyClick(spin,Qt::Key_Return);record(QString("all-level %1").arg(target));QCOMPARE(spin->value(),target);QTRY_VERIFY_WITH_TIMEOUT(view->ready()&&view->result()->mesh.level==target,30000);QVERIFY(has_mesh());QCOMPARE(gl->rotation(),camera);}
    // Click style/overlay controls and drag the rendered mesh after rebuilding.
    auto *shape=view->findChild<QComboBox*>("ico3DShapeCombo");shape->setFocus();QTest::keyClick(shape,Qt::Key_End);QCOMPARE(shape->currentIndex(),1);QVERIFY(has_mesh());
    const QPoint from=gl->rect().center(),to=from+QPoint(70,40);QTest::mousePress(gl,Qt::LeftButton,Qt::NoModifier,from);QMouseEvent drag(QEvent::MouseMove,to,gl->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(gl,&drag);QTest::mouseRelease(gl,Qt::LeftButton,Qt::NoModifier,to);QVERIFY(gl->rotation()!=camera);QVERIFY(has_mesh());
    for(const QString &id:{"icoEdges","icoSeams","ico3DBaseEdgesCheck","ico3DShadingCheck","icoReverse"}){auto *control=view->findChild<QCheckBox*>(id);QTest::mouseClick(control,Qt::LeftButton);QVERIFY(has_mesh());QTest::mouseClick(control,Qt::LeftButton);}
    choose_mode(5);QCOMPARE(mode->currentIndex(),5);QTRY_VERIFY(view->ready());auto *canvas=view->findChild<QWidget*>("icosahedralCanvas");QVERIFY(canvas->isVisible());const auto before=canvas->grab().toImage();const auto exported=view->image_for_export();const QPoint center=canvas->rect().center();QWheelEvent wheel(QPointF(center),QPointF(canvas->mapToGlobal(center)),QPoint(),QPoint(0,240),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(canvas,&wheel);QVERIFY(canvas->grab().toImage()!=before);QCOMPARE(view->image_for_export(),exported);QTest::mouseClick(view->findChild<QPushButton*>("icoFit"),Qt::LeftButton);QCOMPARE(canvas->grab().toImage(),before);choose_mode(0);QCOMPARE(mode->currentIndex(),0);QTRY_VERIFY(view->ready());QVERIFY(has_mesh());
  }
  if(!output.isEmpty()){QVERIFY(view->grab().save(QDir(output).filePath("final-3d-layout.png")));QVERIFY(view->image_for_export().save(QDir(output).filePath("final-3d.png")));}
  step_level(false);delete view;view=nullptr;QTRY_VERIFY_WITH_TIMEOUT(!IcosahedralView::any_busy()&&!DataReadController::any_busy()&&!GeoGridController::any_busy(),20000);
}
QTEST_MAIN(IcosahedralTest)
#include "icosahedral_test.moc"
