// GRIB arbitrary-axis CLI/worker contract; all generated outputs stay in temporary directories.
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>
#include "slice_worker.hpp"
#include "grib/grib_data.hpp"

namespace {
QString fixture(const QString &name) {return QString(HDFSTUDIO_TEST_SOURCE_DIR)+"/testdata/grib/"+name;}
QString cube(const QString &file) {return GribData::open(file)->cubes.front().id;}
bool write_json(const QString &path,const QJsonObject &object) {
  QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;
  const auto bytes=QJsonDocument(object).toJson();return file.write(bytes)==bytes.size();
}
QJsonObject read_json(const QString &path) {QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(file.readAll()).object();}
QJsonObject request(const QString &file,const QString &target,const SliceInfo &info,const NcSliceSpec &spec) {
  const auto metadata=slice_metadata(file,target,info,spec);QJsonArray axes;
  for(size_t axis=0;axis<info.shape.size();++axis)axes.append(int(axis));
  return {{"schema",2},{"source_kind","grib"},{"decode_profile","GribDecodedV1"},{"operation","read"},
    {"file",file},{"variable",target},{"source_stamp",slice_stamp(file)},{"request_id","9007199254741009"},{"source_revision","7"},
    {"expected_shape",metadata["shape"]},{"spec",metadata["spec"]},{"coordinate_axes",axes}};
}
int worker(const QJsonObject &object,const QString &folder) {
  QDir().mkpath(folder);if(!write_json(folder+"/request.json",object))return -1;
  QProcess process;process.start(QCoreApplication::applicationDirPath()+"/hdf_cli.exe",{"slice-worker",folder+"/request.json"});
  if(!process.waitForFinished(120000)){process.kill();process.waitForFinished();return -2;}
  return process.exitStatus()==QProcess::NormalExit?process.exitCode():-3;
}
}
class GribSliceTest:public QObject {
  Q_OBJECT
private slots:
  void allOrderedAxisPairs();
  void missingSlotsAndSpatialFields();
  void invalidShapeIndicesAndBudget();
  void workerV2AndInspect();
  void workerEnvelopeAndSourceChange();
  void corruptV2Rejected();
  void cliCsvAndSourceProtection();
  void businessGrib1();
  void businessGrib2();
};
void GribSliceTest::allOrderedAxisPairs() {
  const auto file=fixture("mini_cube.grb2"),target=cube(file);const auto info=slice_inspect(file,target.toStdString());
  QVERIFY2(info.error.isEmpty(),qPrintable(info.error));QCOMPARE(info.source_kind,QString("grib"));QCOMPARE(info.decode_profile,QString("GribDecodedV1"));
  QCOMPARE(info.shape,std::vector<hsize_t>({2,2,3,4}));QVERIFY(info.source_working_bytes>0);QCOMPARE(info.axes[0].source_labels.size(),2);
  for(const auto &axis:info.axes){QVERIFY(axis.coordinate_path.isEmpty());QCOMPARE(axis.role,SliceAxisRole::Unknown);}
  for(size_t row=0;row<4;++row)for(size_t col=0;col<4;++col)if(row!=col) {
    const NcSliceSpec spec{row,col,{1,1,2,3}};auto out=nc_read_slice_decoded(file,target.toStdString(),info.shape,spec);
    QVERIFY2(out.error.isEmpty(),qPrintable(out.error));QCOMPARE(out.plane.rows,size_t(info.shape[row]));QCOMPARE(out.plane.cols,size_t(info.shape[col]));QVERIFY(out.lease);
    for(size_t r=0;r<out.plane.rows;++r)for(size_t c=0;c<out.plane.cols;++c){auto index=spec.fixed_indices;index[row]=r;index[col]=c;
      QCOMPARE(out.plane.values[r*out.plane.cols+c],(index[1]?85000.:50000.)+600.*index[0]+4.*index[2]+index[3]);}
  }
}
void GribSliceTest::missingSlotsAndSpatialFields() {
  const auto file=fixture("mini_missing_slice.grb2"),target=cube(file);const auto info=slice_inspect(file,target.toStdString());
  auto out=nc_read_slice_decoded(file,target.toStdString(),info.shape,{0,1,{0,0,2,3}});
  QVERIFY2(out.error.isEmpty(),qPrintable(out.error));QCOMPARE(out.missing_fields,size_t(1));QCOMPARE(out.plane.values[0],50011.);QVERIFY(std::isnan(out.plane.values[2]));
  out=nc_read_slice_decoded(file,target.toStdString(),info.shape,{2,3,{1,0,0,0}});QVERIFY(out.error.isEmpty());QCOMPARE(out.missing_fields,size_t(1));
  for(double value:out.plane.values)QVERIFY(std::isnan(value));
  const auto full=fixture("mini_cube.grb2");out=nc_read_slice_decoded(full,"/field/3",{3,4},{1,0,{0,0}});
  QVERIFY(out.error.isEmpty());QCOMPARE(out.plane.rows,size_t(4));QCOMPARE(out.plane.values[2*3+1],85606.);
}
void GribSliceTest::invalidShapeIndicesAndBudget() {
  const auto file=fixture("mini_cube.grb2"),target=cube(file);const std::vector<hsize_t> shape{2,2,3,4};
  for(const auto &spec:std::vector<NcSliceSpec>{{0,0,{0,0,0,0}},{4,1,{0,0,0,0}},{0,1,{0,0,3,0}},{0,1,{0,0}}}) {
    const auto out=nc_read_slice_decoded(file,target.toStdString(),shape,spec);QVERIFY(!out.error.isEmpty());QVERIFY(out.plane.values.empty());
  }
  QCOMPARE(nc_read_slice_decoded(file,target.toStdString(),{2,2,3,5},{0,1,{0,0,0,0}}).error_code,QString("ShapeChanged"));
  auto catalog=*GribData::open(file);catalog.fields[0].points=5000000;
  bool rejected=false;try{GribData::slice_source_bytes(catalog,target);}catch(const grib::Error &error){rejected=error.code=="ResourceLimit";}QVERIFY(rejected);
  rejected=false;try{grib::reserve_memory(769ull*1024*1024);}catch(const grib::Error &error){rejected=error.code=="ResourceLimit";}QVERIFY(rejected);
}
void GribSliceTest::workerV2AndInspect() {
  QTemporaryDir folder;const auto file=fixture("mini_cube.grb2"),target=cube(file);const auto info=slice_inspect(file,target.toStdString());
  auto object=request(file,target,info,{2,0,{0,1,0,3}});const auto task=folder.path()+"/read";QCOMPARE(worker(object,task),0);
  const auto out=slice_load_result(task,9007199254741009ull);QVERIFY2(out.error.isEmpty(),qPrintable(out.error));
  QCOMPARE(out.plane.rows,size_t(3));QCOMPARE(out.plane.cols,size_t(2));QCOMPARE(out.plane.values[2*2+1],85611.);
  QCOMPARE(out.info.source_kind,QString("grib"));QCOMPARE(out.info.axes[0].source_labels,info.axes[0].source_labels);QVERIFY(out.coordinates.empty());
  QCOMPARE(out.metadata["missing_fields"].toString(),QString("0"));QVERIFY(out.metadata["timing_ms"].toObject().contains("catalog_scan"));
  object["operation"]="inspect";QCOMPARE(worker(object,folder.path()+"/inspect"),0);const auto inspect=slice_load_result(folder.path()+"/inspect",9007199254741009ull);
  QVERIFY2(inspect.error.isEmpty(),qPrintable(inspect.error));QVERIFY(inspect.plane.values.empty());QCOMPARE(inspect.info.shape,info.shape);
}
void GribSliceTest::workerEnvelopeAndSourceChange() {
  QTemporaryDir folder;const auto file=fixture("mini_cube.grb2"),target=cube(file);const auto info=slice_inspect(file,target.toStdString());
  const auto original=request(file,target,info,{2,3,{0,0,0,0}});
  for(int variant=0;variant<7;++variant){auto object=original;
    if(variant==0)object["schema"]=1;if(variant==1)object.remove("source_kind");if(variant==2)object["decode_profile"]="DecodeProfileV1";
    if(variant==3)object["schema"]=2.5;if(variant==4)object["source_kind"]="hdf";
    if(variant==5)object["source_stamp"]=QJsonObject{{"size_bytes","1"},{"mtime_utc_ms","0"}};
    if(variant==6)object["coordinate_axes"]=QJsonArray{2,2,3};
    const auto task=folder.path()+"/bad-"+QString::number(variant);QCOMPARE(worker(object,task),variant==5?3:2);QVERIFY(!QFileInfo::exists(task+"/plane.bin"));
  }
}
void GribSliceTest::corruptV2Rejected() {
  QTemporaryDir folder;const auto file=fixture("mini_cube.grb2"),target=cube(file);const auto info=slice_inspect(file,target.toStdString());
  QCOMPARE(worker(request(file,target,info,{2,3,{0,0,0,0}}),folder.path()),0);const auto original=read_json(folder.path()+"/result.json");
  for(int variant=0;variant<8;++variant){auto object=original;
    if(variant==0)object["schema"]=1;if(variant==1)object["source_kind"]="hdf";if(variant==2)object["decode_profile"]="DecodeProfileV1";
    if(variant==3)object["source_working_bytes"]=42;if(variant==4)object["missing_fields"]="-1";if(variant==5)object["data_file"]="../plane.bin";
    if(variant==6){auto axes=object["axes"].toArray();auto axis=axes[0].toObject();axis["source_labels"]=QJsonArray{"bad"};axes[0]=axis;object["axes"]=axes;}
    if(variant==7)object["rows"]="4";
    QVERIFY(write_json(folder.path()+"/result.json",object));QVERIFY2(!slice_load_result(folder.path(),9007199254741009ull).error.isEmpty(),qPrintable(QString::number(variant)));
  }
  QVERIFY(write_json(folder.path()+"/result.json",original));QFile binary(folder.path()+"/plane.bin");QVERIFY(binary.open(QIODevice::ReadWrite));QVERIFY(binary.seek(8));binary.write(QByteArray(1,char(1)));binary.close();
  QVERIFY(!slice_load_result(folder.path(),9007199254741009ull).error.isEmpty());
}
void GribSliceTest::cliCsvAndSourceProtection() {
  QTemporaryDir folder;const auto source=folder.path()+QString::fromUtf8("/源数据.grb2");QVERIFY(QFile::copy(fixture("mini_cube.grb2"),source));const auto target=cube(source);
  const auto output=folder.path()+"/section.csv";QProcess process;
  auto run=[&](const QString &path){process.start(QCoreApplication::applicationDirPath()+"/hdf_cli.exe",{"slice",source,target,"2","0",path,"0","1","0","3"});if(!process.waitForFinished(10000))return -1;return process.exitCode();};
  QCOMPARE(run(output),0);QFile csv(output);QVERIFY(csv.open(QIODevice::ReadOnly));const auto bytes=csv.readAll();QVERIFY(bytes.startsWith("# hovmoller_schema=2"));QVERIFY(bytes.contains("GribDecodedV1"));QVERIFY(bytes.contains("85611"));csv.close();
  QCOMPARE(run(output),4);QCOMPARE(run(source),4);QCOMPARE(QFileInfo(source).size(),QFileInfo(fixture("mini_cube.grb2")).size());
}
static void business(int edition) {
  const auto file=qEnvironmentVariable(edition==1?"HDFSTUDIO_GRIB1_SAMPLE":"HDFSTUDIO_GRIB2_SAMPLE");QVERIFY2(!file.isEmpty(),"Set both business source environment variables");
  const auto catalog=GribData::open(file);const auto evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");QVERIFY(!evidence.isEmpty());QJsonArray records;
  const int count=edition==1?5:int(catalog->fields.size());
  for(int n=0;n<count;++n){const int index=edition==1?n*int(catalog->fields.size()-1)/4:n;const auto target=QString("/field/%1").arg(index);const auto info=slice_inspect(file,target.toStdString());
    const auto out=nc_read_slice_decoded(file,target.toStdString(),info.shape,{1,0,{0,0}});QVERIFY2(out.error.isEmpty(),qPrintable(out.error));
    const auto plane=GribData::read(*catalog,target,{});QVERIFY(plane.error.isEmpty());
    for(size_t row=0;row<out.plane.rows;++row)for(size_t col=0;col<out.plane.cols;++col){const double actual=out.plane.values[row*out.plane.cols+col],expected=plane.values[col*plane.cols+row];QVERIFY((std::isnan(actual)&&std::isnan(expected))||actual==expected);}
    const auto name=QString("section-grib%1-field-%2.f64").arg(edition).arg(index);QFile binary(evidence+"/"+name);QVERIFY(binary.open(QIODevice::WriteOnly));QDataStream stream(&binary);stream.setByteOrder(QDataStream::LittleEndian);for(double value:out.plane.values)stream<<value;binary.close();
    records.append(QJsonObject{{"field",index},{"rows",QString::number(out.plane.rows)},{"cols",QString::number(out.plane.cols)},{"file",name}});
  }
  QJsonObject report{{"fields",records}};
  if(edition==1){
    const grib::Cube *selected=nullptr;
    for(const auto &item:catalog->cubes)if(!item.axes.empty()&&item.axes[0].labels.size()>=3&&(!selected||item.axes[0].labels.size()<selected->axes[0].labels.size()))selected=&item;
    QVERIFY(selected);const auto info=slice_inspect(file,selected->id.toStdString());const size_t rank=info.shape.size();NcSliceSpec spec{rank-2,0,std::vector<size_t>(rank,0)};spec.fixed_indices.back()=17;
    const auto out=nc_read_slice_decoded(file,selected->id.toStdString(),info.shape,spec);QVERIFY2(out.error.isEmpty(),qPrintable(out.error));
    const auto task=evidence+"/business-cube-worker";QCOMPARE(worker(request(file,selected->id,info,spec),task),0);
    const auto loaded=slice_load_result(task,9007199254741009ull);QVERIFY2(loaded.error.isEmpty(),qPrintable(loaded.error));QCOMPARE(loaded.plane.values.size(),out.plane.values.size());
    for(size_t i=0;i<out.plane.values.size();++i)QVERIFY((std::isnan(out.plane.values[i])&&std::isnan(loaded.plane.values[i]))||out.plane.values[i]==loaded.plane.values[i]);
    QJsonArray fields;for(size_t col=0;col<out.plane.cols;++col){std::vector<int> indices(rank-2,0);indices[0]=int(col);try{fields.append(grib::resolve(*catalog,selected->id,indices));}catch(const grib::Error &error){QCOMPARE(error.code,QString("MissingSlice"));fields.append(-1);}}
    report["cube"]=QJsonObject{{"target",selected->id},{"fields",fields},{"fixed_x",17},{"rows",QString::number(out.plane.rows)},{"cols",QString::number(out.plane.cols)},{"file","business-cube-worker/plane.bin"},{"missing_fields",QString::number(out.missing_fields)}};
  }
  QVERIFY(write_json(evidence+QString("/section-grib%1.json").arg(edition),report));
}
void GribSliceTest::businessGrib1(){business(1);}
void GribSliceTest::businessGrib2(){business(2);}
QTEST_GUILESS_MAIN(GribSliceTest)
#include "grib_slice_test.moc"
