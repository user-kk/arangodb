#pragma once

#include <velocypack/Builder.h>
#include <velocypack/Iterator.h>
#include <velocypack/Slice.h>
#include <cstddef>
#include <functional>
#include <numeric>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <xtensor/xarray.hpp>
#include <xtensor/xstorage.hpp>
#include "Assertions/Assert.h"
#include "Basics/Exceptions.h"
#include "Containers/SmallVector.h"
namespace arangodb {
namespace aql {
class Ndarray {
 public:
  enum ValueType { INT_TYPE = 0, FLOAT_TYPE = 1, DOUBLE_TYPE = 2, ERROR = 3 };

  Ndarray() = default;

  template<typename T>
  Ndarray(T initVal, std::vector<size_t> shape) {
    if constexpr (std::is_same_v<T, int>) {
      _type = INT_TYPE;
      _data.emplace<xt::xarray<int>>(xt::xarray<int>::shape_type(shape));
    } else if constexpr (std::is_same_v<T, float>) {
      _type = FLOAT_TYPE;
      _data.emplace<xt::xarray<float>>(xt::xarray<float>::shape_type(shape));
    } else if constexpr (std::is_same_v<T, double>) {
      _type = DOUBLE_TYPE;
      _data.emplace<xt::xarray<double>>(xt::xarray<double>::shape_type(shape));
    } else {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_INTERNAL,
                                     "This type is not allowed");
    }
    std::visit([initVal](auto& data) { data.fill(initVal); }, _data);
  }
  using DataType =
      std::variant<xt::xarray<int>, xt::xarray<float>, xt::xarray<double>>;

  auto shape() {
    return std::visit([](auto& data) { return data.shape(); }, _data);
  }

  void toVPack(VPackBuilder& builder) {
    // 断言当前一定被初始化了
    TRI_ASSERT(_type != ERROR);

    auto thisShape = shape();
    builder.openObject();
    builder.add("_type", "ndarray");
    // 轴名称
    builder.add("_axes", VPackValue(velocypack::ValueType::Array));
    for (auto& i : _axisNames) {
      builder.add(VPackValue(i));
    }
    builder.close();
    // shape
    builder.add("_shape", VPackValue(velocypack::ValueType::Array));
    for (auto& i : thisShape) {
      builder.add(VPackValue(i));
    }
    builder.close();
    // 种类
    builder.add("_valueType", valueTypeMap[_type]);
    // 值
    VPackBuilder arrayBuilder;
    std::visit(
        [&arrayBuilder](auto& view) {
          TRI_ASSERT(view.dimension() != 0);

          std::vector<size_t> strides;
          strides.reserve(view.dimension());
          size_t currentStride = 1;
          for (int i = view.dimension() - 1; i >= 0; --i) {
            currentStride *= view.shape(i);
            strides.push_back(currentStride);
          }
          size_t index = 0;
          for (auto i : view) {
            for (size_t j = 0; j < strides.size(); ++j) {
              if (index % strides[j] != 0) {
                break;
              }
              arrayBuilder.openArray();
            }

            arrayBuilder.add(VPackValue(i));
            index++;

            for (size_t j = 0; j < strides.size(); ++j) {
              if (index % strides[j] != 0) {
                break;
              }
              arrayBuilder.close();
            }
          }
        },
        _data);
    builder.add("_value", arrayBuilder.slice());

    builder.close();
  }

  static Ndarray* fromVpack(VPackSlice slice) {
    TRI_ASSERT(slice.isObject());
    TRI_ASSERT(slice.hasKey("_type") && slice["_type"].toString() == "ndarray");
    Ndarray* ret = new Ndarray();
    // 轴
    if (slice.hasKey("_axes")) {
      for (auto i : VPackArrayIterator(slice.get("_axes"))) {
        ret->_axisNames.push_back(i.toString());
      }
    }
    // 类型和形状
    std::string type = slice.get("_shape").toString();
    containers::SmallVector<size_t, 4> shape;
    for (auto i : VPackArrayIterator(slice.get("_shape"))) {
      shape.push_back(i.getInt());
    }
    size_t n =
        std::reduce(shape.begin(), shape.end(), 1, std::multiplies<size_t>());
    ret->init(type, n);
    // 值
    VPackSlice valueSlice = slice.get("_value");
    std::visit(
        [&valueSlice](auto data) {
          size_t i = 0;
          buildXarray(data, valueSlice, i);
        },
        ret->_data);
    // 重新塑形
    std::visit([&shape](auto data) { data.reshape(shape); }, ret->_data);
    return ret;
  }
  std::vector<std::string>& getAxisNames() { return _axisNames; }
  void setAxisNames(std::vector<std::string> names) {
    _axisNames = std::move(names);
  }
  size_t dimension() {
    return std::visit([](auto data) { return data.dimension(); }, _data);
  }
  template<typename T, typename Arg>
  void set(Arg indice, T val) {
    std::get<xt::xarray<T>>(_data)[std::forward<Arg>(indice)] = val;
  }
  bool operator==(const Ndarray& other) { return this->_data == other._data; }
  bool operator!=(const Ndarray& other) { return this->_data != other._data; }
  template<typename T>
  auto& get() {
    return std::get<xt::xarray<T>>(_data);
  }
  size_t getMemoryUsage() {
    size_t usage = 0;
    if (_type == INT_TYPE) {
      usage = get<int>().size() * sizeof(int);
    } else if (_type == DOUBLE_TYPE) {
      usage = get<double>().size() * sizeof(double);
    } else if (_type == FLOAT_TYPE) {
      usage = get<float>().size() * sizeof(float);
    }
    return usage + sizeof(*this);
  }

 private:
  ValueType _type = ERROR;
  DataType _data;
  std::vector<std::string> _axisNames;
  static constexpr std::array<std::string, 3> valueTypeMap = {"int", "float",
                                                              "double"};
  template<typename T>
  static void buildXarray(xt::xarray<T>& array, VPackSlice slice,
                          size_t& index) {
    if (!slice.isArray()) {
      return;
    }
    for (size_t i = 0; i < slice.length(); ++i) {
      VPackSlice sub = slice.at(i);
      if (sub.isArray()) {
        buildXarray(array, sub, index);
      } else {
        if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
          array[index++] = sub.getDouble();
        } else if constexpr (std::is_same_v<T, int>) {
          array[index++] = sub.getInt();
        }
      }
    }
  }
  void init(std::string_view type, size_t n) {
    if (type == "int") {
      _data.emplace<xt::xarray<int>>(xt::xarray<int>::shape_type({n}));
    } else if (type == "double") {
      _data.emplace<xt::xarray<double>>(xt::xarray<double>::shape_type({n}));
    } else if (type == "float") {
      _data.emplace<xt::xarray<float>>(xt::xarray<float>::shape_type({n}));
    }
  }
};
}  // namespace aql
}  // namespace arangodb