// Independent upstream-encoded fixtures and malformed chunk boundaries for the read-only filter.
#include <QtTest>
#include <QTemporaryDir>
#include <QBuffer>
#include <QScopeGuard>
#include "hdf_filters.hpp"
#include "hdf_filters_fixture.hpp"
#include "plane_reader.hpp"
#include "text_export.hpp"
#include <cstring>
#include <cstdlib>
#include <cmath>

class HdfFiltersTest : public QObject {
  Q_OBJECT
private slots:
  void independentModesTypesAndSlices();
  void directChunksAndTrailingBytes();
  void corruptChunksKeepOriginalBuffer();
  void unknownFilterNamesAreReported();
};

void HdfFiltersTest::independentModesTypesAndSlices() {
  const char *fixtures[] = {HdfFilterFixture::file_none, HdfFilterFixture::file_lz4, HdfFilterFixture::file_zstd};
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  for(int mode = 0; mode < 3; ++mode) {
    const QString file = dir.filePath(QString("mode-%1.%2").arg(mode).arg(mode == 2 ? "nc" : "h5"));
    QFile output(file); QVERIFY(output.open(QIODevice::WriteOnly));
    const QByteArray bytes = QByteArray::fromBase64(fixtures[mode]);
    QCOMPARE(output.write(bytes), bytes.size()); output.close();
    for(const auto &name : {"signed", "unsigned", "floating", "bigendian"}) {
      const auto expected = [&](size_t index) -> double {
        double value = int(index % 200) - 100;
        if(QByteArray(name) == "unsigned") value = 4000000000.0 + index;
        else if(QByteArray(name) == "floating") value *= .125;
        if(QByteArray(name) == "signed" && (value == -100 || value == -99)) return NAN;
        return value * .0001 + 2.0;
      };
      for(size_t layer : {size_t(0), size_t(1)}) {
        auto result = nc_read_plane_decoded(file, std::string("/") + name, {2,35,67}, {layer});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.plane.values.size(), size_t(35 * 67));
        for(size_t index = 0; index < result.plane.values.size(); ++index) {
          const double reference = expected(layer * 35 * 67 + index);
          if(std::isnan(reference)) QVERIFY(std::isnan(result.plane.values[index]));
          else QCOMPARE(result.plane.values[index], reference);
        }
      }
      NcSliceSpec spec; spec.row_axis = 2; spec.col_axis = 1; spec.fixed_indices = {1,0,0};
      auto section = nc_read_slice_decoded(file, std::string("/") + name, {2,35,67}, spec);
      QVERIFY2(section.error.isEmpty(), qPrintable(section.error));
      QCOMPARE(section.plane.rows, size_t(67)); QCOMPARE(section.plane.cols, size_t(35));
      for(size_t row = 0; row < 67; ++row) for(size_t col = 0; col < 35; ++col) {
        const double reference = expected(35 * 67 + col * 67 + row);
        if(std::isnan(reference)) QVERIFY(std::isnan(section.plane.values[row * 35 + col]));
        else QCOMPARE(section.plane.values[row * 35 + col], reference);
      }
      QBuffer text; QVERIFY(text.open(QIODevice::WriteOnly));
      auto exported = HdfTextExport::write(file, "/" + QString::fromLatin1(name), text, 71);
      QVERIFY2(exported.ok, qPrintable(exported.error)); QCOMPARE(exported.valuesWritten, quint64(2*35*67));
      QVERIFY(!text.data().isEmpty());
    }
  }
}

void HdfFiltersTest::directChunksAndTrailingBytes() {
  const char *chunks[] = {HdfFilterFixture::chunk_none, HdfFilterFixture::chunk_lz4, HdfFilterFixture::chunk_zstd};
  for(int index = 0; index < 3; ++index) {
    const unsigned mode = index == 0 ? 0 : unsigned(index + 1);
    const unsigned parameters[] = {0,4,4,0,mode,3};
    QByteArray chunk = QByteArray::fromBase64(chunks[index]);
    size_t capacity = size_t(chunk.size()); void *buffer = std::malloc(capacity);
    std::memcpy(buffer, chunk.constData(), capacity);
    const size_t decoded = hdf_bitshuffle_decode(H5Z_FLAG_REVERSE, 6, parameters, capacity, &capacity, &buffer);
    QCOMPARE(decoded, size_t(35*67*4)); QCOMPARE(capacity, decoded);
    for(int element = 0; element < 35*67; ++element) {
      int32_t value; std::memcpy(&value, static_cast<char*>(buffer) + element*4, 4);
      QCOMPARE(value, element%200-100);
    }
    std::free(buffer);
  }
  QVERIFY(hdf_register_read_filters());
  unsigned configuration = 0; QVERIFY(H5Zget_filter_info(32008, &configuration) >= 0);
  QVERIFY(configuration & H5Z_FILTER_CONFIG_DECODE_ENABLED);
  QVERIFY(!(configuration & H5Z_FILTER_CONFIG_ENCODE_ENABLED));
}

void HdfFiltersTest::corruptChunksKeepOriginalBuffer() {
  H5E_auto2_t previous = nullptr; void *previous_data = nullptr;
  H5Eget_auto2(H5E_DEFAULT, &previous, &previous_data); H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
  const auto restore = qScopeGuard([&]{H5Eset_auto2(H5E_DEFAULT, previous, previous_data);});
  const QByteArray original = QByteArray::fromBase64(HdfFilterFixture::chunk_zstd);
  QList<QByteArray> damaged;
  damaged << original.left(8) << original.left(original.size()-1) << original + 'x';
  auto size = original; for(int i=0;i<8;++i) size[i]=char(0xff); damaged << size;
  auto block = original; for(int i=8;i<12;++i) block[i]=0; damaged << block;
  auto length = original; for(int i=12;i<16;++i) length[i]=char(0xff); damaged << length;
  auto frame = original; frame[16] = char(frame[16] ^ 0xff); damaged << frame;
  for(int variant = 0; variant < damaged.size() + 3; ++variant) {
    const QByteArray chunk = variant < damaged.size() ? damaged[variant] : original;
    unsigned parameters[] = {0,4,4,0,3,3}; size_t parameter_count = 6;
    if(variant == damaged.size()) parameters[2] = 0;
    if(variant == damaged.size()+1) parameters[4] = 99;
    if(variant == damaged.size()+2) parameter_count = 2;
    size_t capacity = size_t(chunk.size()); const size_t before = capacity;
    void *buffer = std::malloc(capacity); std::memcpy(buffer, chunk.constData(), capacity);
    void *identity = buffer;
    QCOMPARE(hdf_bitshuffle_decode(H5Z_FLAG_REVERSE, parameter_count, parameters, capacity, &capacity, &buffer), size_t(0));
    QCOMPARE(buffer, identity); QCOMPARE(capacity, before);
    QCOMPARE(QByteArray(static_cast<const char*>(buffer), qsizetype(capacity)), chunk);
    QVERIFY(hdf_filter_read_error("Read failed").startsWith("Read failed: "));
    std::free(buffer);
  }
}

void HdfFiltersTest::unknownFilterNamesAreReported() {
  QTemporaryDir dir; const QByteArray path = QFile::encodeName(dir.filePath("unknown.h5"));
  const hid_t file = H5Fcreate(path.constData(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  QVERIFY(file >= 0); const hsize_t shape[] = {2,2};
  const hid_t space = H5Screate_simple(2,shape,nullptr), creation = H5Pcreate(H5P_DATASET_CREATE);
  QVERIFY(H5Pset_chunk(creation,2,shape) >= 0);
  QVERIFY(H5Pset_filter(creation,32009,H5Z_FLAG_OPTIONAL,0,nullptr) >= 0);
  const hid_t dataset = H5Dcreate2(file,"unknown",H5T_STD_I32LE,space,H5P_DEFAULT,creation,H5P_DEFAULT);
  QVERIFY(dataset >= 0);
  const QString error = hdf_prepare_dataset(dataset);
  QVERIFY(error.contains("32009")); QVERIFY(error.contains("unavailable"));
  const int values[] = {1,2,3,4};
  QVERIFY(H5Dwrite(dataset,H5T_NATIVE_INT,H5S_ALL,H5S_ALL,H5P_DEFAULT,values) >= 0);
  H5Dclose(dataset); H5Pclose(creation); H5Sclose(space); H5Fclose(file);
  // Optional filter skipped by the writer: HDF5 can read this chunk without the codec.
  const auto plane = nc_read_plane_decoded(QFile::decodeName(path), "/unknown", {2,2}, {});
  QVERIFY2(plane.error.isEmpty(), qPrintable(plane.error));
  QCOMPARE(plane.plane.values, std::vector<double>({1,2,3,4}));
}

QTEST_GUILESS_MAIN(HdfFiltersTest)
#include "hdf_filters_test.moc"
