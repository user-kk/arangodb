#pragma once

#include <velocypack/Builder.h>
#include <velocypack/Iterator.h>
#include <velocypack/Slice.h>
#include <velocypack/Value.h>
#include <velocypack/ValueType.h>
#include <cstddef>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <xtensor/xarray.hpp>
#include <xtensor/xjson.hpp>
#include <xtensor/xstorage.hpp>
#include "Assertions/Assert.h"
#include "Basics/Exceptions.h"
#include "Containers/SmallVector.h"
#include "Basics/Exceptions.h"
#include <xtensor-blas/xlinalg.hpp>
#include <xtensor/xmanipulation.hpp>
#include <xtensor/xstrides.hpp>
#include <xtensor/xindex_view.hpp>
namespace arangodb {
namespace aql {
class NdarrayOperator;
class NdarrayOperatorDisableSIMD;

template<typename T>
struct is_xarray : std::false_type {};

template<typename T>
struct is_xarray<xt::xarray<T>> : std::true_type {};

template<typename T, typename... Args>
struct is_one_of {
  static constexpr bool value = (std::is_same<T, Args>::value || ...);
};

class Ndarray {
 public:
  friend NdarrayOperator;
  friend NdarrayOperatorDisableSIMD;
  enum ValueType { INT_TYPE = 0, FLOAT_TYPE = 1, DOUBLE_TYPE = 2, ERROR = 3 };
  using AxisNameType = std::vector<std::optional<std::string>>;

  Ndarray() = default;
  Ndarray(const Ndarray& array) : _type(array._type), _data(array._data) {}
  ~Ndarray() = default;

  template<typename T>
    requires is_one_of<T, int, double, float>::value
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
    }
    std::visit([initVal](auto& data) { data.fill(initVal); }, _data);
  }
  using DataType =
      std::variant<xt::xarray<int>, xt::xarray<float>, xt::xarray<double>>;

  template<typename T>
    requires is_one_of<T, int, double, float>::value
  auto& get() {
    return std::get<xt::xarray<T>>(_data);
  }

  template<typename T>
    requires is_one_of<T, int, double, float>::value
  const auto& get() const {
    return std::get<xt::xarray<T>>(_data);
  }

  bool empty() const {
    return std::visit([](auto data) { return data.begin() == data.end(); },
                      _data);
  }
  auto shape() const {
    return std::visit([](auto& data) { return data.shape(); }, _data);
  }

  Ndarray* clone() const {
    Ndarray* ret = new Ndarray;
    ret->_axisNames = _axisNames;
    ret->_data = _data;
    ret->_type = _type;
    return ret;
  }

  ///@brief 枚举Ndarray的每一个元素
  void getElemVpack(size_t index, VPackBuilder& builder,
                    const AxisNameType& aliasName) const {
    builder.openObject();
    TRI_ASSERT(index < size());
    auto indice = xt::unravel_index(index, shape());
    size_t n = dimension();

    for (size_t i = 0; i < n; i++) {
      if (checkAxisNameValid(aliasName, i + 1)) {
        builder.add(aliasName[i + 1].value(), VPackValue(indice[i]));
      } else {
        if (checkAxisNameValid(_axisNames, i)) {
          builder.add(_axisNames[i].value(), VPackValue(indice[i]));
        } else {
          builder.add("_axis" + std::to_string(i), VPackValue(indice[i]));
        }
      }
    }
    std::variant<int, double> value = flat(index);
    if (value.index() == 0) {
      if (checkAxisNameValid(aliasName, 0)) {
        builder.add(aliasName[0].value(), VPackValue(std::get<0>(value)));
      } else {
        builder.add("_val", VPackValue(std::get<0>(value)));
      }

    } else {
      if (checkAxisNameValid(aliasName, 0)) {
        builder.add(aliasName[0].value(), VPackValue(std::get<1>(value)));
      } else {
        builder.add("_val", VPackValue(std::get<1>(value)));
      }
    }
    builder.close();
  }

  std::variant<int, double> flat(size_t i) const {
    if (_type == INT_TYPE) {
      return get<int>().flat(i);
    } else if (_type == FLOAT_TYPE) {
      return get<float>().flat(i);
    } else {
      return get<double>().flat(i);
    }
  }

  size_t size() const {
    return std::visit([](auto& data) { return data.size(); }, _data);
  }

  ///@brief axisname->轴的index
  int axisIndexFromName(std::string_view name) const {
    for (size_t i = 0; i < _axisNames.size(); i++) {
      if (_axisNames[i].has_value() && _axisNames[i].value() == name) {
        return i;
      }
    }
    return -1;
  }

  ///@brief ndarray -> vpack
  void toVPack(VPackBuilder& builder) const {
    // 断言当前一定被初始化了
    TRI_ASSERT(_type != ERROR);

    if (this->dimension() == 0) {
      switch (_type) {
        case INT_TYPE: {
          builder.add(VPackValue(this->get<int>()[{}]));
          break;
        }
        case FLOAT_TYPE: {
          builder.add(VPackValue(this->get<float>()[{}]));
          break;
        }
        case DOUBLE_TYPE: {
          builder.add(VPackValue(this->get<double>()[{}]));
          break;
        }
        case ERROR: {
          builder.add(VPackValue(VPackValueType::Null));
          break;
        }
      }
      return;
    }

    auto thisShape = shape();
    builder.openObject();
    builder.add("_type", "ndarray");
    // 轴名称 轴可能为空,轴可能是null和int间隔的序列
    builder.add("_axes", VPackValue(velocypack::ValueType::Array));
    for (size_t i = 0; i < _axisNames.size(); i++) {
      if (_axisNames[i] == std::nullopt) {
        builder.add(VPackValue(VPackValueType::Null));
      } else {
        builder.add(VPackValue(_axisNames[i].value()));
      }
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

  Ndarray* filter(const Ndarray* v) const {
    TRI_ASSERT(v->_type == INT_TYPE);
    TRI_ASSERT(_type != ERROR);
    Ndarray* ret = new Ndarray;
    if (_type == INT_TYPE) {
      ret->_data.emplace<xt::xarray<int>>(
          xt::filter(get<int>(), v->get<int>()));
    } else if (_type == DOUBLE_TYPE) {
      ret->_data.emplace<xt::xarray<double>>(
          xt::filter(get<double>(), v->get<int>()));
    } else {
      ret->_data.emplace<xt::xarray<float>>(
          xt::filter(get<float>(), v->get<int>()));
    }
    ret->_type = _type;
    return ret;
  }
  ValueType getValueType() const { return _type; }

  static bool checkIsNdarray(VPackSlice slice) {
    if (!slice.isObject()) {
      return false;
    }
    if (!slice.hasKey("_type")) {
      return false;
    }
    if (slice["_type"].toString() != "ndarray") {
      return false;
    }

    return true;
  }

  static bool NdarrayIsValid(VPackSlice slice) {
    if (!slice.hasKey("_shape") || !slice.hasKey("_valueType") ||
        !slice.hasKey("_value")) {
      return false;
    }
    return true;
  }

  ///@brief vpack -> ndarray
  static Ndarray* fromVpack(VPackSlice slice) {
    TRI_ASSERT(slice.isObject());
    TRI_ASSERT(slice.hasKey("_type") && slice["_type"].toString() == "ndarray");
    Ndarray* ret = new Ndarray();
    // 轴
    if (slice.hasKey("_axes")) {
      for (auto i : VPackArrayIterator(slice.get("_axes"))) {
        if (i.isString()) {
          ret->_axisNames.push_back(i.toString());
        } else {
          ret->_axisNames.push_back(std::nullopt);
        }
      }
    }
    // 类型和形状
    std::string type = slice.get("_valueType").toString();
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
        [&valueSlice](auto& data) {
          size_t i = 0;
          buildXarray(data, valueSlice, i);
        },
        ret->_data);
    // 重新塑形
    std::visit([&shape](auto& data) { data.reshape(shape); }, ret->_data);
    return ret;
  }

  ///@brief 普通数组转Ndarray
  template<typename T>
  static Ndarray* fromVPackArray(VPackSlice arraySlice,
                                 const AxisNameType& axisNames,
                                 const std::vector<int>& shape) {
    Ndarray* ret = new Ndarray;
    xt::xarray<T> array;
    xt::from_json(nlohmann::json::parse(arraySlice.toJson()), array);
    if (!shape.empty()) {
      array.reshape(shape);
    }
    ret->_data = std::move(array);
    ret->setType<T>();
    if (!axisNames.empty()) {
      AxisNameType names(ret->dimension(), std::nullopt);
      for (size_t i = 0; i < axisNames.size(); i++) {
        if (i >= names.size()) {
          break;
        }
        names[i] = axisNames[i];
      }
      ret->setAxisNames(names);
    }

    return ret;
  }

  ///@brief 进行类型与轴重命名时使用
  template<typename T>
    requires is_one_of<T, int, double, float>::value
  static Ndarray* fromOtherNdarray(const Ndarray* other,
                                   const AxisNameType& axisNames,
                                   const std::vector<int>& shape) {
    if (axisNames.size() != 0 && shape.size() != 0 &&
        axisNames.size() != shape.size()) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                     "error: axisNames size != shape size");
    }
    Ndarray* ret = new Ndarray;
    xt::xarray<T> array;
    std::visit([&array](auto data) { array = data; }, other->_data);

    if (!shape.empty()) {
      array.reshape(shape);
    }
    ret->_data = std::move(array);
    ret->setType<T>();

    if (!axisNames.empty()) {
      ret->setAxisNames(axisNames);
    } else {
      // 当不修改或修改后维度数不变的时候,继承原来的轴名称
      if (shape.empty() || shape.size() == other->size()) {
        ret->_axisNames = other->_axisNames;
      }
    }

    return ret;
  }

  ///@brief 进行类型与轴重命名时使用
  template<typename T>
    requires is_one_of<T, int, double, float>::value
  static Ndarray* fromOtherVPack(VPackSlice slice,
                                 const AxisNameType& axisNames,
                                 const std::vector<int>& shape) {
    TRI_ASSERT(slice.isObject());
    TRI_ASSERT(slice.hasKey("_type") && slice["_type"].toString() == "ndarray");
    if (axisNames.size() != 0 && shape.size() != 0 &&
        axisNames.size() != shape.size()) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                     "error: axisNames size != shape size");
    }
    Ndarray* ret = new Ndarray();

    // otherVpack中的shape
    containers::SmallVector<size_t, 4> otherShape;
    for (auto i : VPackArrayIterator(slice.get("_shape"))) {
      otherShape.push_back(i.getInt());
    }

    // 轴
    if (!axisNames.empty()) {
      ret->setAxisNames(axisNames);
    } else {
      // 当不修改或修改后维度数不变的时候,继承原来的轴名称
      if (shape.empty() || shape.size() == otherShape.size()) {
        if (slice.hasKey("_axes")) {
          for (auto i : VPackArrayIterator(slice.get("_axes"))) {
            if (i.isString()) {
              ret->_axisNames.push_back(i.toString());
            } else {
              ret->_axisNames.push_back(std::nullopt);
            }
          }
        }
      }
    }
    // 类型和形状
    size_t n = std::reduce(otherShape.begin(), otherShape.end(), 1,
                           std::multiplies<size_t>());
    ret->init<T>(n);
    // 值
    VPackSlice valueSlice = slice.get("_value");
    std::visit(
        [&valueSlice](auto& data) {
          size_t i = 0;
          buildXarray(data, valueSlice, i);
        },
        ret->_data);
    if (!shape.empty()) {
      // 重新塑形
      std::visit([&shape](auto& data) { data.reshape(shape); }, ret->_data);
    } else {
      std::visit([&otherShape](auto& data) { data.reshape(otherShape); },
                 ret->_data);
    }

    return ret;
  }

  const AxisNameType& getAxisNames() const { return _axisNames; }

  void setAxisNames(AxisNameType names) { _axisNames = std::move(names); }

  size_t dimension() const {
    return std::visit([](auto& data) { return data.dimension(); }, _data);
  }

  bool operator==(const Ndarray& other) const {
    return this->_data == other._data;
  }
  bool operator!=(const Ndarray& other) const {
    return this->_data != other._data;
  }

  size_t getMemoryUsage() const {
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

  std::string getNdarrayData() {
    if (_type == INT_TYPE) {
      std::stringstream stream;
      stream << get<int>();
      return stream.str();
    } else if (_type == DOUBLE_TYPE) {
      std::stringstream stream;
      stream << get<double>();
      return stream.str();
    } else if (_type == FLOAT_TYPE) {
      std::stringstream stream;
      stream << get<float>();
      return stream.str();

    } else {
      return "error";
    }
  }

 private:
  ValueType _type = ERROR;
  DataType _data;
  AxisNameType _axisNames;
  static constexpr std::array<std::string, 3> valueTypeMap = {"int", "float",
                                                              "double"};

  template<typename T>
  void setType() {
    // 设置类型
    if constexpr (std::is_same_v<T, double>) {
      _type = DOUBLE_TYPE;
    } else if constexpr (std::is_same_v<T, int>) {
      _type = INT_TYPE;
    } else if constexpr (std::is_same_v<T, float>) {
      _type = FLOAT_TYPE;
    } else {
      _type = ERROR;
    }
  }
  bool checkAxisNameValid(const AxisNameType& names, size_t i) const {
    if (i >= names.size()) {
      return false;
    }

    if (!names[i].has_value()) {
      return false;
    }
    return true;
  }

  template<typename T>
    requires is_one_of<T, int, double, float>::value
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
      _type = INT_TYPE;
    } else if (type == "double") {
      _data.emplace<xt::xarray<double>>(xt::xarray<double>::shape_type({n}));
      _type = DOUBLE_TYPE;
    } else if (type == "float") {
      _data.emplace<xt::xarray<float>>(xt::xarray<float>::shape_type({n}));
      _type = FLOAT_TYPE;
    }
  }
  template<typename T>
    requires is_one_of<T, int, double, float>::value
  void init(size_t n) {
    _data.emplace<xt::xarray<T>>(typename xt::xarray<T>::shape_type({n}));
    setType<T>();
  }
};
inline const Ndarray* getNdarrayPtr(
    const std::variant<const Ndarray*, std::unique_ptr<const Ndarray>>& ptr) {
  if (ptr.index() == 0) {
    return std::get<0>(ptr);
  } else {
    return std::get<1>(ptr).get();
  }
}
}  // namespace aql
}  // namespace arangodb