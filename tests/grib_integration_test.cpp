// Native GRIB integration invariants; fixtures contain analytically known tiny arrays.
#include <QtTest>
#include "grib/grib_data.hpp"
#include "data_source.hpp"
#include <QTemporaryDir>
#include <QJsonDocument>
#include <cmath>

class GribIntegrationTest:public QObject {
  Q_OBJECT
  QString fixture(const QString& name)const{return QString(HDFSTUDIO_TEST_SOURCE_DIR)+"/testdata/grib/"+name;}
private slots:
  void formatsAndInvalidInputs() {
    for(const auto& name:{"mini_simple.grb2","mini_grib1.grb","mini_mixed.grib","mini_wrapped.bin"}) {
      QVERIFY(GribData::is_grib(fixture(name)));QVERIFY(!GribData::open(fixture(name))->fields.empty());
    }
    QVERIFY(!GribData::is_grib(fixture("not_grib.bin")));
    QVERIFY_EXCEPTION_THROWN(GribData::open(fixture("mini_truncated.grb2")),grib::Error);
    QVERIFY_EXCEPTION_THROWN(GribData::open(fixture("mini_trailing_corrupt.bin")),grib::Error);
  }
  void cubeIdentityAndMissing() {
    const auto cat=GribData::open(fixture("mini_cube.grb2"));
    QCOMPARE(cat->cubes.size(),size_t(1));const auto target=cat->cubes.front().id;
    const auto info=GribData::describe(*cat,target);
    QVERIFY(info.can_image);QCOMPARE(info.shape,std::vector<size_t>({2,2,3,4}));
    auto plane=DataSource::read_plane(cat->path,target,{1,0});QVERIFY2(plane.error.isEmpty(),qPrintable(plane.error));QCOMPARE(plane.values[0],50600.0);
    const auto missing=GribData::open(fixture("mini_missing_slice.grb2"));
    plane=GribData::read(*missing,missing->cubes.front().id,{1,0});QCOMPARE(plane.error_code,QString("MissingSlice"));QVERIFY(plane.values.empty());
    const auto duplicate=GribData::open(fixture("mini_duplicates.grb2"));QVERIFY(duplicate->cubes.front().axes.front().name.startsWith("message"));
    const auto multi=GribData::open(fixture("mini_multifield.grb2"));QCOMPARE(multi->fields.size(),size_t(2));
    QCOMPARE(multi->fields[0].offset,multi->fields[1].offset);QCOMPARE(multi->fields[1].sub_field,1);
    const auto json=grib::catalog_json(*multi);QVERIFY(QJsonDocument(json).toJson().contains("\"offset\": \"0\""));
  }
  void packing_data() {
    QTest::addColumn<QString>("name");
    for(const auto& name:{"mini_simple.grb2","mini_grid_jpeg.grb2","mini_grid_png.grb2","mini_grid_ccsds.grb2","mini_grid_complex.grb2","mini_grid_complex_spatial_differencing.grb2","mini_grid_ieee.grb2"})QTest::newRow(name)<<QString(name);
  }
  void packing() {
    QFETCH(QString,name);auto plane=GribData::read(fixture(name),"/field/0",{},true,true);
    QVERIFY2(plane.error.isEmpty(),qPrintable(plane.error));
    const auto cat=GribData::open(fixture(name));QCOMPARE(plane.rows,size_t(cat->fields[0].nj));QCOMPARE(plane.cols,size_t(cat->fields[0].ni));
    for(size_t index=0;index<plane.values.size();++index)QCOMPARE(plane.values[index],85000.0+double(index));
    QVERIFY(plane.lease);QCOMPARE(plane.coordinates.latitudes.size(),plane.values.size());
    if(name.contains("complex")||name.contains("ieee"))QCOMPARE(plane.stored_semantics,DataSource::StoredSemantics::Unavailable);
    else QCOMPARE(plane.stored_semantics,DataSource::StoredSemantics::ReconstructedQuantized);
  }
  void scanOrderAndBitmap() {
    for(int mode:{0,64,128,16,32,48}) {
      const auto cat=GribData::open(fixture(QString("mini_scan_%1.grb2").arg(mode)));const auto plane=grib::read_plane(*cat,0);
      for(size_t index=0;index<12;++index)QCOMPARE(plane.values[index],85000.0+double(plane.source_indices[index]));
      GribData::release(cat->path);
    }
    const auto plane=GribData::read(fixture("mini_bitmap.grb2"),"/field/0",{});QVERIFY(std::isnan(plane.values.at(3)));
  }
  void everyOrderedSectionPair() {
    const auto file=fixture("mini_cube.grb2");const auto cat=GribData::open(file);const auto target=cat->cubes.front().id;
    const auto info=GribData::describe(*cat,target);
    for(size_t ra=0;ra<4;++ra)for(size_t ca=0;ca<4;++ca)if(ra!=ca) {
      DataSource::ReadSelection spec;spec.row_axis=ra;spec.col_axis=ca;spec.fixed_indices={1,1,1,1};
      auto slice=GribData::slice(file,target,spec);QVERIFY2(slice.error.isEmpty(),qPrintable(slice.error));
      for(size_t row=0;row<slice.rows;++row)for(size_t col=0;col<slice.cols;++col) {
        auto pos=spec.fixed_indices;pos[ra]=row;pos[ca]=col;const auto plane=GribData::read(*cat,target,{pos[0],pos[1]});
        QCOMPARE(slice.values[row*slice.cols+col],plane.values[pos[2]*4+pos[3]]);
      }
    }
  }
  void unicodeAndSourceChanges() {
    QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath(QString::fromUtf8("数据_🌧.grb2"));
    QVERIFY(QFile::copy(fixture("mini_simple.grb2"),path));auto cat=GribData::open(path);QVERIFY(GribData::read(*cat,"/field/0",{}).error.isEmpty());
    QFile file(path);QVERIFY(file.open(QIODevice::Append));file.write("x");file.close();
    QCOMPARE(GribData::read(*cat,"/field/0",{}).error_code,QString("SourceChanged"));GribData::release(path);
  }
  void cleanup(){for(const auto& file:QDir(QString(HDFSTUDIO_TEST_SOURCE_DIR)+"/testdata/grib").entryList(QDir::Files))GribData::release(fixture(file));}
};
QTEST_GUILESS_MAIN(GribIntegrationTest)
#include "grib_integration_test.moc"
