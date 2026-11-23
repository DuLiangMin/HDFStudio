// GRIB tree/properties gate. Run grib_browser_tests -o <evidence>.txt,txt.
// Inputs: read-only testdata/grib fixtures; generated files use QTemporaryDir only.
#include <QtTest>
#include <QScopeGuard>
#include <cmath>
#include <cstring>
#include <eccodes.h>
#include "hdf_explorer.hpp"
#include "data_read_controller.hpp"
#include "read_scheduler.hpp"
#include "grib_table_view.hpp"
#include "analysis_table_view.hpp"
#include "raster_view.hpp"
#include "color_bar.hpp"
#include "hovmoller_view.hpp"
#include "netcdf.h"

class GribBrowserTest : public QObject {
  Q_OBJECT
  QString fixture(const QString &name) const {return QString(HDFSTUDIO_TEST_SOURCE_DIR)+"/testdata/grib/"+name;}
  QTreeWidgetItem *branch(QTreeWidgetItem *root,const QString &prefix) const {
    for(int i=0;i<root->childCount();++i)if(root->child(i)->text(0).startsWith(prefix))return root->child(i);
    return nullptr;
  }
private slots:
  void sectionTreePresetsAndExports() {
    MainWindow window;window.resize(1200,850);window.show();QCOMPARE(window.read_file(fixture("mini_cube.grb2")),0);
    auto *tree=window.findChild<FileTreeWidget*>();auto *root=tree->topLevelItem(0);auto *target=branch(root,"Variables")->child(0);tree->setCurrentItem(target);
    auto *action=window.findChild<QAction*>("actionSpaceTimeSection");QVERIFY(action);
    auto *menu=qobject_cast<QMenu*>(action->parent());QVERIFY(menu);QVERIFY(QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection));QVERIFY(action->isEnabled());action->trigger();
    auto *view=window.findChild<HovmollerView*>();QVERIFY(view);QTRY_VERIFY_WITH_TIMEOUT(view->ready()||view->state()=="Error",15000);QVERIFY2(view->ready(),qPrintable(view->error()));
    auto snapshot=view->snapshot();QCOMPARE(snapshot->info.source_kind,QString("grib"));QCOMPARE(snapshot->metadata["schema"].toInt(),2);QCOMPARE(snapshot->metadata["decode_profile"].toString(),QString("GribDecodedV1"));
    QCOMPARE(snapshot->spec.row_axis,size_t(2));QCOMPARE(snapshot->spec.col_axis,size_t(0));QCOMPARE(snapshot->image.size(),QSize(2,3));
    for(size_t y=0;y<3;++y)for(size_t t=0;t<2;++t)QCOMPARE((*snapshot->decoded)[y*2+t],50000.+600.*t+4.*y);
    auto *lat=view->findChild<QPushButton*>("hovmollerLatTime"),*lon=view->findChild<QPushButton*>("hovmollerLonTime");QVERIFY(lat->isEnabled());QVERIFY(lon->isEnabled());QVERIFY(lat->text().contains("index"));
    const auto graphics=view->findChild<SectionCanvas*>()->view();const auto rect=graphics->mapFromScene(QRectF(0,0,2,3)).boundingRect();QVERIFY(std::abs(rect.width()-graphics->viewport()->width())<=6);
    auto *axis_labels=view->findChild<QWidget*>("hovmollerXAxisLabels");QVERIFY(axis_labels);QVERIFY(axis_labels->height()>=4*view->fontMetrics().lineSpacing()+48);QVERIFY(!axis_labels->grab().isNull());QVERIFY(axis_labels->accessibleName().contains("[ref "));QVERIFY(axis_labels->accessibleName().contains("step "));
    QSignalSpy probe(view->findChild<SectionCanvas*>(),&SectionCanvas::probe_changed);const auto local=graphics->mapFromScene(QPointF(1.5,.5));QMouseEvent move(QEvent::MouseMove,local,graphics->viewport()->mapToGlobal(local),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(graphics->viewport(),&move);QVERIFY(!probe.isEmpty());QVERIFY(probe.last()[2].toString().contains("[ref "));
    QTemporaryDir folder;QVERIFY(view->export_csv(folder.path()+"/section.csv").isEmpty());QVERIFY(view->export_png(folder.path()+"/section.png").isEmpty());QCOMPARE(QImage(folder.path()+"/section.png").size(),QSize(2,3));
    QFile csv(folder.path()+"/section.csv");QVERIFY(csv.open(QIODevice::ReadOnly));const auto bytes=csv.readAll();QVERIFY(bytes.contains("GribDecodedV1"));QVERIFY(bytes.contains("source_labels"));QVERIFY(bytes.contains("[ref "));QVERIFY(!view->export_csv(fixture("mini_cube.grb2"),true).isEmpty());
    lon->click();QTRY_VERIFY_WITH_TIMEOUT(view->ready(),10000);QCOMPARE(view->snapshot()->image.size(),QSize(4,2));
    view->set_fixed_index(2,1);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),10000);QCOMPARE((*view->snapshot()->decoded)[1*4+3],50607.);
    view->set_axes(2,0);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),10000);view->set_fixed_index(1,1);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),10000);QCOMPARE((*view->snapshot()->decoded)[2*2+1],85608.);
    if(const auto evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");!evidence.isEmpty()){QVERIFY(view->export_csv(evidence+"/grib-gui-section.csv",true).isEmpty());QVERIFY(view->grab().save(evidence+"/grib-gui-section.png"));}
    window.close_all_files();QTRY_VERIFY_WITH_TIMEOUT(!SliceJobController::any_busy(),5000);QVERIFY(window.findChildren<HovmollerView*>().empty());
  }
  void sectionMissingLatestCancelAndSourceChange() {
    const auto file=fixture("mini_missing_slice.grb2");HovmollerView view(file,GribData::open(file)->cubes.front().id);
    QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(view.snapshot()->missing,size_t(3));QCOMPARE(view.snapshot()->metadata["missing_fields"].toString(),QString("1"));
    view.set_axes(0,1);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);view.set_fixed_index(2,2);view.set_fixed_index(3,3);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE((*view.snapshot()->decoded)[0],50011.);QVERIFY(std::isnan((*view.snapshot()->decoded)[2]));
    view.set_axes(2,0);for(int i=0;i<8;++i)view.set_fixed_index(3,size_t(i%4));view.set_fixed_index(3,1);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(view.snapshot()->spec.fixed_indices[3],size_t(1));
    view.set_fixed_index(3,2);view.cancel();QCOMPARE(view.state(),QString("Cancelled"));QVERIFY(!view.snapshot());QTRY_VERIFY_WITH_TIMEOUT(!SliceJobController::any_busy(),5000);
    QTemporaryDir folder;const auto source=folder.path()+"/changed.grb2";QVERIFY(QFile::copy(fixture("mini_cube.grb2"),source));auto release=qScopeGuard([&]{GribData::release(source);});
    HovmollerView changed(source,GribData::open(source)->cubes.front().id);QTRY_VERIFY_WITH_TIMEOUT(changed.ready(),10000);{QFile modify(source);QVERIFY(modify.open(QIODevice::Append));modify.write("x");}
    changed.set_fixed_index(3,1);QCOMPARE(changed.state(),QString("Error"));QVERIFY(changed.error().contains("SourceChanged"));QVERIFY(!changed.snapshot());
    HovmollerView unsupported(fixture("mini_simple.grb2"),"/field/999999");QTRY_VERIFY_WITH_TIMEOUT(unsupported.state()=="Error",10000);QVERIFY(!unsupported.snapshot());
  }
  void sectionCfsBusinessValues() {
    const auto file=qEnvironmentVariable("HDFSTUDIO_GRIB_CFS_SAMPLE");QVERIFY2(!file.isEmpty(),"Set HDFSTUDIO_GRIB_CFS_SAMPLE");const auto catalog=GribData::open(file);
    QString target;for(const auto &cube:catalog->cubes)if(catalog->fields[size_t(cube.representative)].short_name=="prate")target=cube.id;QVERIFY(!target.isEmpty());
    HovmollerView view(file,target);view.resize(1200,850);view.show();QTRY_VERIFY_WITH_TIMEOUT(view.ready()||view.state()=="Error",120000);QVERIFY2(view.ready(),qPrintable(view.error()));
    const auto snapshot=view.snapshot();QCOMPARE(snapshot->image.size(),QSize(480,190));
    for(int t:{0,1,2,17,479}){const auto plane=GribData::read(*catalog,target,{size_t(t)});QVERIFY(plane.error.isEmpty());for(size_t y=0;y<190;++y)QCOMPARE((*snapshot->decoded)[y*480+size_t(t)],plane.values[y*384]);}
    const auto evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");if(!evidence.isEmpty()){QVERIFY(view.export_csv(evidence+"/cfs-gui-section.csv").isEmpty());QVERIFY(view.export_png(evidence+"/cfs-gui-section.png").isEmpty());QVERIFY(view.grab().save(evidence+"/cfs-gui-window.png"));}
  }
  void doubleClickPrefersImageForGribArrays() {
    MainWindow window;window.resize(1200,800);window.show();
    QCOMPARE(window.read_file(fixture("mini_cube.grb2")),0);
    auto *tree=window.findChild<FileTreeWidget*>();QVERIFY(tree);
    auto *root=tree->topLevelItem(0);auto *variables=branch(root,"Variables");QVERIFY(variables);
    auto *cube=variables->child(0);tree->setCurrentItem(cube);
    emit tree->itemDoubleClicked(cube,0);
    auto images=window.findChildren<RasterView*>();QCOMPARE(images.size(),1);
    QVERIFY(!images.front()->image_for_export().isNull());
    QVERIFY(!tree->findChild<QDialog*>("propertiesDialog"));
    QCOMPARE(images.front()->layer_axis_count(),2);
    auto *page=branch(root,"Messages")->child(0);page->setExpanded(true);
    auto *field=page->child(0)->child(0);tree->setCurrentItem(field);
    emit tree->itemDoubleClicked(field,0);
    images=window.findChildren<RasterView*>();QCOMPARE(images.size(),2);
    QVERIFY(!images.back()->image_for_export().isNull());
    QVERIFY(!tree->findChild<QDialog*>("propertiesDialog"));
    emit tree->itemDoubleClicked(variables,0);
    auto *properties=tree->findChild<QDialog*>("propertiesDialog");QVERIFY(properties);delete properties;
    QCOMPARE(window.findChildren<RasterView*>().size(),2);
    window.close_all_files();QTRY_VERIFY_WITH_TIMEOUT(!DataReadController::any_busy(),10000);
  }
  void virtualAndPhysicalProperties() {
    MainWindow window;QCOMPARE(window.read_file(fixture("mini_cube.grb2")),0);
    auto *tree=window.findChild<FileTreeWidget*>();QVERIFY(tree);QCOMPARE(tree->topLevelItemCount(),1);
    auto *root=tree->topLevelItem(0);QVERIFY(root->text(0).contains("4 messages / 4 fields"));
    auto *variables=branch(root,"Variables"),*messages=branch(root,"Messages");QVERIFY(variables);QVERIFY(messages);QCOMPARE(variables->childCount(),1);
    auto *cube=variables->child(0);QVERIFY(cube->data(0,Qt::UserRole+10).toString().startsWith("/cube/"));
    const auto report=tree->item_properties_text(cube);QVERIFY(report.contains("Virtual variable"));QVERIFY(report.contains("representative field"));QVERIFY(report.contains("Varying keys:"));
    QVERIFY(tree->item_properties_text(cube->child(0)).contains("time (reference + step): length=2"));
    QVERIFY(tree->item_properties_text(variables).contains("not HDF groups"));
    QCOMPARE(messages->childCount(),1);auto *page=messages->child(0);QCOMPARE(page->childCount(),1);
    page->setExpanded(true);QCOMPARE(page->childCount(),4);auto *field=page->child(0)->child(0);QVERIFY(field);
    const auto field_report=tree->item_properties_text(field);QVERIFY(field_report.contains("sub-field: 0; offset: 0; length:"));
    tree->setCurrentItem(field);QVERIFY(QMetaObject::invokeMethod(tree,"show_item_properties",Qt::DirectConnection));
    auto *dialog=tree->findChild<QDialog*>("propertiesDialog");QVERIFY(dialog);
    auto *copy=dialog->findChild<QPushButton*>("propertiesCopyAll");QVERIFY(copy);copy->click();QCOMPARE(QApplication::clipboard()->text(),field_report);delete dialog;
    auto *cf=window.findChild<QPlainTextEdit*>("cfCheckText"),*netcdf=window.findChild<QPlainTextEdit*>("netcdf4Text");QVERIFY(cf);QVERIFY(netcdf);
    QVERIFY(cf->toPlainText().contains("not applicable to GRIB"));QVERIFY(!cf->property("pending").toBool());QVERIFY(!netcdf->property("pending").toBool());
    const auto evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");
    if(!evidence.isEmpty()){QDir().mkpath(evidence);window.resize(1200,800);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));messages->setExpanded(true);page->child(0)->setExpanded(true);QVERIFY(window.grab().save(evidence+"/browser.png"));}
    window.close_all_files();QCOMPARE(tree->topLevelItemCount(),0);QVERIFY(window.property("openSourceFiles").toStringList().isEmpty());
  }
  void pagesAndSessionsStaySeparate() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    const auto path=temporary.filePath("many.bin");QFile input(fixture("mini_simple.grb2"));QVERIFY(input.open(QIODevice::ReadOnly));const auto message=input.readAll();
    QFile output(path);QVERIFY(output.open(QIODevice::WriteOnly));for(int i=0;i<257;++i)QCOMPARE(output.write(message),qint64(message.size()));output.close();
    MainWindow window;QCOMPARE(window.read_file(path),0);QCOMPARE(window.read_file(fixture("mini_multifield.grb2")),0);
    auto *tree=window.findChild<FileTreeWidget*>();QCOMPARE(tree->topLevelItemCount(),2);
    auto *many=tree->topLevelItem(0),*multi=tree->topLevelItem(1);
    auto *many_messages=branch(many,"Messages"),*multi_messages=branch(multi,"Messages");QVERIFY(many_messages);QVERIFY(multi_messages);
    QCOMPARE(many_messages->childCount(),2);QCOMPARE(many_messages->child(0)->childCount(),1);
    auto *multi_page=multi_messages->child(0);multi_page->setExpanded(true);QCOMPARE(multi_page->childCount(),1);QCOMPARE(multi_page->child(0)->childCount(),2);
    const auto second=tree->item_properties_text(multi_page->child(0)->child(1));QVERIFY(second.contains("sub-field: 1; offset: 0;"));QVERIFY(second.contains(fixture("mini_multifield.grb2")));
    auto *tail=many_messages->child(1);tail->setExpanded(true);QCOMPARE(tail->childCount(),1);QVERIFY(tail->child(0)->text(0).startsWith("Message 256 "));
    auto *first=many_messages->child(0);first->setExpanded(true);QCOMPARE(first->childCount(),256);QCOMPARE(multi_page->childCount(),1);
    const auto session=many->data(0,Qt::UserRole+13).toString();window.close_file_of(many);QCOMPARE(tree->topLevelItemCount(),1);
    QCOMPARE(window.read_file(path),0);QCOMPARE(tree->topLevelItemCount(),2);QVERIFY(tree->topLevelItem(1)->data(0,Qt::UserRole+13).toString()!=session);
    window.close_all_files();QCOMPARE(tree->topLevelItemCount(),0);
  }
  void unicodeAndMixedContainers() {
    QTemporaryDir temporary;const QString unicode=temporary.filePath(QString::fromUtf8("\xe6\xb5\x8b\xe8\xaf\x95")+QString::fromUcs4(U"\U0001f30d")+".data");
    QVERIFY(QFile::copy(fixture("mini_simple.grb2"),unicode));
    const QString classic=temporary.filePath("classic.nc"),hdf=temporary.filePath("hdf.nc");
    for(const auto &entry:QList<QPair<QString,int>>{{classic,NC_CLOBBER},{hdf,NC_CLOBBER|NC_NETCDF4}}) {
      int file=-1,dim=-1,var=-1;QCOMPARE(nc_create(entry.first.toLocal8Bit().constData(),entry.second,&file),NC_NOERR);
      QCOMPARE(nc_def_dim(file,"x",2,&dim),NC_NOERR);QCOMPARE(nc_def_var(file,"a",NC_DOUBLE,1,&dim,&var),NC_NOERR);QCOMPARE(nc_close(file),NC_NOERR);
    }
    MainWindow window;QCOMPARE(window.read_file(classic),0);QCOMPARE(window.read_file(unicode),0);QCOMPARE(window.read_file(hdf),0);
    auto *tree=window.findChild<FileTreeWidget*>();QCOMPARE(tree->topLevelItemCount(),3);QVERIFY(tree->item_properties_text(tree->topLevelItem(1)).contains(unicode));
    QCOMPARE(window.read_file(unicode),0);QCOMPARE(tree->topLevelItemCount(),3);
    window.close_file_of(tree->topLevelItem(2));QCOMPARE(tree->topLevelItemCount(),2);QVERIFY(window.property("openSourceFiles").toStringList().contains(unicode));
    window.close_all_files();QVERIFY(!DataReadController::any_busy());
  }
  void queuedIndexCancellationPublishesNothing() {
    auto *scheduler=ReadScheduler::instance();QVERIFY(scheduler->acquire());QVERIFY(scheduler->acquire());
    const auto release=qScopeGuard([scheduler]{scheduler->release();scheduler->release();});
    MainWindow window;QTimer cancel;cancel.setInterval(1);bool clicked=false;
    connect(&cancel,&QTimer::timeout,this,[&]{if(!DataReadController::any_busy())return;for(auto *widget:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QProgressDialog*>(widget))if(dialog->objectName()=="gribIndexProgress")if(auto *button=dialog->findChild<QPushButton*>()){clicked=true;button->click();cancel.stop();}});
    cancel.start();QCOMPARE(window.read_file(fixture("mini_showcase.grb2")),-1);QVERIFY(clicked);
    QCOMPARE(window.findChild<FileTreeWidget*>()->topLevelItemCount(),0);QVERIFY(!DataReadController::any_busy());QVERIFY(!window.property("gribIndexPending").toBool());
  }
  void corruptIndexPublishesNothing() {
    QTemporaryDir temporary;const auto path=temporary.filePath("truncated.data");QFile input(fixture("mini_simple.grb2"));QVERIFY(input.open(QIODevice::ReadOnly));
    QFile output(path);QVERIFY(output.open(QIODevice::WriteOnly));const auto bytes=input.readAll();output.write(bytes.left(bytes.size()-4));output.close();
    MainWindow window;QTimer dismiss;dismiss.setInterval(1);QString error;
    connect(&dismiss,&QTimer::timeout,this,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(widget)){error=box->text();box->accept();}});
    dismiss.start();QCOMPARE(window.read_file(path),-1);QVERIFY(!error.isEmpty());QCOMPARE(window.findChild<FileTreeWidget*>()->topLevelItemCount(),0);
  }
  void gridDecodedQuantizedCopyAndCsv() {
    QTemporaryDir temporary;
    for(int mode=0;mode<3;++mode) {
      GribTableView view(fixture("mini_simple.grb2"),"/field/0",mode);
      QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);auto *table=view.findChild<AnalysisTableView*>("gribDataTable");QVERIFY(table);auto *model=table->model();
      QCOMPARE(model->rowCount(),3);QCOMPARE(model->columnCount(),mode==2?8:4);
      for(int row=0;row<3;++row)for(int col=0;col<4;++col) {
        const double decoded=85000.+row*4+col;
        if(mode!=1)QCOMPARE(model->data(model->index(row,mode==2?col*2:col)).toString().toDouble(),decoded);
        if(mode!=0)QCOMPARE(model->data(model->index(row,mode==2?col*2+1:col)).toString().toDouble(),std::ldexp(double(row*4+col),20));
      }
      table->selectionModel()->select(QItemSelection(model->index(0,0),model->index(1,model->columnCount()-1)),QItemSelectionModel::ClearAndSelect);
      QString error;const QString selected=table->selectionText(&error);QVERIFY(error.isEmpty());QCOMPARE(selected.count('\n'),1);
      QTRY_VERIFY_WITH_TIMEOUT(([&]{table->copyAction()->trigger();return QApplication::clipboard()->text()==selected;})(),3000);
      const QString path=temporary.filePath(QString("mode-%1.csv").arg(mode));QVERIFY(view.export_csv(path).isEmpty());QVERIFY(!view.export_csv(path).isEmpty());QVERIFY(view.export_csv(path,true).isEmpty());QVERIFY(!view.export_csv(fixture("mini_simple.grb2"),true).isEmpty());
      QFile csv(path);QVERIFY(csv.open(QIODevice::ReadOnly));const auto lines=csv.readAll().split('\n');QVERIFY(lines[0].contains("grib_grid_schema=1"));QVERIFY(lines[1].contains("decode_profile"));
      QCOMPARE(lines[2].split(',').size(),model->columnCount());QCOMPARE(lines[2].split(',')[0].toDouble(),mode==1?0.:85000.);
      const auto evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");if(!evidence.isEmpty()){QVERIFY(view.export_csv(evidence+QString("/grid-mode-%1.csv").arg(mode),true).isEmpty());view.resize(960,600);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));QVERIFY(view.grab().save(evidence+QString("/grid-mode-%1.png").arg(mode)));}
    }
  }
  void gridLayersMissingAndUnsupportedQuantized() {
    const auto file=fixture("mini_cube.grb2"),missing_file=fixture("mini_missing_slice.grb2");
    const auto catalog=GribData::open(file),missing=GribData::open(missing_file);
    GribTableView view(file,catalog->cubes.front().id);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(view.layer_axis_count(),2);
    auto *model=view.findChild<AnalysisTableView*>()->model();view.set_layer_index(0,1);QVERIFY(!view.ready());QCOMPARE(model->rowCount(),0);
    QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(model->data(model->index(0,0)).toString().toDouble(),50600.);
    for(int i=0;i<20;++i)view.set_layer_index(0,i%2);view.set_layer_index(0,0);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(model->data(model->index(0,0)).toString().toDouble(),50000.);
    GribTableView absent(missing_file,missing->cubes.front().id);QTRY_VERIFY_WITH_TIMEOUT(absent.ready(),10000);absent.set_layer_index(0,1);
    QTRY_VERIFY_WITH_TIMEOUT(absent.findChild<QLabel*>("gribGridStatus")->text().contains("MissingSlice"),10000);QVERIFY(!absent.ready());QCOMPARE(absent.findChild<AnalysisTableView*>()->model()->rowCount(),0);QVERIFY(!absent.findChild<QAction*>("gribGridExportCsv")->isEnabled());
    absent.set_layer_index(0,0);QTRY_VERIFY_WITH_TIMEOUT(absent.ready(),10000);
    GribTableView unsupported(fixture("mini_grid_ieee.grb2"),"/field/0",2);QTRY_VERIFY_WITH_TIMEOUT(unsupported.ready(),10000);auto *both=unsupported.findChild<AnalysisTableView*>()->model();QCOMPARE(both->data(both->index(0,0)).toString(),QString("85000"));QCOMPARE(both->data(both->index(0,1)).toString(),QString("unavailable"));
    GribTableView bitmap(fixture("mini_bitmap.grb2"),"/field/0");QTRY_VERIFY_WITH_TIMEOUT(bitmap.ready(),10000);QCOMPARE(bitmap.findChild<AnalysisTableView*>()->model()->data(bitmap.findChild<AnalysisTableView*>()->model()->index(0,3)).toString(),QString("nan"));
  }
  void gridTreeNavigationAndClose() {
    auto *window=new MainWindow;window->resize(1200,800);window->show();QCOMPARE(window->read_file(fixture("mini_cube.grb2")),0);
    auto *tree=window->findChild<FileTreeWidget*>();auto *root=tree->topLevelItem(0);auto *cube=branch(root,"Variables")->child(0);tree->setCurrentItem(cube);
    QVERIFY(QMetaObject::invokeMethod(tree,"add_grid",Qt::DirectConnection));auto views=window->findChildren<GribTableView*>();QCOMPARE(views.size(),1);
    auto *view=views[0];QTRY_VERIFY_WITH_TIMEOUT(view->ready(),10000);QTRY_COMPARE(window->current_layer_view(),static_cast<LayerNavigation*>(view));window->step_layer(LayerStep::Next);QCOMPARE(view->layer_index(0),1);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),10000);
    view->set_layer_index(0,0);window->close_file_of(root);QCOMPARE(tree->topLevelItemCount(),0);QTRY_VERIFY_WITH_TIMEOUT(!DataReadController::any_busy(),10000);
    QCOMPARE(window->read_file(fixture("mini_cube.grb2")),0);tree->setCurrentItem(branch(tree->topLevelItem(0),"Variables")->child(0));QVERIFY(QMetaObject::invokeMethod(tree,"add_grid",Qt::DirectConnection));delete window;QTRY_VERIFY_WITH_TIMEOUT(!DataReadController::any_busy(),10000);
  }
  void gridSourceChangeAndInvalidObject() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const auto source=temporary.filePath("source.grb2");QVERIFY(QFile::copy(fixture("mini_simple.grb2"),source));
    auto release=qScopeGuard([&]{GribData::release(source);});
    GribTableView view(source,"/field/0");QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);
    QVERIFY(view.findChild<QLabel*>("gribGridStatus")->text().contains("Quantized X not requested"));
    {QFile changed(source);QVERIFY(changed.open(QIODevice::Append));QCOMPARE(changed.write("changed"),qint64(7));}
    const auto output=temporary.filePath("changed.csv");QVERIFY(view.export_csv(output).contains("SourceChanged"));QVERIFY(!QFile::exists(output));
    GribTableView stale(source,"/field/0");QVERIFY(!stale.ready());QVERIFY(stale.findChild<QLabel*>("gribGridStatus")->text().contains("SourceChanged"));
    GribTableView invalid(fixture("mini_simple.grb2"),"/field/9999");QVERIFY(!invalid.ready());QCOMPARE(invalid.findChild<AnalysisTableView*>()->model()->rowCount(),0);QVERIFY(!invalid.findChild<QAction*>("gribGridExportCsv")->isEnabled());
  }
  void gridLargeAsyncModelUsesBoundedAccessibility() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const auto source=temporary.filePath("large.grb2");
    auto *handle=codes_grib_handle_new_from_samples(nullptr,"regular_ll_sfc_grib2");QVERIFY(handle);auto cleanup=qScopeGuard([&]{codes_handle_delete(handle);GribData::release(source);});
    QCOMPARE(codes_set_long(handle,"Ni",1001),0);QCOMPARE(codes_set_long(handle,"Nj",1000),0);
    std::vector<double> values(1001000,7.);QCOMPARE(codes_set_double_array(handle,"values",values.data(),values.size()),0);
    const void *message=nullptr;size_t length=0;QCOMPARE(codes_get_message(handle,&message,&length),0);
    {QFile output(source);QVERIFY(output.open(QIODevice::WriteOnly));QCOMPARE(output.write(static_cast<const char*>(message),qint64(length)),qint64(length));}
    GribTableView view(source,"/field/0");view.resize(960,600);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);
    auto *table=view.findChild<AnalysisTableView*>();QVERIFY(table->property("boundedGridAccessibility").toBool());
    QCOMPARE(table->model()->rowCount(),1000);QCOMPARE(table->model()->columnCount(),1001);
    QAccessibleInterface *accessible=QAccessible::queryAccessibleInterface(table);QVERIFY(accessible);QVERIFY(!accessible->tableInterface());
    const auto model=table->model();const auto index=model->index(1,1);QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(index).center());
    QVERIFY(table->selectionModel()->hasSelection());QVERIFY(table->copyAction()->isEnabled());QString error;QCOMPARE(table->selectionText(&error),QString("7"));QVERIFY(error.isEmpty());
  }
  void gridExternalGrib1Sample() {
    const auto source=qEnvironmentVariable("HDFSTUDIO_GRIB1_SAMPLE"),cases=qEnvironmentVariable("HDFSTUDIO_GRIB1_CASES"),evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");
    if(source.isEmpty()||cases.isEmpty())QSKIP("Optional read-only GRIB1 business sample; configure sample and generated reference cases");
    QVERIFY(QFileInfo(source).isFile());QVERIFY(!evidence.isEmpty());QFile input(cases);QVERIFY(input.open(QIODevice::ReadOnly));const auto reference=QJsonDocument::fromJson(input.readAll()).object();
    auto release=qScopeGuard([&]{GribData::release(source);});
    MainWindow window;window.resize(1250,850);window.show();QCOMPARE(window.read_file(source),0);
    auto *tree=window.findChild<FileTreeWidget*>();QCOMPARE(tree->topLevelItemCount(),1);auto *root=tree->topLevelItem(0);QVERIFY(root->text(0).contains("GRIB1"));
    const auto catalog=GribData::open(source);QCOMPARE(int(catalog->fields.size()),reference["field_count"].toInt());QCOMPARE(int(catalog->cubes.size()),reference["cube_count"].toInt());
    auto *pages=branch(root,"Messages [physical");QVERIFY(pages);QCOMPARE(pages->childCount(),(int(catalog->fields.size())+255)/256);
    auto *last_page=pages->child(pages->childCount()-1);last_page->setExpanded(true);QCOMPARE(last_page->childCount(),int(catalog->fields.size())%256);
    for(const auto &entry:reference["fields"].toArray()) {
      const auto item=entry.toObject();const auto target=item["target"].toString();GribTableView grid(source,target);
      QTRY_VERIFY_WITH_TIMEOUT(grid.ready(),15000);auto *model=grid.findChild<AnalysisTableView*>()->model();QCOMPARE(model->rowCount(),item["rows"].toInt());QCOMPARE(model->columnCount(),item["cols"].toInt());
      for(const auto &probe:item["probes"].toArray()) {const auto point=probe.toObject();const int position=point["position"].toInt();QCOMPARE(model->data(model->index(position/model->columnCount(),position%model->columnCount())).toString().toDouble(),point["value"].toDouble());}
      const auto properties=GribData::properties(source,target);QVERIFY(properties.contains("offset: "+item["offset"].toString()));QVERIFY(properties.contains("length: "+item["length"].toString()));
      QVERIFY2(grid.export_csv(evidence+"/external-"+item["tag"].toString()+".csv",true).isEmpty(),qPrintable(target));
    }
    const auto navigation=reference["navigation"].toObject();GribTableView cube(source,navigation["target"].toString());QTRY_VERIFY_WITH_TIMEOUT(cube.ready(),15000);QCOMPARE(cube.layer_axis_count(),1);
    cube.set_layer_index(0,cube.layer_count(0)-1);QVERIFY(!cube.ready());QTRY_VERIFY_WITH_TIMEOUT(cube.ready(),15000);QVERIFY(cube.export_csv(evidence+"/external-cube-last.csv",true).isEmpty());
    for(int i=0;i<12;++i)cube.set_layer_index(0,i%2);cube.set_layer_index(0,0);QTRY_VERIFY_WITH_TIMEOUT(cube.ready(),15000);QVERIFY(cube.export_csv(evidence+"/external-cube-first.csv",true).isEmpty());
    cube.resize(1050,650);cube.show();QVERIFY(QTest::qWaitForWindowExposed(&cube));QVERIFY(cube.grab().save(evidence+"/external-grib1-grid.png"));
    window.close_file_of(root);QCOMPARE(tree->topLevelItemCount(),0);QVERIFY(!DataReadController::any_busy());
  }
  void imageDecodedMaskPaletteProbeAndPng() {
    const auto source=fixture("mini_simple.grb2");RasterView view(source,"/field/0",GribData::describe(source,"/field/0"));view.resize(1100,750);view.show();
    QTRY_VERIFY_WITH_TIMEOUT(view.plane_snapshot().values!=nullptr,10000);QTRY_VERIFY(!view.render_pending());
    auto plane=view.plane_snapshot();QCOMPARE(plane.rows,size_t(3));QCOMPARE(plane.cols,size_t(4));QCOMPARE(plane.units,QString("K"));QCOMPARE(view.property("reader").toString(),QString("ecCodes"));
    for(size_t i=0;i<12;++i)QCOMPARE(plane.values->at(i),85000.+i);
    const auto count=view.property("planeReadCount").toULongLong();auto image=view.image_for_export();QCOMPARE(image.size(),QSize(4,3));
    const auto colors=view.color_state();for(int row=0;row<3;++row)for(int col=0;col<4;++col)QCOMPARE(image.pixel(col,row),HdfColorMap::map_value(colors.palette,colors.mapping,85000.+row*4+col,false));
    QVERIFY(QTest::qWaitForWindowExposed(&view));auto *canvas=view.findChild<QGraphicsView*>();QVERIFY(canvas);const auto position=canvas->mapFromScene(QPointF(.5,.5));
    QMouseEvent probe(QEvent::MouseMove,QPointF(position),QPointF(canvas->viewport()->mapToGlobal(position)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas->viewport(),&probe);
    QTRY_VERIFY(canvas->viewport()->toolTip().contains("85000"));QVERIFY(canvas->viewport()->toolTip().contains("[0, 0]"));
    auto *start=view.findChild<QLineEdit*>("maskStart"),*end=view.findChild<QLineEdit*>("maskEnd");start->setText("85000");end->setText("85000");QVERIFY(QMetaObject::invokeMethod(end,"editingFinished"));
    image=view.image_for_export();QCOMPARE(image.pixelColor(0,0),QColor(Qt::white));QVERIFY(std::isnan(view.plane_snapshot().values->at(0)));QCOMPARE(plane.values->at(0),85000.);
    QCOMPARE(view.property("planeReadCount").toULongLong(),count);QVERIFY(!view.findChild<QAction*>("projsAction")->isEnabled());
    auto *palette=view.findChild<QComboBox*>("colorMapSelector");palette->setCurrentIndex((palette->currentIndex()+1)%palette->count());view.findChild<QCheckBox*>("reverseColorMap")->setChecked(true);QCOMPARE(view.property("planeReadCount").toULongLong(),count);
    const auto original=view.image_for_export();view.findChild<QAction*>("flipHorizontalAction")->trigger();view.findChild<QAction*>("flipVerticalAction")->trigger();QCOMPARE(view.image_for_export(),original.mirrored(true,true));
    const auto evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");if(!evidence.isEmpty()){QVERIFY(view.image_for_export().save(evidence+"/image-simple.png"));QVERIFY(view.grab().save(evidence+"/image-simple-ui.png"));}
    const auto bitmap_file=fixture("mini_bitmap.grb2");RasterView bitmap(bitmap_file,"/field/0",GribData::describe(bitmap_file,"/field/0"));const auto bitmap_image=bitmap.image_for_export();QVERIFY(!bitmap_image.isNull());QCOMPARE(bitmap_image.pixelColor(3,0),QColor(110,110,110));
  }
  void imageLayersLatestMissingAndSnapshotLease() {
    const auto source=fixture("mini_missing_slice.grb2");const auto catalog=GribData::open(source);const auto target=catalog->cubes.front().id;
    auto *view=new RasterView(source,target,GribData::describe(*catalog,target));QVERIFY(!view->image_for_export().isNull());auto old=view->plane_snapshot();std::weak_ptr<void> lease=old.storage_lease;QVERIFY(!lease.expired());
    view->set_layer_index(0,1);QVERIFY(!view->plane_snapshot().values);QVERIFY(view->render_pending());
    QTRY_VERIFY_WITH_TIMEOUT(!view->render_pending(),10000);QVERIFY(!view->plane_snapshot().values);QVERIFY(view->findChild<QLabel*>("rasterLegend")->text().contains("MissingSlice"));QVERIFY(view->image_for_export().isNull());
    view->set_layer_index(0,0);QTRY_VERIFY_WITH_TIMEOUT(view->plane_snapshot().values!=nullptr,10000);
    view->set_layer_index(1,1);for(int i=0;i<20;++i)view->set_layer_index(0,i%2);view->set_layer_index(0,0);QTRY_VERIFY_WITH_TIMEOUT(!view->render_pending(),10000);QVERIFY(view->plane_snapshot().values);QCOMPARE(view->plane_snapshot().values->at(0),85000.);
    delete view;QVERIFY(!lease.expired());old={};QTRY_VERIFY(lease.expired());QTRY_VERIFY_WITH_TIMEOUT(!DataReadController::any_busy(),10000);
  }
  void imageHoldsPresentedFrameDuringPendingRead() {
    const auto source=fixture("mini_cube.grb2");const auto catalog=GribData::open(source);const auto target=catalog->cubes.front().id;
    RasterView view(source,target,GribData::describe(*catalog,target));QVERIFY(!view.image_for_export().isNull());
    auto *canvas=view.findChild<QGraphicsView*>();QVERIFY(canvas);QGraphicsPixmapItem *image=nullptr;
    for(auto *item:canvas->scene()->items())if(item->type()==QGraphicsPixmapItem::Type)image=static_cast<QGraphicsPixmapItem*>(item);
    QVERIFY(image);const auto before=image->pixmap().toImage();const auto before_revision=view.property("renderedRevision");
    auto *bar=view.findChild<ColorBar*>();bar->resize(700,94);const auto before_bar=bar->grab().toImage();
    auto *scheduler=ReadScheduler::instance();QVERIFY(scheduler->acquire());QVERIFY(scheduler->acquire());
    auto release=qScopeGuard([&]{scheduler->release();scheduler->release();});
    view.set_layer_index(0,1);QVERIFY(view.render_pending());QVERIFY(!view.plane_snapshot().values);
    QCOMPARE(image->pixmap().toImage(),before);QCOMPARE(view.property("renderedRevision"),before_revision);
    QCOMPARE(bar->grab().toImage(),before_bar);QVERIFY(view.findChild<QLabel*>("rasterLegend")->text().contains("previous image"));
    QVERIFY(!view.property("minimum").isValid());QVERIFY(!view.findChild<QAction*>("showProfileAction")->isEnabled());
    for(int index=0;index<30;++index)view.set_layer_index(0,index%2);
    view.set_layer_index(0,1);QCOMPARE(image->pixmap().toImage(),before);QVERIFY(!view.plane_snapshot().values);
    release.dismiss();scheduler->release();scheduler->release();
    QTRY_VERIFY_WITH_TIMEOUT(!view.render_pending(),10000);QVERIFY(view.plane_snapshot().values);
    const auto expected=GribData::read(*catalog,target,{1,0},false,false);QVERIFY(expected.error.isEmpty());
    QCOMPARE(*view.plane_snapshot().values,expected.values);QVERIFY(view.property("renderedRevision")!=before_revision);
    QVERIFY(!view.findChild<QLabel*>("rasterLegend")->text().contains("previous image"));
    view.set_sync_out_of_range(true,"test range");QVERIFY(image->pixmap().isNull());QVERIFY(!view.plane_snapshot().values);
    view.set_sync_out_of_range(false,{});QTRY_VERIFY(view.plane_snapshot().values);QVERIFY(!view.image_for_export().isNull());
  }
  void imageCfsPlaybackFramesMatchDecodedValues() {
    const auto source=qEnvironmentVariable("HDFSTUDIO_GRIB_CFS_SAMPLE");
    if(source.isEmpty())QSKIP("Optional CFS business sample");
    const auto catalog=GribData::open(source);QVERIFY(!catalog->cubes.empty());const auto target=catalog->cubes.front().id;
    RasterView view(source,target,GribData::describe(*catalog,target));QVERIFY(!view.image_for_export().isNull());
    QCOMPARE(view.layer_count(0),480);QCOMPARE(view.plane_snapshot().rows,size_t(190));QCOMPARE(view.plane_snapshot().cols,size_t(384));
    for(int frame:{1,2,17,479,0}) {
      view.set_layer_index(0,frame);QVERIFY(!view.plane_snapshot().values);
      QTRY_VERIFY_WITH_TIMEOUT(!view.render_pending(),10000);const auto snapshot=view.plane_snapshot();QVERIFY(snapshot.values);
      const auto expected=GribData::read(*catalog,target,{size_t(frame)},false,false);QVERIFY(expected.error.isEmpty());QCOMPARE(*snapshot.values,expected.values);
      const auto image=view.image_for_export();const auto colors=view.color_state();
      for(int row=0;row<image.height();++row)for(int col=0;col<image.width();++col)
        QCOMPARE(image.pixel(col,row),HdfColorMap::map_value(colors.palette,colors.mapping,expected.values[size_t(row)*expected.cols+size_t(col)],false));
    }
  }
  void imageTreeCloseAndSourceChange() {
    auto *window=new MainWindow;QCOMPARE(window->read_file(fixture("mini_cube.grb2")),0);auto *tree=window->findChild<FileTreeWidget*>();auto *root=tree->topLevelItem(0);tree->setCurrentItem(branch(root,"Variables")->child(0));
    window->findChild<QAction*>("actionImageView")->trigger();auto *view=window->findChild<RasterView*>();QVERIFY(view);QVERIFY(!view->image_for_export().isNull());
    QCOMPARE(window->current_layer_view(),static_cast<LayerNavigation*>(view));window->step_layer(LayerStep::Next);window->close_file_of(root);QCOMPARE(tree->topLevelItemCount(),0);QTRY_VERIFY_WITH_TIMEOUT(!DataReadController::any_busy(),10000);
    QCOMPARE(window->read_file(fixture("mini_cube.grb2")),0);tree->setCurrentItem(branch(tree->topLevelItem(0),"Variables")->child(0));window->findChild<QAction*>("actionImageView")->trigger();delete window;QTRY_VERIFY_WITH_TIMEOUT(!DataReadController::any_busy(),10000);
    QTemporaryDir temporary;const auto file=temporary.filePath("changed.grb2");QVERIFY(QFile::copy(fixture("mini_simple.grb2"),file));auto release=qScopeGuard([&]{GribData::release(file);});RasterView changed(file,"/field/0",GribData::describe(file,"/field/0"));QVERIFY(!changed.image_for_export().isNull());
    {QFile output(file);QVERIFY(output.open(QIODevice::Append));QCOMPARE(output.write("changed"),qint64(7));}QVERIFY(changed.image_for_export().isNull());QVERIFY(!changed.plane_snapshot().values);QVERIFY(changed.findChild<QLabel*>("rasterLegend")->text().contains("SourceChanged"));
  }
  void imageExternalGrib1Sample() {
    const auto source=qEnvironmentVariable("HDFSTUDIO_GRIB1_SAMPLE"),cases=qEnvironmentVariable("HDFSTUDIO_GRIB1_CASES"),evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");
    if(source.isEmpty()||cases.isEmpty())QSKIP("Optional external GRIB1 image case");
    QFile input(cases);QVERIFY(input.open(QIODevice::ReadOnly));const auto reference=QJsonDocument::fromJson(input.readAll()).object();const auto catalog=GribData::open(source);QVERIFY(!evidence.isEmpty());
    for(const auto &entry:reference["fields"].toArray()) {
      const auto item=entry.toObject();const auto target=item["target"].toString(),tag=item["tag"].toString();RasterView view(source,target,GribData::describe(*catalog,target));
      const auto image=view.image_for_export();QCOMPARE(image.size(),QSize(item["cols"].toInt(),item["rows"].toInt()));const auto plane=view.plane_snapshot();QVERIFY(plane.values);
      const auto colors=view.color_state();for(int row=0;row<image.height();++row)for(int col=0;col<image.width();++col)QCOMPARE(image.pixel(col,row),HdfColorMap::map_value(colors.palette,colors.mapping,plane.values->at(size_t(row)*plane.cols+size_t(col)),false));
      for(const auto &probe:item["probes"].toArray()){const auto point=probe.toObject();QCOMPARE(plane.values->at(size_t(point["position"].toInt())),point["value"].toDouble());}
      QVERIFY(image.save(evidence+"/raster-"+tag+".png"));QFile raw(evidence+"/raster-"+tag+".f64");QVERIFY(raw.open(QIODevice::WriteOnly));QCOMPARE(raw.write(reinterpret_cast<const char*>(plane.values->data()),qint64(plane.values->size()*sizeof(double))),qint64(plane.values->size()*sizeof(double)));
      if(tag=="field-0"){view.resize(1100,750);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));QVERIFY(view.grab().save(evidence+"/raster-grib1-ui.png"));}
    }
  }
  void gridImageExternalGrib2Sample() {
    const auto source=qEnvironmentVariable("HDFSTUDIO_GRIB2_SAMPLE"),cases=qEnvironmentVariable("HDFSTUDIO_GRIB2_CASES"),evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");
    if(source.isEmpty()||cases.isEmpty())QSKIP("Optional read-only GRIB2 complex-packing business sample");
    QFile input(cases);QVERIFY(input.open(QIODevice::ReadOnly));const auto reference=QJsonDocument::fromJson(input.readAll()).object();QVERIFY(!evidence.isEmpty());
    MainWindow window;QCOMPARE(window.read_file(source),0);auto *tree=window.findChild<FileTreeWidget*>();auto *root=tree->topLevelItem(0);QVERIFY(root->text(0).contains("GRIB2"));
    const auto catalog=GribData::open(source);QCOMPARE(int(catalog->fields.size()),reference["field_count"].toInt());
    QCOMPARE(branch(root,"Variables")->childCount(),int(catalog->cubes.size()));auto *page=branch(root,"Messages")->child(0);page->setExpanded(true);QCOMPARE(page->childCount(),reference["field_count"].toInt());
    int missing_fields=0,unknown_fields=0;
    for(const auto &entry:reference["fields"].toArray()) {
      const auto item=entry.toObject();const auto target=item["target"].toString(),tag=item["tag"].toString();
      QFile expected_file(reference["reference_directory"].toString()+"/"+tag+".f64");QVERIFY(expected_file.open(QIODevice::ReadOnly));const auto expected=expected_file.readAll();
      GribTableView grid(source,target);QTRY_VERIFY_WITH_TIMEOUT(grid.ready(),15000);auto *model=grid.findChild<AnalysisTableView*>()->model();QCOMPARE(model->rowCount(),item["rows"].toInt());QCOMPARE(model->columnCount(),item["cols"].toInt());
      QVERIFY(grid.export_csv(evidence+"/grib2-"+tag+".csv",true).isEmpty());
      const auto info=GribData::describe(*catalog,target);RasterView view(source,target,info);const auto image=view.image_for_export();QCOMPARE(image.size(),QSize(item["cols"].toInt(),item["rows"].toInt()));const auto plane=view.plane_snapshot();QVERIFY(plane.values);QCOMPARE(plane.units,info.units);
      QCOMPARE(expected.size(),qsizetype(plane.values->size()*sizeof(double)));size_t missing=0;
      const auto colors=view.color_state();
      for(size_t position=0;position<plane.values->size();++position) {
        double truth=0;std::memcpy(&truth,expected.constData()+position*sizeof(double),sizeof(double));const auto value=plane.values->at(position);
        if(std::isnan(truth)){QVERIFY(std::isnan(value));++missing;}else QCOMPARE(value,truth);
        const int row=int(position/plane.cols),column=int(position%plane.cols);
        QCOMPARE(image.pixel(column,row),HdfColorMap::map_value(colors.palette,colors.mapping,value,false));
      }
      QCOMPARE(missing,size_t(item["missing_count"].toInt()));if(missing)++missing_fields;if(info.units=="unknown")++unknown_fields;
      for(const auto &probe:item["probes"].toArray()) {
        const auto point=probe.toObject();const int position=point["position"].toInt();const auto text=model->data(model->index(position/int(plane.cols),position%int(plane.cols))).toString();
        if(point["value"].isNull())QCOMPARE(text,QString("nan"));else QCOMPARE(text.toDouble(),point["value"].toDouble());
      }
      QFile raw(evidence+"/grib2-raster-"+tag+".f64");QVERIFY(raw.open(QIODevice::WriteOnly));QCOMPARE(raw.write(reinterpret_cast<const char*>(plane.values->data()),qint64(plane.values->size()*sizeof(double))),qint64(plane.values->size()*sizeof(double)));
      if(reference["png_fields"].toArray().contains(QJsonValue(target.mid(7).toInt())))QVERIFY(image.save(evidence+"/grib2-raster-"+tag+".png"));
    }
    QVERIFY(missing_fields>0);QVERIFY(unknown_fields>0);
    GribTableView both(source,"/field/0",2);QTRY_VERIFY_WITH_TIMEOUT(both.ready(),10000);auto *both_model=both.findChild<AnalysisTableView*>()->model();QCOMPARE(both_model->data(both_model->index(0,1)).toString(),QString("unavailable"));
    tree->setCurrentItem(page->child(0)->child(0));window.findChild<QAction*>("actionImageView")->trigger();auto *image_view=window.findChild<RasterView*>();QVERIFY(image_view);QVERIFY(!image_view->image_for_export().isNull());
    window.close_file_of(root);QCOMPARE(tree->topLevelItemCount(),0);QTRY_VERIFY_WITH_TIMEOUT(!DataReadController::any_busy(),10000);
  }
};
QTEST_MAIN(GribBrowserTest)
#include "grib_browser_test.moc"
