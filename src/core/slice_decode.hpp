// 存储类型域内识别缺测，之后转换为float64。仅由读取器使用。
#pragma once
#include <vector>
#include <cmath>
#include <limits>
#include <type_traits>

template<class T> void slice_decode_values(const std::vector<T> &raw, bool has_fill,T fill,
    bool has_missing,T missing,double scale,double offset,std::vector<double> &out,bool &warning) {
  out.resize(raw.size());
  for(size_t index=0;index<raw.size();++index) {
    const T value=raw[index];
    const bool absent=(has_fill && (value==fill || (std::is_floating_point<T>::value && std::isnan(double(fill)) && std::isnan(double(value))))) ||
      (has_missing && (value==missing || (std::is_floating_point<T>::value && std::isnan(double(missing)) && std::isnan(double(value))))) || !std::isfinite(double(value));
    if(absent) {out[index]=NAN;continue;}
    if constexpr(std::is_integral<T>::value && sizeof(T)>=8) {
      // Count significant bits after removing trailing zeros: some large integers remain exact.
      using Unsigned=typename std::make_unsigned<T>::type;
      Unsigned magnitude;
      if constexpr(std::is_signed<T>::value) magnitude=value<0?Unsigned(-(value+1))+1:Unsigned(value);
      else magnitude=value;
      if(magnitude){while((magnitude&1)==0)magnitude>>=1;unsigned bits=0;while(magnitude){++bits;magnitude>>=1;}if(bits>53)warning=true;}
    }
    const double decoded=double(value)*scale+offset;
    out[index]=std::isfinite(decoded)?decoded:NAN;
  }
}
