#include "raster_budget.hpp"
#include <QProcessEnvironment>
#include <atomic>
#include <limits>
namespace {
std::atomic<quint64> raster_reserved_bytes{0};
struct RasterReservation {
  quint64 bytes;
  explicit RasterReservation(quint64 count):bytes(count) {}
  ~RasterReservation() {raster_reserved_bytes.fetch_sub(bytes);}
};
}
std::shared_ptr<void> reserve_raster(quint64 pixels) {
  bool valid=false;
  const quint64 configured=qEnvironmentVariable("HDFSTUDIO_RASTER_BUDGET_MIB").toULongLong(&valid);
  const quint64 budget=(valid&&configured>=1&&configured<=16384?configured:1024)*1024*1024;
  if(pixels>std::numeric_limits<quint64>::max()/64) return {};
  const quint64 bytes=pixels*64;
  auto used=raster_reserved_bytes.load();
  do {if(bytes>budget||used>budget-bytes) return {};}
  while(!raster_reserved_bytes.compare_exchange_weak(used,used+bytes));
  return std::make_shared<RasterReservation>(bytes);
}
