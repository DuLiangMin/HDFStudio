// ecCodes operations are serialized: default context and multi-field state are process-global.
#include "grib_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <eccodes.h>
#include <mutex>
#include <numeric>
#include <random>
#include <tuple>

namespace grib {
namespace {
std::recursive_mutex codec_mutex;
std::mutex budget_mutex;
size_t reserved_bytes = 0;
struct MemoryLease {
    size_t bytes;
    explicit MemoryLease(size_t requested) : bytes(requested) {
        std::lock_guard<std::mutex> lock(budget_mutex);
        if (bytes > 768ULL * 1024 * 1024 - reserved_bytes)
            throw Error("ResourceLimit",
                        "768 MiB process plane budget exceeded; close other views");
        reserved_bytes += bytes;
    }
    ~MemoryLease() {
        std::lock_guard<std::mutex> lock(budget_mutex);
        reserved_bytes -= bytes;
    }
};
using Handle = std::unique_ptr<codes_handle, decltype(&codes_handle_delete)>;
Handle owned(codes_handle* handle) {
    return Handle(handle, codes_handle_delete);
}
// No FILE* crosses the ecCodes DLL boundary: Debug and Release CRTs differ on Windows.
// QFile also preserves Unicode paths. Decode one bounded physical message in memory.
struct Message {
    qint64 offset = 0;
    QByteArray bytes;
    int fields = 1;
};
quint64 unsigned_be(const QByteArray& bytes, int begin, int count) {
    quint64 value = 0;
    for (int index = 0; index < count; ++index)
        value = (value << 8) | quint8(bytes[begin + index]);
    return value;
}
bool next_message(QFile& file, Message& message, const std::function<bool()>& cancelled = {}) {
    while (!file.atEnd()) {
        if (cancelled && cancelled())
            throw Error("Cancelled", "Operation cancelled");
        qint64 start = file.pos();
        QByteArray chunk = file.peek(4);
        qsizetype found = 0;
        if (chunk != "GRIB") {
            chunk = file.read(1024 * 1024);
            found = chunk.indexOf("GRIB");
            if (found < 0) {
                if (chunk.size() < 4)
                    return false;
                file.seek(start + chunk.size() - 3);
                continue;
            }
        }
        message.offset = start + found;
        if (!file.seek(message.offset))
            throw Error("ReadFailed", "Message seek failed");
        QByteArray header = file.peek(16);
        if (header.size() < 8)
            throw Error("CorruptMessage", "Truncated GRIB header");
        int edition = quint8(header[7]);
        quint64 length = 0;
        if (edition == 1) {
            length = unsigned_be(header, 4, 3);
            if (length & 0x800000)
                throw Error("UnsupportedPacking",
                            "GRIB1 large-message convention is not supported");
        } else if (edition == 2) {
            if (header.size() < 16)
                throw Error("CorruptMessage", "Truncated GRIB2 header");
            length = unsigned_be(header, 8, 8);
        } else
            throw Error("CorruptMessage", "Unsupported GRIB edition in message header");
        if (length < quint64(edition == 2 ? 20 : 12) ||
            length > quint64(file.size() - message.offset))
            throw Error("CorruptMessage", "Truncated or invalid physical message length");
        if (length > 512ULL * 1024 * 1024)
            throw Error("ResourceLimit", "Physical message exceeds 512 MiB");
        message.bytes = file.read(qint64(length));
        if (quint64(message.bytes.size()) != length || !message.bytes.endsWith("7777"))
            throw Error("CorruptMessage", "Missing GRIB end marker");
        message.fields = 1;
        if (edition == 2) {
            message.fields = 0;
            quint64 position = 16;
            while (position + 4 < length) {
                if (position + 5 > length - 4)
                    throw Error("CorruptMessage", "Truncated section header");
                quint64 section_length = unsigned_be(message.bytes, int(position), 4);
                int section = quint8(message.bytes[int(position) + 4]);
                if (section_length < 5 || section_length > length - 4 - position || section < 1 ||
                    section > 7)
                    throw Error("CorruptMessage", "Invalid GRIB2 section length or number");
                if (section == 7)
                    ++message.fields;
                position += section_length;
            }
            if (position != length - 4 || !message.fields)
                throw Error("CorruptMessage", "Incomplete GRIB2 sections");
        }
        return true;
    }
    return false;
}
void initialise() {
    static bool ready = false;
    if (!ready) {
        codes_grib_multi_support_on(nullptr);
        ready = true;
    }
}
long integer(codes_handle* h, const char* key, long fallback = 0) {
    long result = fallback;
    if (codes_get_long(h, key, &result))
        return fallback;
    return result;
}
double real(codes_handle* h, const char* key, double fallback = missing) {
    double result = fallback;
    if (codes_get_double(h, key, &result))
        return fallback;
    return result;
}
QString text(codes_handle* h, const char* key) {
    size_t length = 0;
    if (codes_get_length(h, key, &length) || length > 65536)
        return {};
    QByteArray buffer(int(length + 1), 0);
    size_t capacity = size_t(buffer.size());
    if (codes_get_string(h, key, buffer.data(), &capacity))
        return {};
    return QString::fromUtf8(buffer.constData());
}
void check(int status, const QString& context) {
    if (status)
        throw Error("DecodeFailed",
                    context + ": " + QString::fromUtf8(codes_get_error_message(status)));
}
QStringList identity_keys() {
    return {"edition",
            "centre",
            "subCentre",
            "tablesVersion",
            "localTablesVersion",
            "table2Version",
            "indicatorOfParameter",
            "discipline",
            "parameterCategory",
            "parameterNumber",
            "paramId",
            "shortName",
            "name",
            "units",
            "typeOfLevel",
            "level",
            "dataDate",
            "dataTime",
            "stepRange",
            "startStep",
            "endStep",
            "stepUnits",
            "stepType",
            "validityDate",
            "validityTime",
            "number",
            "productDefinitionTemplateNumber",
            "typeOfStatisticalProcessing",
            "typeOfGeneratingProcess",
            "generatingProcessIdentifier",
            "typeOfFirstFixedSurface",
            "scaleFactorOfFirstFixedSurface",
            "scaledValueOfFirstFixedSurface",
            "typeOfSecondFixedSurface",
            "scaleFactorOfSecondFixedSurface",
            "scaledValueOfSecondFixedSurface",
            "gridType",
            "Ni",
            "Nj",
            "Nx",
            "Ny",
            "numberOfDataPoints",
            "numberOfValues",
            "scanningMode",
            "iScansNegatively",
            "jScansPositively",
            "jPointsAreConsecutive",
            "alternativeRowScanning",
            "latitudeOfFirstGridPointInDegrees",
            "longitudeOfFirstGridPointInDegrees",
            "latitudeOfLastGridPointInDegrees",
            "longitudeOfLastGridPointInDegrees",
            "packingType",
            "bitsPerValue",
            "referenceValue",
            "binaryScaleFactor",
            "decimalScaleFactor",
            "bitmapPresent",
            "missingValue",
            "numberOfMissing",
            "missingValueManagementUsed",
            "md5GridSection",
            "uvRelativeToGrid"};
}
Field inspect(codes_handle* h, int index, int sub, qint64 offset, qint64 length) {
    Field f;
    f.index = index;
    f.sub_field = sub;
    f.offset = offset;
    f.length = length;
    for (const QString& key : identity_keys()) {
        const QByteArray name = key.toLatin1();
        if (codes_is_defined(h, name.constData()))
            f.keys[key] = text(h, name.constData());
    }
    f.edition = int(integer(h, "edition"));
    f.short_name = text(h, "shortName");
    f.name = text(h, "name");
    f.units = text(h, "units");
    f.centre = text(h, "centre");
    // Table provenance is part of parameter identity even when ecCodes has a friendly shortName.
    QStringList ids = {text(h, "edition"),
                       text(h, "centre"),
                       text(h, "subCentre"),
                       text(h, "tablesVersion"),
                       text(h, "localTablesVersion"),
                       text(h, "table2Version"),
                       text(h, "discipline"),
                       text(h, "parameterCategory"),
                       text(h, "parameterNumber"),
                       text(h, "indicatorOfParameter")};
    f.parameter = ids.join(':');
    f.level_type = text(h, "typeOfLevel");
    f.level = real(h, "level", 0);
    f.grid_type = text(h, "gridType");
    f.grid_hash = text(h, "md5GridSection");
    f.ni = integer(h, "Ni", integer(h, "Nx"));
    f.nj = integer(h, "Nj", integer(h, "Ny"));
    f.points = integer(h, "numberOfDataPoints", integer(h, "numberOfValues"));
    f.scan = integer(h, "scanningMode");
    f.packing = text(h, "packingType");
    f.bits = integer(h, "bitsPerValue");
    f.decimal = integer(h, "decimalScaleFactor");
    f.binary = integer(h, "binaryScaleFactor");
    f.reference = real(h, "referenceValue", 0);
    f.bitmap = integer(h, "bitmapPresent") != 0;
    f.step_type = text(h, "stepType");
    f.has_member = codes_is_defined(h, "perturbationNumber") || codes_is_defined(h, "number");
    f.member = integer(h, "number", integer(h, "perturbationNumber"));
    // Reference cycle and interval are retained so coincident validity times never overwrite.
    f.time_key = text(h, "dataDate") + "T" + text(h, "dataTime").rightJustified(4, '0') + "/" +
                 text(h, "stepRange") + "/" + text(h, "stepUnits");
    f.time_label = text(h, "validityDate") + " " + text(h, "validityTime").rightJustified(4, '0') +
                   " [ref " + text(h, "dataDate") + " " +
                   text(h, "dataTime").rightJustified(4, '0') + ", step " + text(h, "stepRange") +
                   " " + text(h, "stepUnits") + "]";
    return f;
}
template <class Callback> void message_fields(Message& message, Callback callback) {
    initialise();
    void* data = message.bytes.data();
    size_t remaining = size_t(message.bytes.size());
    struct Reset {
        ~Reset() {
            codes_grib_multi_support_reset_file(nullptr, nullptr);
        }
    } reset;
    for (int sub = 0; sub < message.fields; ++sub) {
        int status = 0;
        auto handle =
            owned(codes_grib_handle_new_from_multi_message(nullptr, &data, &remaining, &status));
        if (!handle || status)
            throw Error("CorruptMessage", "Cannot decode sub-field " + QString::number(sub) + ": " +
                                              QString::fromUtf8(codes_get_error_message(status)));
        callback(handle.get(), sub);
    }
}
void validate(const Catalog& cat) {
    QFileInfo info(cat.path);
    if (info.size() != cat.size || info.lastModified().toMSecsSinceEpoch() != cat.modified)
        throw Error("SourceChanged", "File changed since indexing; close and reopen it");
}
template <class Callback> void with_field(const Catalog& cat, int index, Callback callback) {
    if (index < 0 || index >= int(cat.fields.size()))
        throw Error("InvalidSelection", "Field index out of range");
    validate(cat);
    const auto& field = cat.fields[size_t(index)];
    QFile file(cat.path);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(field.offset))
        throw Error("OpenFailed", "Cannot reopen source message");
    Message message;
    if (!next_message(file, message) || message.offset != field.offset ||
        message.bytes.size() != field.length || field.sub_field >= message.fields)
        throw Error("CorruptMessage", "Indexed physical message changed");
    message_fields(message, [&](codes_handle* handle, int sub) {
        if (sub == field.sub_field)
            callback(handle);
    });
}
QString grouping_key(const Field& f) {
    QStringList items = {
        f.parameter, f.level_type, f.grid_hash, f.grid_type, QString::number(f.points),
        f.units,     f.step_type};
    for (const QString& key :
         QStringList{"productDefinitionTemplateNumber", "typeOfStatisticalProcessing",
                     "typeOfGeneratingProcess", "generatingProcessIdentifier",
                     "typeOfFirstFixedSurface", "typeOfSecondFixedSurface"})
        items << f.keys.value(key);
    // Second-surface level metadata distinguishes layers with identical first levels.
    items << f.keys.value("scaleFactorOfSecondFixedSurface")
          << f.keys.value("scaledValueOfSecondFixedSurface");
    items << QString::number(f.has_member) << f.keys.value("uvRelativeToGrid");
    // Only explicitly covered templates may aggregate. Unknown products remain lossless fields.
    bool template_ok = false;
    const int product_template = f.keys.value("productDefinitionTemplateNumber").toInt(&template_ok);
    if ((f.edition == 2 && (!template_ok || !QList<int>{0,1,2,8,11}.contains(product_template))) ||
        f.grid_hash.isEmpty() || f.parameter.isEmpty() || f.level_type.isEmpty())
        items << "individual-field" << QString::number(f.index);
    return items.join('|');
}
QString fallback_name(const Field& f) {
    if (!f.short_name.isEmpty() && f.short_name != "unknown")
        return f.short_name;
    return f.edition == 2
               ? "disc" + f.keys.value("discipline") + "_cat" + f.keys.value("parameterCategory") +
                     "_num" + f.keys.value("parameterNumber")
               : "tab" + f.keys.value("table2Version") + "_ind" +
                     f.keys.value("indicatorOfParameter");
}
} // namespace
QString number(double value) {
    return std::isfinite(value) ? QString::number(value, 'g', 17) : QString("missing");
}
QString api_version() {
    std::lock_guard<std::recursive_mutex> lock(codec_mutex);
    long v = codes_get_api_version();
    return QString("%1.%2.%3").arg(v / 10000).arg((v / 100) % 100).arg(v % 100);
}
Probe probe_file(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw Error("OpenFailed", file.errorString());
    QByteArray bytes = file.read(1024 * 1024);
    Probe result;
    if (bytes.startsWith(QByteArray::fromHex("89484446")) || bytes.startsWith("CDF")) {
        result.kind = "HDF5/NetCDF - use HDFStudio";
        return result;
    }
    qsizetype offset = bytes.indexOf("GRIB");
    if (offset >= 0) {
        file.seek(offset);
        Message message;
        if (next_message(file, message)) {
            result.kind = QString("GRIB%1").arg(quint8(message.bytes[7]));
            result.offset = message.offset;
            result.valid = true;
            return result;
        }
    }
    result.kind = "Unknown";
    return result;
}
std::shared_ptr<Catalog> scan_file(const QString& path, const std::function<bool()>& cancelled) {
    std::lock_guard<std::recursive_mutex> lock(codec_mutex);
    auto cat = std::make_shared<Catalog>();
    QFileInfo info(path);
    cat->path = info.absoluteFilePath();
    cat->session = QUuid::createUuid().toString(QUuid::WithoutBraces);
    cat->size = info.size();
    cat->modified = info.lastModified().toMSecsSinceEpoch();
    const Probe probe = probe_file(path);
    if (!probe.valid)
        throw Error(probe.kind.startsWith("HDF") ? "UnsupportedContainer" : "NotGribFormat",
                    probe.kind);
    QSet<int> editions;
    QFile source(cat->path);
    if (!source.open(QIODevice::ReadOnly))
        throw Error("OpenFailed", source.errorString());
    Message message;
    while (next_message(source, message, cancelled))
        message_fields(message, [&](codes_handle* handle, int sub) {
            if (cancelled && cancelled())
                throw Error("Cancelled", "Operation cancelled");
            if (cat->fields.size() >= 200000)
                throw Error("ResourceLimit", "At most 200000 fields per file");
            auto f =
                inspect(handle, int(cat->fields.size()), sub, message.offset, message.bytes.size());
            editions.insert(f.edition);
            cat->fields.push_back(std::move(f));
        });
    if (cat->fields.empty())
        throw Error("CorruptMessage", "No fields decoded");
    cat->format =
        editions.size() > 1 ? "GRIB1+GRIB2 Mixed" : QString("GRIB%1").arg(*editions.begin());
    build_cubes(*cat);
    validate(*cat);
    return cat;
}
void build_cubes(Catalog& cat) {
    cat.cubes.clear();
    QMap<QString, std::vector<int>> groups;
    for (const auto& field : cat.fields)
        groups[grouping_key(field)].push_back(field.index);
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const auto& first = cat.fields[size_t(it.value().front())];
        Cube cube;
        cube.representative = first.index;
        cube.id = "/cube/" + fallback_name(first) + "_" +
                  QString::fromLatin1(
                      QCryptographicHash::hash(it.key().toUtf8(), QCryptographicHash::Sha256)
                          .toHex()
                          .left(12));
        cube.title =
            fallback_name(first) + " [" + first.name + ", " + first.units + "] " + first.level_type;
        QStringList times, members, levels;
        QMap<QString, QString> time_labels;
        for (int index : it.value()) {
            const auto& f = cat.fields[size_t(index)];
            if (!times.contains(f.time_key))
                times << f.time_key;
            time_labels[f.time_key] = f.time_label;
            if (!levels.contains(number(f.level)))
                levels << number(f.level);
            if (!members.contains(QString::number(f.member)))
                members << QString::number(f.member);
        }
        // Only compare numeric steps within the same declared unit. Other cases retain file order.
        QMap<QString,const Field*> time_fields;
        QString common_step_unit; bool numeric_time_order=true;
        for(int index:it.value()) {
            const auto& field=cat.fields[size_t(index)];
            time_fields.insert(field.time_key,&field);
            if(common_step_unit.isEmpty())common_step_unit=field.keys.value("stepUnits");
            bool valid=false;field.keys.value("endStep").toDouble(&valid);
            if(!valid||field.keys.value("stepUnits")!=common_step_unit)numeric_time_order=false;
        }
        if(numeric_time_order)std::stable_sort(times.begin(),times.end(),[&](const QString& left,const QString& right){
            const auto* a=time_fields.value(left);const auto* b=time_fields.value(right);
            return std::make_tuple(a->keys.value("dataDate"),a->keys.value("dataTime").toInt(),a->keys.value("endStep").toDouble(),a->keys.value("startStep").toDouble(),a->index)<
                   std::make_tuple(b->keys.value("dataDate"),b->keys.value("dataTime").toInt(),b->keys.value("endStep").toDouble(),b->keys.value("startStep").toDouble(),b->index);
        });
        std::sort(levels.begin(), levels.end(),
                  [](const QString& a, const QString& b) { return a.toDouble() < b.toDouble(); });
        std::sort(members.begin(), members.end(),
                  [](const QString& a, const QString& b) { return a.toInt() < b.toInt(); });
        if (times.size() > 1) {
            Axis a;
            a.name = "time (reference + step)";
            for (const auto& t : times)
                a.labels << time_labels[t];
            cube.axes.push_back(a);
        }
        if (levels.size() > 1)
            cube.axes.push_back({first.level_type, levels});
        if (members.size() > 1)
            cube.axes.push_back({"number", members});
        quint64 slot_count =
            quint64(times.size()) * quint64(levels.size()) * quint64(members.size());
        // Sparse or duplicate products retain a lossless message axis instead of allocating a huge
        // cube.
        bool fallback = slot_count > 1000000;
        if (!fallback)
            cube.lookup.assign(size_t(slot_count), -1);
        if (!fallback)
            for (int index : it.value()) {
                const auto& f = cat.fields[size_t(index)];
                size_t slot = size_t(
                    (times.indexOf(f.time_key) * levels.size() + levels.indexOf(number(f.level))) *
                        members.size() +
                    members.indexOf(QString::number(f.member)));
                if (cube.lookup[slot] != -1) {
                    fallback = true;
                    break;
                }
                cube.lookup[slot] = index;
            }
        if (fallback) {
            cube.axes.clear();
            Axis axis;
            axis.name = "message (duplicate or sparse axes)";
            for (int index : it.value())
                axis.labels << QString("#%1 %2 level %3 member %4")
                                   .arg(index)
                                   .arg(cat.fields[size_t(index)].time_label)
                                   .arg(cat.fields[size_t(index)].level)
                                   .arg(cat.fields[size_t(index)].member);
            cube.axes.push_back(axis);
            cube.lookup = it.value();
            cat.warnings << cube.id + ": message axis preserves duplicates/sparse coverage";
        }
        cat.cubes.push_back(std::move(cube));
    }
}
int resolve(const Catalog& cat, const QString& target, const std::vector<int>& indices) {
    if (target.startsWith("/field/")) {
        bool ok = false;
        int index = target.mid(7).toInt(&ok);
        if (!ok || index < 0 || index >= int(cat.fields.size()) || !indices.empty())
            throw Error("InvalidSelection", "Invalid field target/indices");
        return index;
    }
    for (const auto& cube : cat.cubes)
        if (cube.id == target) {
            if (indices.size() != cube.axes.size())
                throw Error("InvalidSelection", "Provide exactly one index per cube axis");
            size_t slot = 0;
            for (size_t axis = 0; axis < indices.size(); ++axis) {
                if (indices[axis] < 0 || indices[axis] >= cube.axes[axis].labels.size())
                    throw Error("InvalidSelection", "Cube axis out of range");
                slot = slot * size_t(cube.axes[axis].labels.size()) + size_t(indices[axis]);
            }
            int index = cube.lookup.at(slot);
            if (index < 0)
                throw Error("MissingSlice", "Missing slice in file");
            return index;
        }
    throw Error("InvalidSelection", "Unknown target: " + target);
}
std::shared_ptr<void> reserve_memory(quint64 bytes) {
    if(bytes>768ULL*1024*1024)throw Error("ResourceLimit","GRIB reservation exceeds process budget");
    return std::make_shared<MemoryLease>(size_t(bytes));
}
Plane read_plane(const Catalog& cat, int index, bool coordinates, bool quantized) {
    std::lock_guard<std::recursive_mutex> lock(codec_mutex);
    Plane plane;
    if (index < 0 || index >= int(cat.fields.size()))
        throw Error("InvalidSelection", "Field index out of range");
    const auto& field = cat.fields[size_t(index)];
    with_field(cat, index, [&](codes_handle* h) {
        size_t count = 0;
        check(codes_get_size(h, "values", &count), "values count");
        if (!count || count > 16 * 1024 * 1024)
            throw Error("ResourceLimit", "Plane limited to 16 Mi points");
        // Peak bound includes decoder, values, raw, coordinates, index map and rendering copies.
        if (count > size_t(512 * 1024 * 1024 / 128))
            throw Error("ResourceLimit", "Plane exceeds 512 MiB peak-memory reservation");
        plane.memory_lease = std::make_shared<MemoryLease>(count * 128);
        std::vector<double> values(count);
        size_t length = count;
        check(codes_get_double_array(h, "values", values.data(), &length), "decode values");
        if (length != count)
            throw Error("CorruptMessage", "Decoded count mismatch");
        std::vector<long> bitmap;
        if (field.bitmap) {
            bitmap.resize(count);
            length = count;
            check(codes_get_long_array(h, "bitmap", bitmap.data(), &length), "bitmap");
            if (length != count)
                throw Error("CorruptMessage", "Bitmap count mismatch");
        }
        if (integer(h, "missingValueManagementUsed") > 0) {
            double sentinel = real(h, "missingValue");
            for (auto& value : values)
                if (value == sentinel)
                    value = missing;
        }
        plane.rectangular = field.ni > 0 && field.nj > 0 &&
                            quint64(field.ni) * quint64(field.nj) == count && field.ni < INT_MAX &&
                            field.nj < INT_MAX && field.grid_type != "sh";
        plane.rows = plane.rectangular ? int(field.nj) : int(count);
        plane.cols = plane.rectangular ? int(field.ni) : 1;
        plane.grid_type = field.grid_type;
        plane.units = field.units;
        plane.values.resize(count);
        plane.stored.assign(count, missing);
        plane.source_indices.resize(count);
        bool consecutive = integer(h, "jPointsAreConsecutive") != 0;
        bool alternating = integer(h, "alternativeRowScanning") != 0;
        for (size_t source = 0; source < count; ++source) {
            size_t dest = source;
            if (plane.rectangular) {
                size_t row, col;
                if (!consecutive) {
                    row = source / size_t(plane.cols);
                    col = source % size_t(plane.cols);
                    if (alternating && (row % 2))
                        col = size_t(plane.cols) - 1 - col;
                } else {
                    col = source / size_t(plane.rows);
                    row = source % size_t(plane.rows);
                    if (alternating && (col % 2))
                        row = size_t(plane.rows) - 1 - row;
                }
                dest = row * size_t(plane.cols) + col;
            }
            plane.source_indices[dest] = source;
            plane.values[dest] = (!bitmap.empty() && !bitmap[source]) ? missing : values[source];
        }
        const bool linear =
            QStringList{"grid_simple", "grid_jpeg", "grid_png", "grid_ccsds"}.contains(
                field.packing);
        plane.exact_stored = quantized && linear && field.bits <= 52 && field.bits >= 0;
        plane.raw_description = plane.exact_stored
                                    ? "reconstructed quantized integer X (not packed bytes)"
                                    : "unavailable: packing has no supported per-cell raw integer";
        if (plane.exact_stored) {
            const double decimal = std::pow(10.0, double(field.decimal)),
                         binary = std::ldexp(1.0, int(field.binary));
            if (!std::isfinite(decimal) || decimal == 0 || !std::isfinite(binary) || binary == 0) {
                plane.exact_stored = false;
            } else
                for (size_t i = 0; i < count; ++i) {
                    double y = plane.values[i];
                    if (!std::isfinite(y))
                        continue;
                    double raw =
                        field.bits == 0 ? 0 : std::round((y * decimal - field.reference) / binary);
                    double decoded = (field.reference + raw * binary) / decimal;
                    double tolerance = 1e-12 * std::max(1.0, std::abs(y));
                    if (!std::isfinite(raw) || !std::isfinite(decoded) || raw < 0 ||
                        raw > std::ldexp(1.0, int(field.bits)) - 1 ||
                        std::abs(decoded - y) > tolerance) {
                        plane.exact_stored = false;
                        break;
                    }
                    plane.stored[i] = raw;
                }
        }
        if (!plane.exact_stored) {
            plane.stored.assign(count, missing);
            plane.raw_description =
                "unavailable: per-cell integer reconstruction is unsupported or failed validation";
        }
        if (coordinates && field.grid_type != "sh") {
            std::vector<double> latitude(count), longitude(count);
            length = count;
            int lat_error = codes_get_double_array(h, "latitudes", latitude.data(), &length);
            length = count;
            int lon_error = codes_get_double_array(h, "longitudes", longitude.data(), &length);
            if (!lat_error && !lon_error) {
                plane.latitudes.resize(count);
                plane.longitudes.resize(count);
                for (size_t i = 0; i < count; ++i) {
                    plane.latitudes[i] = latitude[plane.source_indices[i]];
                    plane.longitudes[i] = longitude[plane.source_indices[i]];
                }
            }
        }
    });
    return plane;
}
QString properties(const Catalog& cat, const QString& target) {
    int index = -1;
    for (const auto& cube : cat.cubes)
        if (cube.id == target)
            index = cube.representative;
    if (index < 0)
        index = resolve(cat, target);
    const auto& f = cat.fields.at(size_t(index));
    QStringList lines = {
        "GRIB native properties (ecCodes)",
        "File: " + QDir::toNativeSeparators(cat.path),
        "Object: " + target,
        QString("Field: %1; sub-field: %2; offset: %3; length: %4")
            .arg(index)
            .arg(f.sub_field)
            .arg(f.offset)
            .arg(f.length),
        "Grid identity: " + f.grid_hash,
        "Latitude stored order: " +
            QString(f.keys.value("jScansPositively") == "1" ? "south to north" : "north to south"),
        "Packing: Y = (R + X * 2^E) * 10^(-D)",
        QString("R=%1 E=%2 D=%3 bits=%4")
            .arg(number(f.reference))
            .arg(f.binary)
            .arg(f.decimal)
            .arg(f.bits),
        "missingValue is an ecCodes accessor/default, not proof of a product sentinel. "
        "Bitmap/internal packing masks only; use Mask Range for user filtering."};
    for (auto it = f.keys.cbegin(); it != f.keys.cend(); ++it)
        lines << it.key() + ": " + it.value();
    for (const auto& cube : cat.cubes)
        if (cube.id == target) {
            for (const auto& axis : cube.axes)
                lines << QString("Axis %1: %2").arg(axis.name).arg(axis.labels.size());
            lines << QString("Missing slots: %1")
                         .arg(std::count(cube.lookup.begin(), cube.lookup.end(), -1));
        }
    return lines.join('\n') + '\n';
}
QString keys_dump(const Catalog& cat, int index, const QString& name_space) {
    std::lock_guard<std::recursive_mutex> lock(codec_mutex);
    QStringList lines;
    with_field(cat, index, [&](codes_handle* h) {
        QByteArray ns = name_space.toLatin1();
        auto* iterator = codes_keys_iterator_new(h, CODES_KEYS_ITERATOR_SKIP_DUPLICATES,
                                                 ns.isEmpty() ? nullptr : ns.constData());
        if (!iterator)
            throw Error("DecodeFailed", "Cannot enumerate keys");
        std::unique_ptr<codes_keys_iterator, decltype(&codes_keys_iterator_delete)> guard(
            iterator, codes_keys_iterator_delete);
        int count = 0;
        while (codes_keys_iterator_next(iterator) && count++ < 10000) {
            const char* key = codes_keys_iterator_get_name(iterator);
            size_t size = 0;
            if (codes_get_size(h, key, &size))
                continue;
            lines << QString::fromLatin1(key) + ": " +
                         (size > 32 ? QString("[array of %1 elements; omitted]").arg(size)
                                    : text(h, key));
        }
    });
    return lines.join('\n') + '\n';
}
QJsonObject catalog_json(const Catalog& cat) {
    QJsonArray fields, cubes, warnings;
    for (const auto& f : cat.fields) {
        QJsonObject obj;
        for (auto it = f.keys.cbegin(); it != f.keys.cend(); ++it)
            obj[it.key()] = it.value();
        obj["index"] = f.index;
        obj["offset"] = QString::number(f.offset);
        obj["length"] = QString::number(f.length);
        obj["sub_field"] = f.sub_field;
        obj["parameter_key"] = f.parameter;
        fields << obj;
    }
    for (const auto& c : cat.cubes) {
        QJsonArray axes, lookup;
        for (const auto& a : c.axes)
            axes << QJsonObject{{"name", a.name}, {"labels", QJsonArray::fromStringList(a.labels)}};
        for (int i : c.lookup)
            lookup << i;
        cubes << QJsonObject{{"id", c.id}, {"title", c.title}, {"axes", axes}, {"lookup", lookup}};
    }
    for (const auto& w : cat.warnings)
        warnings << w;
    return {{"schema_version", 1},
            {"format", cat.format},
            {"fields", fields},
            {"cubes", cubes},
            {"warnings", warnings}};
}
size_t mask_values(std::vector<double>& values, double low, double high) {
    if (low > high || std::isnan(low) || std::isnan(high))
        throw Error("InvalidSelection", "Invalid mask range");
    size_t count = 0;
    for (auto& value : values)
        if (std::isfinite(value) && value >= low && value <= high) {
            value = missing;
            ++count;
        }
    return count;
}
bool parse_bound(const QString& text, bool lower, double& value) {
    if (text.trimmed().isEmpty() || text == "-") {
        value = lower ? -INFINITY : INFINITY;
        return true;
    }
    bool ok = false;
    value = text.toDouble(&ok);
    return ok && std::isfinite(value);
}
Statistics statistics(const std::vector<double>& values) {
    Statistics s;
    double mean = 0, m2 = 0;
    for (double value : values) {
        if (!std::isfinite(value)) {
            ++s.missing;
            continue;
        }
        if (!s.finite) {
            s.minimum = value;
            s.maximum = value;
        } else {
            s.minimum = std::min(s.minimum, value);
            s.maximum = std::max(s.maximum, value);
        }
        ++s.finite;
        double delta = value - mean;
        mean += delta / double(s.finite);
        m2 += delta * (value - mean);
    }
    if (s.finite) {
        s.mean = mean;
        s.stddev = std::sqrt(std::max(0.0, m2 / double(s.finite)));
    }
    return s;
}
Histogram histogram(const std::vector<double>& values, double fraction, int bins, quint64 seed) {
    if (!std::isfinite(fraction) || fraction <= 0 || fraction > 1 || bins < 1 || bins > 4096)
        throw Error("InvalidSelection", "fraction must be in (0,1], bins in [1,4096]");
    Histogram h;
    h.population = statistics(values);
    h.fraction = fraction;
    h.seed = seed;
    size_t remaining = h.population.finite;
    if (!remaining)
        return h;
    size_t wanted = std::max(size_t(1), size_t(std::llround(double(remaining) * fraction)));
    std::vector<double> sample;
    sample.reserve(wanted);
    quint64 state = seed ? seed : 1;
    for (double value : values) {
        if (!std::isfinite(value))
            continue;
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        quint64 random = state * 2685821657736338717ULL;
        double uniform = double(random >> 11) * 0x1.0p-53;
        if (wanted && (wanted == remaining || uniform < double(wanted) / double(remaining))) {
            sample.push_back(value);
            --wanted;
        }
        --remaining;
    }
    h.sample = statistics(sample);
    if (h.sample.minimum == h.sample.maximum)
        bins = 1;
    h.edges.resize(size_t(bins + 1));
    h.counts.assign(size_t(bins), 0);
    double range = h.sample.maximum - h.sample.minimum;
    for (int b = 0; b <= bins; ++b)
        h.edges[size_t(b)] = h.sample.minimum + range * b / bins;
    for (double value : sample) {
        int bin = range > 0 ? int((value - h.sample.minimum) / range * bins) : 0;
        bin = std::clamp(bin, 0, bins - 1);
        ++h.counts[size_t(bin)];
    }
    return h;
}
QString histogram_text(const Histogram& h) {
    QStringList lines = {QString("finite: %1 missing: %2 sampled: %3 fraction: %4 seed: %5")
                             .arg(h.population.finite)
                             .arg(h.population.missing)
                             .arg(h.sample.finite)
                             .arg(h.fraction)
                             .arg(h.seed),
                         QString("sample min: %1 max: %2 mean: %3 stddev(population): %4")
                             .arg(number(h.sample.minimum), number(h.sample.maximum),
                                  number(h.sample.mean), number(h.sample.stddev))};
    for (size_t b = 0; b < h.counts.size(); ++b)
        lines
            << QString("%1,%2,%3").arg(number(h.edges[b]), number(h.edges[b + 1])).arg(h.counts[b]);
    return lines.join('\n') + '\n';
}
void export_text(const Plane& p, QIODevice& output, bool stored) {
    if (stored && !p.exact_stored)
        throw Error("UnsupportedPacking", "Per-cell stored integers unavailable");
    QTextStream out(&output);
    out.setRealNumberPrecision(17);
    const auto& v = stored ? p.stored : p.values;
    for (int r = 0; r < p.rows; ++r) {
        for (int c = 0; c < p.cols; ++c) {
            if (c)
                out << ',';
            double value = v[size_t(r) * size_t(p.cols) + size_t(c)];
            if (std::isfinite(value))
                out << value;
        }
        out << '\n';
        if (r % 1024 == 0) {
            out.flush();
            if (out.status() != QTextStream::Ok)
                throw Error("WriteFailed", "Write failed");
        }
    }
    out.flush();
    if (out.status() != QTextStream::Ok)
        throw Error("WriteFailed", "Write failed");
}
void export_csv(const Plane& p, const QString& target, bool stored) {
    if (QFileInfo::exists(target))
        throw Error("WriteFailed", "Output exists; refusing to overwrite");
    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
        throw Error("WriteFailed", output.errorString());
    export_text(p, output, stored);
    if (!output.commit())
        throw Error("WriteFailed", output.errorString());
}
std::vector<ProfileSample> profile(const Plane& p, const std::vector<Point>& controls, bool spline,
                                   bool bilinear, double spacing) {
    std::vector<ProfileSample> result;
    if (controls.size() < 2 || !p.rectangular)
        return result;
    if (spacing <= 0 || !std::isfinite(spacing))
        throw Error("InvalidSelection", "Invalid profile spacing");
    if (controls.size() > 64)
        throw Error("ResourceLimit", "Profile limited to 64 control points");
    for (const auto& control : controls)
        if (!std::isfinite(control.x) || !std::isfinite(control.y) || control.x < 0 ||
            control.y < 0 || control.x > p.cols - 1 || control.y > p.rows - 1)
            throw Error("InvalidSelection", "Profile controls must be finite grid coordinates");
    std::vector<Point> path;
    for (size_t segment = 0; segment + 1 < controls.size(); ++segment) {
        Point a = controls[segment], b = controls[segment + 1],
              before = controls[segment ? segment - 1 : segment],
              after = controls[std::min(segment + 2, controls.size() - 1)];
        double requested = std::ceil(std::hypot(b.x - a.x, b.y - a.y) / spacing) * 4;
        if (!std::isfinite(requested) || requested > 100000)
            throw Error("ResourceLimit", "Profile spacing exceeds sample budget");
        int steps = std::max(2, int(requested));
        if (steps > 100000 || path.size() + size_t(steps) > 1000000)
            throw Error("ResourceLimit", "Profile exceeds sample budget");
        for (int step = 0; step < steps; ++step) {
            double t = double(step) / steps;
            Point point;
            if (spline) {
                auto cat = [&](double p0, double p1, double p2, double p3) {
                    return .5 *
                           ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
                            (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
                };
                point = {cat(before.x, a.x, b.x, after.x), cat(before.y, a.y, b.y, after.y)};
            } else
                point = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
            path.push_back(point);
        }
    }
    path.push_back(controls.back());
    double cumulative = 0, next = 0;
    auto sample = [&](Point point, double distance) {
        double value = missing;
        if (point.x >= 0 && point.y >= 0 && point.x <= p.cols - 1 && point.y <= p.rows - 1) {
            if (!bilinear)
                value =
                    p.values[size_t(std::lround(point.y)) * p.cols + size_t(std::lround(point.x))];
            else {
                int x = int(std::floor(point.x)), y = int(std::floor(point.y)),
                    x1 = std::min(x + 1, p.cols - 1), y1 = std::min(y + 1, p.rows - 1);
                double dx = point.x - x, dy = point.y - y;
                double sum = 0;
                bool valid = true;
                for (int i = 0; i < 4; ++i) {
                    int xx = (i % 2) ? x1 : x, yy = (i / 2) ? y1 : y;
                    double weight = ((i % 2) ? dx : 1 - dx) * ((i / 2) ? dy : 1 - dy);
                    if (weight == 0)
                        continue;
                    double cell = p.values[size_t(yy) * p.cols + xx];
                    if (!std::isfinite(cell)) {
                        valid = false;
                        break;
                    }
                    sum += weight * cell;
                }
                if (valid)
                    value = sum;
            }
        }
        result.push_back({distance, point.x, point.y, value});
    };
    for (size_t i = 1; i < path.size(); ++i) {
        double length = std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
        while (next <= cumulative + length && length > 0) {
            double t = (next - cumulative) / length;
            sample({path[i - 1].x + t * (path[i].x - path[i - 1].x),
                    path[i - 1].y + t * (path[i].y - path[i - 1].y)},
                   next);
            next += spacing;
        }
        cumulative += length;
    }
    if (result.empty() || result.back().distance < cumulative - 1e-9)
        sample(path.back(), cumulative);
    return result;
}
void export_profile(const std::vector<ProfileSample>& samples, const QString& target) {
    if (QFileInfo::exists(target))
        throw Error("WriteFailed", "Output exists; refusing to overwrite");
    QSaveFile file(target);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        throw Error("WriteFailed", file.errorString());
    QTextStream out(&file);
    out << "distance_cells,x,y,decoded\n";
    for (const auto& sample : samples)
        out << number(sample.distance) << ',' << number(sample.x) << ',' << number(sample.y) << ','
            << (std::isfinite(sample.value) ? number(sample.value) : QString()) << '\n';
    out.flush();
    if (out.status() != QTextStream::Ok || !file.commit())
        throw Error("WriteFailed", "Profile CSV write failed");
}
bool vector_compatible(const Field& u, const Field& v) {
    if (u.grid_hash.isEmpty() || u.grid_hash != v.grid_hash || u.grid_type != v.grid_type ||
        u.ni != v.ni || u.nj != v.nj || u.level_type != v.level_type || u.level != v.level ||
        u.time_key != v.time_key || u.has_member != v.has_member || u.member != v.member ||
        u.units != v.units || u.step_type != v.step_type || u.centre != v.centre)
        return false;
    for (const QString& key : QStringList{
             "edition", "subCentre", "tablesVersion", "localTablesVersion", "table2Version",
             "uvRelativeToGrid", "productDefinitionTemplateNumber", "typeOfStatisticalProcessing",
             "typeOfGeneratingProcess", "generatingProcessIdentifier", "typeOfFirstFixedSurface",
             "scaleFactorOfFirstFixedSurface", "scaledValueOfFirstFixedSurface",
             "typeOfSecondFixedSurface", "scaleFactorOfSecondFixedSurface",
             "scaledValueOfSecondFixedSurface"})
        if (u.keys.value(key) != v.keys.value(key))
            return false;
    return true;
}
int vector_partner(const Catalog& catalog, int u_index) {
    if (u_index < 0 || u_index >= int(catalog.fields.size()))
        throw Error("InvalidSelection", "U field index out of range");
    const auto& u = catalog.fields[size_t(u_index)];
    QString expected;
    if (u.short_name == "u")
        expected = "v";
    else if (u.short_name == "10u")
        expected = "10v";
    else if (u.short_name == "ugrd")
        expected = "vgrd";
    if (expected.isEmpty())
        throw Error("InvalidSelection", "Select a recognized U component");
    int partner = -1;
    for (const auto& v : catalog.fields)
        if (v.short_name == expected && vector_compatible(u, v)) {
            if (partner >= 0)
                throw Error(
                    "InvalidSelection",
                    "Ambiguous V components; duplicate products cannot be paired automatically");
            partner = v.index;
        }
    if (partner < 0)
        throw Error(
            "InvalidSelection",
            "No V component with the same grid, cycle, interval, level, member and provenance");
    if (!QStringList{"regular_ll", "regular_gg"}.contains(u.grid_type))
        throw Error("UnsupportedGrid",
                    "Vector rotation for this grid is not implemented; inspect scalar components");
    return partner;
}
Plane vector_magnitude(const Plane& u, const Plane& v) {
    if (!u.rectangular || !v.rectangular || u.rows != v.rows || u.cols != v.cols ||
        u.values.size() != v.values.size())
        throw Error("InvalidSelection", "Vector plane shapes differ");
    Plane result;
    result.rows = u.rows;
    result.cols = u.cols;
    result.rectangular = true;
    result.grid_type = u.grid_type;
    result.units = u.units;
    result.memory_lease = u.memory_lease;
    result.values.resize(u.values.size(), missing);
    for (size_t index = 0; index < u.values.size(); ++index)
        if (std::isfinite(u.values[index]) && std::isfinite(v.values[index]))
            result.values[index] = std::hypot(u.values[index], v.values[index]);
    return result;
}
Point vector_direction(const Field& field, double east, double north) {
    return {field.keys.value("iScansNegatively") == "1" ? -east : east,
            field.keys.value("jScansPositively") == "1" ? north : -north};
}
} // namespace grib
