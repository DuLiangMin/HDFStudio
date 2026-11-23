// Small GRIB GUI-controller gates. A subsequent feature is added only after both configurations pass.
#include <QtTest>
#include "data_read_controller.hpp"
#include "read_scheduler.hpp"
#include "slice_job_controller.hpp"
#include "raster_budget.hpp"
#include <QScopeGuard>
class GribGuiTest:public QObject {
  Q_OBJECT
  QString fixture(const QString& name)const{return QString(HDFSTUDIO_TEST_SOURCE_DIR)+"/testdata/grib/"+name;}
private slots:
  void schedulerBoundsAndQueuedCancellation(){
    auto* scheduler=ReadScheduler::instance();QVERIFY(scheduler->acquire());QVERIFY(scheduler->acquire());QVERIFY(!scheduler->acquire());
    auto* reader=new DataReadController;int completions=0;
    connect(reader,&DataReadController::indexed,this,[&]{++completions;});
    reader->index(fixture("mini_cube.grb2"));QVERIFY(reader->busy());reader->cancel();QVERIFY(!reader->busy());
    scheduler->release();scheduler->release();QCoreApplication::processEvents();QCOMPARE(completions,0);reader->dispose();
  }
  void latestReadAndMissingInvalidation(){
    const auto file=fixture("mini_cube.grb2");const auto cat=GribData::open(file);auto* reader=new DataReadController;
    int completions=0,failures=0;double first=0;std::shared_ptr<void> retained;
    connect(reader,&DataReadController::completed,this,[&](auto plane,auto lease){++completions;first=plane->values.front();retained=lease;});
    connect(reader,&DataReadController::failed,this,[&]{++failures;});
    reader->read(file,cat->cubes.front().id,{0,0});reader->read(file,cat->cubes.front().id,{1,0});
    QTRY_COMPARE_WITH_TIMEOUT(completions,1,10000);QCOMPARE(first,50600.0);QVERIFY(retained);QCOMPARE(failures,0);
    const auto missing=GribData::open(fixture("mini_missing_slice.grb2"));reader->read(missing->path,missing->cubes.front().id,{1,0});
    QTRY_COMPARE_WITH_TIMEOUT(failures,1,10000);QCOMPARE(completions,1);QTRY_VERIFY(!reader->busy());reader->dispose();
  }
  void disposeDuringIndex(){
    auto* reader=new DataReadController;QPointer<DataReadController> guard=reader;int completions=0;
    connect(reader,&DataReadController::indexed,this,[&]{++completions;});reader->index(fixture("mini_showcase.grb2"));reader->dispose();
    QTRY_VERIFY_WITH_TIMEOUT(!guard,10000);QCOMPARE(completions,0);QVERIFY(!DataReadController::any_busy());
  }
  void mixedControllersShareAdmission(){
    auto* scheduler=ReadScheduler::instance();QVERIFY(scheduler->acquire());QVERIFY(scheduler->acquire());
    auto* reader=new DataReadController;auto* section=new SliceJobController;
    int indexed=0,failed=0;
    connect(reader,&DataReadController::indexed,this,[&]{++indexed;});
    connect(section,&SliceJobController::failed,this,[&](const QString& error){if(error.startsWith("MemoryBudgetExceeded"))++failed;});
    section->submit({{"operation","inspect"}},quint64(16385)*1024*1024);
    reader->index(fixture("mini_simple.grb2"));
    QCoreApplication::processEvents();QCOMPARE(indexed,0);QCOMPARE(failed,0);QVERIFY(reader->busy());QVERIFY(section->busy());
    scheduler->release();scheduler->release();
    QTRY_COMPARE_WITH_TIMEOUT(failed,1,10000);QTRY_COMPARE_WITH_TIMEOUT(indexed,1,10000);
    QTRY_VERIFY(!reader->busy()&&!section->busy());
    QVERIFY(scheduler->acquire());QVERIFY(scheduler->acquire());QVERIFY(!scheduler->acquire());scheduler->release();scheduler->release();
    reader->dispose();section->dispose();
  }
  void retainedPlaneKeepsBudgetUntilReleased(){
    const auto previous=qgetenv("HDFSTUDIO_RASTER_BUDGET_MIB");
    const auto restore=qScopeGuard([previous]{if(previous.isNull())qunsetenv("HDFSTUDIO_RASTER_BUDGET_MIB");else qputenv("HDFSTUDIO_RASTER_BUDGET_MIB",previous);});
    qputenv("HDFSTUDIO_RASTER_BUDGET_MIB","1");
    auto* reader=new DataReadController;QPointer<DataReadController> guard=reader;
    std::shared_ptr<DataSource::PlaneData> retained_plane;std::shared_ptr<void> retained_budget;QString error;
    connect(reader,&DataReadController::completed,this,[&](auto plane,auto lease){retained_plane=plane;retained_budget=lease;});
    connect(reader,&DataReadController::failed,this,[&](const QString& message){error=message;});
    reader->read(fixture("mini_simple.grb2"),"/field/0",{});
    QTRY_VERIFY_WITH_TIMEOUT(retained_plane||!error.isEmpty(),10000);QVERIFY2(error.isEmpty(),qPrintable(error));
    reader->dispose();QTRY_VERIFY(!guard);QVERIFY(retained_budget);QCOMPARE(retained_plane->values.front(),85000.);
    QVERIFY(!reserve_raster_bytes(1024*1024));retained_plane.reset();retained_budget.reset();
    QVERIFY(reserve_raster_bytes(1024*1024));
  }
  void invalidRequestReleasesAdmission(){
    auto* reader=new DataReadController;int failed=0,indexed=0;
    connect(reader,&DataReadController::failed,this,[&](const QString& error){if(error.startsWith("InvalidSelection"))++failed;});
    connect(reader,&DataReadController::indexed,this,[&]{++indexed;});
    reader->read(fixture("mini_simple.grb2"),"/field/0",{size_t(INT_MAX)+1});
    QTRY_COMPARE_WITH_TIMEOUT(failed,1,10000);QTRY_VERIFY(!reader->busy());
    reader->index(fixture("mini_simple.grb2"));QTRY_COMPARE_WITH_TIMEOUT(indexed,1,10000);reader->dispose();
    auto* scheduler=ReadScheduler::instance();QVERIFY(scheduler->acquire());QVERIFY(scheduler->acquire());scheduler->release();scheduler->release();
  }
};
QTEST_GUILESS_MAIN(GribGuiTest)
#include "grib_gui_test.moc"
