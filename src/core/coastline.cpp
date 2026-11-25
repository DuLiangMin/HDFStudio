#include "coastline.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <cmath>
namespace Coastline {
QByteArray bundled_hash(){return HDFSTUDIO_COASTLINE_SHA256;}
Data load(const QString &filename,const QByteArray &expected_hash){Data out;QFile file(filename);
  if(!file.open(QIODevice::ReadOnly)||file.size()>16*1024*1024){out.error="Coastline resource missing or excessive size";return out;}
  const auto bytes=file.readAll();const auto hash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
  if(expected_hash.isEmpty()||hash!=expected_hash){out.error="Coastline SHA256 mismatch";return out;}
  const auto root=QJsonDocument::fromJson(bytes).object();
  if(root["schema_version"]!=1||root["resolution"]!="l"||root["coordinate_order"]!="lon,lat"||root["units"]!="degree"||root["area_threshold_km2"]!=1000||root["prime_meridian"]!="Greenwich"){out.error="Unsupported coastline schema";return out;}
  size_t count=0;for(const auto &fv:root["features"].toArray()){const auto feature=fv.toObject();const int level=feature["level"].toInt();if((level!=1&&level!=5)||feature["area_km2"].toDouble()<=1000){out.error="Invalid coastline selection";break;}
    for(const auto &pv:feature["parts"].toArray()){std::vector<QPointF> part;for(const auto &cv:pv.toObject()["coordinates"].toArray()){const auto pair=cv.toArray();const double lon=pair.size()==2?pair[0].toDouble(NAN):NAN,lat=pair.size()==2?pair[1].toDouble(NAN):NAN;
        if(++count>200000||!std::isfinite(lon)||!std::isfinite(lat)||std::abs(lon)>180||std::abs(lat)>90){out.error="Invalid/excessive coastline vertices";break;}part.emplace_back(lon,lat);}
      if(!out.error.isEmpty())break;if(part.size()<2){out.error="Short coastline part";break;}out.parts.push_back(std::move(part));}if(!out.error.isEmpty())break;}
  if(out.parts.empty()&&out.error.isEmpty())out.error="Empty coastline asset";if(!out.error.isEmpty())out.parts.clear();out.revision=QString::fromLatin1(hash);return out;
}
}
