#include "color_map.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <limits>

namespace HdfColorMap {
namespace {
constexpr int max_bytes = 2 * 1024 * 1024;
uint32_t packed(const Rgba &color) {
  uint32_t channels[4];
  for(int channel=0;channel<4;++channel)
    channels[channel]=uint32_t(std::floor(std::clamp(color[channel],0.0,1.0)*255.0+0.5));
  return channels[3]<<24 | channels[0]<<16 | channels[1]<<8 | channels[2];
}
QString read_file(const QString &path, QByteArray &data) {
  QFile file(path);
  if(!file.open(QIODevice::ReadOnly)) return file.errorString();
  if(file.size()>max_bytes) return "Palette exceeds the 2 MiB limit.";
  data=file.readAll();
  return {};
}
void append_legacy(std::vector<Palette> &result) {
  const QStringList names={"Viridis","Plasma","Inferno","Turbo","Grayscale"};
  const std::vector<std::vector<std::array<int,3>>> colors={
    {{68,1,84},{59,82,139},{33,145,140},{94,201,98},{253,231,37}},
    {{13,8,135},{126,3,168},{204,71,120},{248,149,64},{240,249,33}},
    {{0,0,4},{87,15,109},{188,55,84},{249,142,9},{252,255,164}},
    {{48,18,59},{50,131,246},{35,213,112},{250,185,40},{122,4,3}},
    {{0,0,0},{255,255,255}}};
  for(int index=0;index<names.size();++index) {
    Palette palette;
    palette.id="legacy:"+names[index].toLower(); palette.name=names[index]+" (legacy)";
    palette.group="Legacy"; palette.kind="sequential";
    palette.source="HDFStudio 0.12.03 compatibility implementation";
    palette.revision="pre-colormap-20261121"; palette.license="See HDFStudio THIRD_PARTY_NOTICES";
    palette.modification="Original sparse control points retained for compatibility.";
    for(size_t item=0;item<colors[index].size();++item) {
      const auto &rgb=colors[index][item];
      palette.colors.push_back({rgb[0]/255.,rgb[1]/255.,rgb[2]/255.,1.});
      palette.positions.push_back(double(item)/(colors[index].size()-1));
    }
    result.push_back(std::move(palette));
  }
}
}
QString validate(const Palette &palette) {
  if(palette.id.isEmpty() || palette.name.isEmpty()) return "Palette ID/name is missing.";
  if(palette.colors.size()<2 || palette.colors.size()>4096) return "A palette needs 2..4096 colors.";
  if(palette.positions.size()!=palette.colors.size()) return "Color positions do not match the color count.";
  if(palette.positions.front()!=0 || palette.positions.back()!=1) return "Positions must cover 0..1.";
  for(size_t index=0;index<palette.colors.size();++index) {
    for(double value:palette.colors[index])
      if(!std::isfinite(value)||value<0||value>1) return "RGBA components must be finite values in 0..1.";
    if(!std::isfinite(palette.positions[index]) || (index && palette.positions[index]<=palette.positions[index-1]))
      return "Color positions must be finite and strictly increasing.";
  }
  return {};
}
QString validate(const Palette &palette, const Mapping &mapping) {
  auto error=validate(palette); if(!error.isEmpty()) return error;
  if(!std::isfinite(mapping.minimum)||!std::isfinite(mapping.maximum)||mapping.minimum>mapping.maximum)
    return "Invalid display range.";
  if(mapping.normalization==Normalization::Centered &&
     (!std::isfinite(mapping.center)||mapping.minimum>=mapping.center||mapping.center>=mapping.maximum))
    return "Center must lie strictly inside the display range.";
  if(mapping.normalization==Normalization::Cyclic &&
     (!std::isfinite(mapping.origin)||!std::isfinite(mapping.period)||mapping.period<=0||
      !std::isfinite(mapping.origin+mapping.period)||mapping.origin+mapping.period<=mapping.origin))
    return "Cycle needs a finite origin and a positive, representable period.";
  if(!mapping.boundaries.empty()) {
    if(mapping.mode!=Mode::Stepped || mapping.normalization!=Normalization::Linear)
      return "Custom boundaries require stepped, linear mapping.";
    if(mapping.boundaries.size()!=palette.colors.size()+1) return "K colors require K+1 boundaries.";
    for(size_t index=0;index<mapping.boundaries.size();++index)
      if(!std::isfinite(mapping.boundaries[index])||(index&&mapping.boundaries[index]<=mapping.boundaries[index-1]))
        return "Boundaries must be finite and strictly increasing.";
  }
  return {};
}
QString parse_json(const QByteArray &data, Palette &out) {
  if(data.size()>max_bytes) return "Palette exceeds the 2 MiB limit.";
  QJsonParseError parse_error;
  const auto document=QJsonDocument::fromJson(data,&parse_error);
  if(parse_error.error!=QJsonParseError::NoError || !document.isObject()) return "Invalid palette JSON.";
  const auto object=document.object();
  if(object.value("schema_version").toInt()!=1) return "Unsupported palette schema.";
  Palette palette;
  palette.id=object.value("id").toString(); palette.name=object.value("name").toString();
  palette.group=object.value("group").toString(); palette.kind=object.value("kind").toString();
  palette.source=object.value("source").toString(); palette.license=object.value("license").toString();
  palette.revision=object.value("revision").toString(); palette.source_hash=object.value("source_sha256").toString();
  palette.modification=object.value("modification").toString(); palette.stepped=object.value("stepped").toBool();
  for(const auto &author:object.value("authors").toArray()) palette.authors<<author.toString();
  for(const auto &row:object.value("colors").toArray()) {
    const auto rgba=row.toArray(); if(rgba.size()!=4) return "Expected RGBA rows.";
    Rgba color;
    for(int channel=0;channel<4;++channel) {
      if(!rgba[channel].isDouble()) return "RGBA components must be numeric.";
      color[channel]=rgba[channel].toDouble();
    }
    palette.colors.push_back(color);
  }
  for(const auto &value:object.value("positions").toArray()) {
    if(!value.isDouble()) return "Positions must be numeric.";
    palette.positions.push_back(value.toDouble());
  }
  const auto error=validate(palette); if(error.isEmpty()) out=std::move(palette);
  return error;
}
QByteArray to_json(const Palette &palette) {
  QJsonObject object{{"schema_version",1},{"id",palette.id},{"name",palette.name},
    {"group",palette.group},{"kind",palette.kind},{"source",palette.source},{"license",palette.license},
    {"revision",palette.revision},{"source_sha256",palette.source_hash},{"modification",palette.modification},
    {"stepped",palette.stepped},{"authors",QJsonArray::fromStringList(palette.authors)}};
  QJsonArray colors,positions;
  for(const auto &color:palette.colors) colors.append(QJsonArray{color[0],color[1],color[2],color[3]});
  for(double position:palette.positions) positions.append(position);
  object["colors"]=colors; object["positions"]=positions;
  return QJsonDocument(object).toJson();
}
QString parse_rgb(const QByteArray &data, RgbScale scale, Palette &out) {
  if(data.size()>max_bytes) return "RGB file exceeds the 2 MiB limit.";
  Palette palette; palette.id="import:"+QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());
  palette.name="Imported RGB"; palette.group="Imported"; palette.kind="custom";
  palette.license="User supplied; redistribution rights not verified";
  palette.source_hash=palette.id.mid(7);
  int declared=-1, line_number=0;
  double largest=0;
  const QRegularExpression header("^ncolors?\\s*=\\s*([0-9]+)$",QRegularExpression::CaseInsensitiveOption);
  for(QString line:QString::fromUtf8(data).split('\n')) {
    ++line_number; line.remove(QChar(0xfeff));
    int comment=line.indexOf('#'); if(comment>=0) line=line.left(comment);
    comment=line.indexOf(';'); if(comment>=0) line=line.left(comment);
    line=line.trimmed(); if(line.isEmpty()) continue;
    auto match=header.match(line);
    if(match.hasMatch()) {
      bool ok=false; int count=match.captured(1).toInt(&ok);
      if(!ok||declared>=0||count<2||count>4096||!palette.colors.empty()) return "Invalid or repeated ncolors header.";
      declared=count; continue;
    }
    auto tokens=line.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
    if(tokens.size()!=3) return QString("Line %1: expected three RGB numbers.").arg(line_number);
    Rgba color{0,0,0,1};
    for(int channel=0;channel<3;++channel) {
      bool ok=false; color[channel]=tokens[channel].toDouble(&ok);
      if(!ok||!std::isfinite(color[channel])||color[channel]<0||color[channel]>255)
        return QString("Line %1: RGB outside 0..255.").arg(line_number);
      largest=std::max(largest,color[channel]);
    }
    palette.colors.push_back(color);
    if(palette.colors.size()>4096) return "RGB file exceeds 4096 colors.";
  }
  if(palette.colors.size()<2 || (declared>=0&&size_t(declared)!=palette.colors.size())) return "RGB color count mismatch (at least two required).";
  if(scale==RgbScale::Auto && largest<=1) return "Ambiguous RGB scale: choose 0..1 or 0..255 explicitly.";
  if(scale==RgbScale::Unit && largest>1) return "RGB component exceeds the selected 0..1 range.";
  const double divisor=scale==RgbScale::Unit?1.:255.;
  for(size_t index=0;index<palette.colors.size();++index) {
    for(int channel=0;channel<3;++channel) palette.colors[index][channel]/=divisor;
    palette.positions.push_back(double(index)/(palette.colors.size()-1));
  }
  palette.modification=QString("Imported RGB; scale 0..%1; all rows retained").arg(divisor);
  out=std::move(palette); return {};
}
std::vector<Palette> load_registry(QString &error) {
  std::vector<Palette> result; append_legacy(result); error.clear();
  QByteArray data; auto problem=read_file(":/colormaps/manifest.json",data);
  if(!problem.isEmpty()) {error=problem;return result;}
  const auto manifest=QJsonDocument::fromJson(data).object();
  if(manifest.value("schema_version").toInt()!=1) {error="Invalid palette manifest.";return result;}
  for(const auto &entry:manifest.value("palettes").toArray()) {
    const auto item=entry.toObject(); Palette palette;
    problem=read_file(":/colormaps/"+item.value("file").toString(),data);
    if(problem.isEmpty() && QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex())!=item.value("sha256").toString())
      problem="Resource SHA256 mismatch.";
    if(problem.isEmpty()) problem=parse_json(data,palette);
    if(problem.isEmpty()) result.push_back(std::move(palette));
    else error+=item.value("id").toString()+": "+problem+"\n";
  }
  const QDir directory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/colormaps");
  for(const QString &file:directory.entryList({"*.json"},QDir::Files).mid(0,64)) {
    Palette palette;
    problem=read_file(directory.filePath(file),data);
    if(problem.isEmpty()) problem=parse_json(data,palette);
    if(problem.isEmpty()&&palette.id.startsWith("import:")) result.push_back(std::move(palette));
    else error+="Imported "+file+": "+problem+"\n";
  }
  return result;
}
QString import_rgb(const QString &filename, RgbScale scale, Palette &out) {
  QByteArray data; auto error=read_file(filename,data); if(!error.isEmpty()) return error;
  Palette palette; error=parse_rgb(data,scale,palette); if(!error.isEmpty()) return error;
  palette.name=QFileInfo(filename).completeBaseName(); palette.source=QFileInfo(filename).absoluteFilePath();
  const QString directory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/colormaps";
  if(!QDir().mkpath(directory)) return "Cannot create user palette directory.";
  const QString id=palette.source_hash+ (scale==RgbScale::Unit?"-unit":"-byte");
  palette.id="import:"+id;
  QSaveFile destination(directory+"/"+id+".json");
  if(!destination.open(QIODevice::WriteOnly)) return destination.errorString();
  const auto encoded=to_json(palette);
  if(destination.write(encoded)!=encoded.size()||!destination.commit()) return destination.errorString();
  out=std::move(palette);return {};
}
double fraction(double value,const Mapping &mapping) {
  if(!std::isfinite(value)) return .5;
  // MSVC long double has double precision/range. Scale before subtraction to
  // handle finite ranges such as [-DBL_MAX, DBL_MAX] without overflow.
  auto linear=[](double number,double low,double high) {
    if(number<=low) return 0.;
    if(number>=high) return 1.;
    const double scale=std::max(std::abs(low),std::abs(high));
    const double span=high-low;
    if(std::isfinite(span)) return (number-low)/span;
    return (number/scale-low/scale)/(high/scale-low/scale);
  };
  long double result=.5;
  if(mapping.normalization==Normalization::Cyclic) {
    double first=std::fmod(value,mapping.period)/mapping.period;
    double second=std::fmod(mapping.origin,mapping.period)/mapping.period;
    if(first<0) first+=1;
    if(second<0) second+=1;
    result=first-second;
    if(result<0) result+=1;
  } else if(mapping.normalization==Normalization::Centered) {
    if(value<=mapping.center) result=.5L*linear(value,mapping.minimum,mapping.center);
    else result=.5L+.5L*linear(value,mapping.center,mapping.maximum);
  } else if(mapping.maximum>mapping.minimum)
    result=linear(value,mapping.minimum,mapping.maximum);
  return double(std::clamp(result,0.L,1.L));
}
double value_at(double position,const Mapping &mapping) {
  const long double value=std::clamp(position,0.,1.);
  if(mapping.normalization==Normalization::Cyclic) return double(mapping.origin+value*mapping.period);
  if(mapping.normalization==Normalization::Centered) {
    if(value<=.5) return double((1-2*value)*mapping.minimum+2*value*mapping.center);
    return double((2-2*value)*mapping.center+(2*value-1)*mapping.maximum);
  }
  return double((1-value)*mapping.minimum+value*mapping.maximum);
}
uint32_t sample(const Palette &palette,double position,bool reversed,bool stepped) {
  if(palette.colors.size()<2) return missing_color;
  double value=std::clamp(std::isfinite(position)?position:.5,0.,1.);
  if(stepped) {
    size_t index=std::min(size_t(value*palette.colors.size()),palette.colors.size()-1);
    if(reversed) index=palette.colors.size()-1-index;
    return packed(palette.colors[index]);
  }
  if(reversed) value=1-value;
  auto upper=std::upper_bound(palette.positions.begin(),palette.positions.end(),value);
  size_t right=std::clamp(size_t(upper-palette.positions.begin()),size_t(1),palette.colors.size()-1);
  const double local=(value-palette.positions[right-1])/(palette.positions[right]-palette.positions[right-1]);
  Rgba color;
  for(int channel=0;channel<4;++channel)
    color[channel]=palette.colors[right-1][channel]+local*(palette.colors[right][channel]-palette.colors[right-1][channel]);
  return packed(color);
}
uint32_t map_value(const Palette &palette,const Mapping &mapping,double value,bool masked) {
  if(masked) return masked_color;
  if(!std::isfinite(value)) return missing_color;
  double low=mapping.minimum, high=mapping.maximum;
  if(!mapping.boundaries.empty()) {low=mapping.boundaries.front();high=mapping.boundaries.back();}
  if(mapping.normalization!=Normalization::Cyclic) {
    if(value<low&&mapping.custom_under) return mapping.under;
    if(value>high&&mapping.custom_over) return mapping.over;
  }
  if(!mapping.boundaries.empty()) {
    auto upper=std::upper_bound(mapping.boundaries.begin(),mapping.boundaries.end(),value);
    size_t index=upper==mapping.boundaries.begin()?0:size_t(upper-mapping.boundaries.begin()-1);
    index=std::min(index,palette.colors.size()-1);
    if(mapping.reversed) index=palette.colors.size()-1-index;
    return packed(palette.colors[index]);
  }
  return sample(palette,fraction(value,mapping),mapping.reversed,mapping.mode==Mode::Stepped);
}
}
