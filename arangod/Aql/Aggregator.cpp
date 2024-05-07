////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2024 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Business Source License 1.1 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     https://github.com/arangodb/arangodb/blob/devel/LICENSE
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#include "Aggregator.h"

#include "Aql/AqlValue.h"
#include "Aql/AqlValueMaterializer.h"
#include "Aql/Functions.h"
#include "Aql/Ndarray.hpp"
#include "Aql/Parser.h"
#include "Assertions/Assert.h"
#include "Basics/Exceptions.h"
#include "Containers/FlatHashSet.h"
#include "Basics/VelocyPackHelper.h"
#include "Transaction/Context.h"
#include "Transaction/Helpers.h"
#include "Transaction/Methods.h"

#include <velocypack/Builder.h>
#include <velocypack/Iterator.h>
#include <velocypack/Slice.h>
#include <velocypack/Value.h>
#include <velocypack/ValueType.h>

#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>
#include <xtensor/xarray.hpp>
#include <xtensor/xjson.hpp>

using namespace arangodb;
using namespace arangodb::aql;
using namespace arangodb::basics;

namespace {

constexpr bool doesRequireInput = true;
constexpr bool doesNotRequireInput = false;

constexpr bool official = true;
constexpr bool internalOnly = false;

/// @brief struct containing aggregator meta information
struct AggregatorInfo {
  /// @brief factory to create a new aggregator instance in a query
  /// Note: this is a shared_ptr because a unique_ptr cannot be initialized via
  /// initializer list
  std::shared_ptr<Aggregator::Factory> factory;

  /// @brief whether or not the aggregator needs input
  /// note: currently this is false only for LENGTH/COUNT, for which the input
  /// is optimized away
  bool requiresInput;

  /// @brief whether or not the aggregator is part of the public API (i.e.
  /// callable from a user AQL query)
  bool isOfficialAggregator;

  /// @brief under which name this aggregator is pushed on a DB server. if left
  /// empty, the aggregator will only work on a coordinator and not be pushed
  /// onto a DB server for example, the SUM aggregator is cumutative, so it can
  /// be pushed executed on partial results on the DB server as SUM too however,
  /// the LENGTH aggregator needs to be pushed as LENGTH to the DB server, but
  /// the partial lengths need to be aggregated on the coordinator using SUM
  std::string_view pushToDBServerAs;

  /// @brief under which name this aggregator is executed on a coordinator to
  /// aggregate partial results from the DB servers if parts of the aggregation
  /// were pushed to the DB servers for example, the LENGTH aggregator will be
  /// pushed to the DB server as LENGTH, but on the coordinator the aggregator
  /// needs to be converted to SUM to sum up the partial lengths from the DB
  /// servers
  std::string_view runOnCoordinatorAs;
};

/// @brief helper class for block-wise memory allocations
class MemoryBlockAllocator {
 public:
  /// @brief create a temporary storage instance
  explicit MemoryBlockAllocator(size_t blockSize)
      : blockSize(blockSize), current(nullptr), end(nullptr) {}

  /// @brief destroy a temporary storage instance
  ~MemoryBlockAllocator() { clear(); }

  void clear() noexcept {
    for (auto& it : blocks) {
      delete[] it;
    }
    blocks.clear();
    current = nullptr;
    end = nullptr;
  }

  /// @brief register a short data value
  char* store(char const* p, size_t length) {
    if (current == nullptr || (current + length > end)) {
      allocateBlock(length);
    }

    TRI_ASSERT(!blocks.empty());
    TRI_ASSERT(current != nullptr);
    TRI_ASSERT(end != nullptr);
    TRI_ASSERT(current + length <= end);

    char* position = current;
    memcpy(static_cast<void*>(position), p, length);
    current += length;
    return position;
  }

 private:
  /// @brief allocate a new block of memory
  void allocateBlock(size_t minLength) {
    size_t length = std::max(minLength, blockSize);
    char* buffer = new char[length];

    try {
      blocks.emplace_back(buffer);
    } catch (...) {
      delete[] buffer;
      throw;
    }
    current = buffer;
    end = current + length;
  }

  /// @brief already allocated blocks
  std::vector<char*> blocks;

  /// @brief size of each block
  size_t const blockSize;

  /// @brief offset into current block
  char* current;

  /// @brief end of current block
  char* end;
};

/// @brief aggregator for LENGTH()
struct AggregatorLength final : public Aggregator {
  explicit AggregatorLength(velocypack::Options const* opts)
      : Aggregator(opts), count(0) {}

  void reset() override { count = 0; }

  void reduce(VPackFunctionParametersView) override { ++count; }

  AqlValue get() const override {
    uint64_t value = count;
    return AqlValue(AqlValueHintUInt(value));
  }

  uint64_t count;
};

struct AggregatorMin final : public Aggregator {
  explicit AggregatorMin(velocypack::Options const* opts)
      : Aggregator(opts), value() {}

  ~AggregatorMin() { value.destroy(); }

  void reset() override { value.erase(); }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    if (!cmpValue.isNull(true) &&
        (value.isEmpty() ||
         AqlValue::Compare(_vpackOptions, value, cmpValue, true) > 0)) {
      // the value `null` itself will not be used in MIN() to compare lower than
      // e.g. value `false`
      value.destroy();
      value = cmpValue.clone();
    }
  }

  AqlValue get() const override {
    if (value.isEmpty()) {
      return AqlValue(AqlValueHintNull());
    }
    return value.clone();
  }

  AqlValue value;
};

struct AggregatorMinWith final : public AggregatorNeedDynamicMemory {
  explicit AggregatorMinWith(velocypack::Options const* opts)
      : AggregatorNeedDynamicMemory(opts), minValue() {}

  ~AggregatorMinWith() {
    AggregatorNeedDynamicMemory::clear();
    minValue.destroy();
    destoryMinWithValue();
  }

  void reset() override {
    AggregatorNeedDynamicMemory::clear();
    minValue.erase();
    destoryMinWithValue();
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValue const& withValue = extractFunctionParameterValue(parameters, 1);
    if (cmpValue.isNull(true)) {
      return;
    }
    if (minValue.isEmpty()) {
      minValue.destroy();
      minValue = cmpValue.clone();
      destoryMinWithValue();
      minWithValue.push_back(withValue.clone());
    } else {
      int ret = AqlValue::Compare(_vpackOptions, minValue, cmpValue, true);
      if (ret > 0) {
        minValue.destroy();
        minValue = cmpValue.clone();
        destoryMinWithValue();
        minWithValue.push_back(withValue.clone());
      } else if (ret == 0) {
        minWithValue.push_back(withValue.clone());
      }
    }
  }

  AqlValue get() const override {
    if (minWithValue.empty()) {
      return AqlValue(AqlValueHintNull());
    }
    VPackBuilder builder;
    size_t previousSize = builder.buffer()->size();
    builder.openArray();
    for (auto& i : minWithValue) {
      i.toVelocyPack(_vpackOption, builder, false);
    }
    builder.close();
    _memoryUsage += builder.buffer()->size() - previousSize;
    return AqlValue(builder.slice(), builder.size());
  }

  AqlValue minValue;
  std::vector<AqlValue> minWithValue;
  void destoryMinWithValue() {
    for (auto& i : minWithValue) {
      i.destroy();
    }
    minWithValue.clear();
  }
};

struct AggregatorMaxWith final : public AggregatorNeedDynamicMemory {
  explicit AggregatorMaxWith(velocypack::Options const* opts)
      : AggregatorNeedDynamicMemory(opts), maxValue() {}

  ~AggregatorMaxWith() {
    AggregatorNeedDynamicMemory::clear();
    maxValue.destroy();
    destoryMinWithValue();
  }

  void reset() override {
    AggregatorNeedDynamicMemory::clear();
    maxValue.erase();
    destoryMinWithValue();
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValue const& withValue = extractFunctionParameterValue(parameters, 1);
    if (cmpValue.isNull(true)) {
      return;
    }
    if (maxValue.isEmpty()) {
      maxValue.destroy();
      maxValue = cmpValue.clone();
      destoryMinWithValue();
      maxWithValue.push_back(withValue.clone());
    } else {
      int ret = AqlValue::Compare(_vpackOption, maxValue, cmpValue, true);
      if (ret < 0) {
        maxValue.destroy();
        maxValue = cmpValue.clone();
        destoryMinWithValue();
        maxWithValue.push_back(withValue.clone());
      } else if (ret == 0) {
        maxWithValue.push_back(withValue.clone());
      }
    }
  }

  AqlValue get() const override {
    if (maxWithValue.empty()) {
      return AqlValue(AqlValueHintNull());
    }
    VPackBuilder builder;
    size_t previousSize = builder.buffer()->size();
    builder.openArray();
    for (auto& i : maxWithValue) {
      i.toVelocyPack(nullptr, builder, true);
    }
    builder.close();
    _memoryUsage += builder.buffer()->size() - previousSize;
    return AqlValue(builder.slice(), builder.size());
  }

  AqlValue maxValue;
  std::vector<AqlValue> maxWithValue;
  void destoryMinWithValue() {
    for (auto& i : maxWithValue) {
      i.destroy();
    }
    maxWithValue.clear();
  }
};

struct AggregatorMinN final : public AggregatorNeedDynamicMemory {
  explicit AggregatorMinN(velocypack::Options const* opts)
      : AggregatorNeedDynamicMemory(opts) {}

  ~AggregatorMinN() {
    AggregatorNeedDynamicMemory::clear();
    destorySet();
  }

  void reset() override {
    AggregatorNeedDynamicMemory::clear();
    destorySet();
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValue const& nValue = extractFunctionParameterValue(parameters, 1);

    if (cmpValue.isNull(true) || !nValue.isNumber() || nValue.toInt64() <= 0) {
      return;
    }
    size_t n = static_cast<size_t>(nValue.toInt64());
    if (minSet.size() >= n) {
      if (this->cmp(cmpValue, *minSet.rbegin())) {  // it < set中最大的数
        minSet.insert(cmpValue.clone());
        auto i = --minSet.end();
        AqlValue v = (*i);  // 得到要被删除的元素
        minSet.erase(i);
        v.destroy();  // 销毁AqlValue指向的内存
      }
    } else {
      minSet.insert(cmpValue.clone());
    }
  }

  AqlValue get() const override {
    if (minSet.empty()) {
      return AqlValue(AqlValueHintNull());
    }
    VPackBuilder builder;
    size_t previousSize = builder.buffer()->size();
    builder.openArray();
    for (auto& i : minSet) {
      i.toVelocyPack(_vpackOption, builder, false);
    }
    builder.close();
    _memoryUsage += builder.buffer()->size() - previousSize;
    return AqlValue(builder.slice(), builder.size());
  }
  void destorySet() {
    std::vector<AqlValue> values(minSet.begin(), minSet.end());
    minSet.clear();
    for (auto& i : values) {
      i.destroy();
    }
  }

  std::function<bool(const AqlValue&, const AqlValue&)> cmp{
      [this](const AqlValue& lhs, const AqlValue& rhs) {
        return AqlValue::Compare(_vpackOptions, lhs, rhs, true) < 0;
      }};
  std::multiset<AqlValue, std::function<bool(const AqlValue&, const AqlValue&)>>
      minSet{cmp};
};

struct AggregatorMaxN final : public AggregatorNeedDynamicMemory {
  explicit AggregatorMaxN(velocypack::Options const* opts)
      : AggregatorNeedDynamicMemory(opts) {}

  ~AggregatorMaxN() {
    AggregatorNeedDynamicMemory::clear();
    destorySet();
  }

  void reset() override {
    AggregatorNeedDynamicMemory::clear();
    destorySet();
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValue const& nValue = extractFunctionParameterValue(parameters, 1);

    if (cmpValue.isNull(true) || !nValue.isNumber() || nValue.toInt64() <= 0) {
      return;
    }
    size_t n = static_cast<size_t>(nValue.toInt64());
    if (maxSet.size() >= n) {
      if (this->cmp(cmpValue, *maxSet.rbegin())) {  // it > set中最小的数
        maxSet.insert(cmpValue.clone());
        auto i = --maxSet.end();
        AqlValue v = (*i);  // 得到要被删除的元素
        maxSet.erase(i);
        v.destroy();  // 销毁AqlValue指向的内存
      }
    } else {
      maxSet.insert(cmpValue.clone());
    }
  }

  AqlValue get() const override {
    if (maxSet.empty()) {
      return AqlValue(AqlValueHintNull());
    }
    VPackBuilder builder;
    size_t previousSize = builder.buffer()->size();
    builder.openArray();
    for (auto& i : maxSet) {
      i.toVelocyPack(_vpackOption, builder, false);
    }
    builder.close();
    _memoryUsage += builder.buffer()->size() - previousSize;
    return AqlValue(builder.slice(), builder.size());
  }
  void destorySet() {
    std::vector<AqlValue> values(maxSet.begin(), maxSet.end());
    maxSet.clear();
    for (auto& i : values) {
      i.destroy();
    }
  }

  std::function<bool(const AqlValue&, const AqlValue&)> cmp{
      [this](const AqlValue& lhs, const AqlValue& rhs) {
        return AqlValue::Compare(_vpackOptions, lhs, rhs, true) > 0;
      }};
  std::multiset<AqlValue, std::function<bool(const AqlValue&, const AqlValue&)>>
      maxSet{cmp};
};

struct AggregatorMinNWith final : public AggregatorNeedDynamicMemory {
  explicit AggregatorMinNWith(velocypack::Options const* opts)
      : AggregatorNeedDynamicMemory(opts) {}

  ~AggregatorMinNWith() {
    AggregatorNeedDynamicMemory::clear();
    destorySet();
  }

  void reset() override {
    AggregatorNeedDynamicMemory::clear();
    destorySet();
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValue const& nValue = extractFunctionParameterValue(parameters, 1);
    AqlValue const& withValue = extractFunctionParameterValue(parameters, 2);

    if (cmpValue.isNull(true) || withValue.isNull(true) || !nValue.isNumber() ||
        nValue.toInt64() <= 0) {
      return;
    }
    size_t n = static_cast<size_t>(nValue.toInt64());
    ElemType cmpPair = std::make_pair(cmpValue, withValue);
    if (minSet.size() >= n) {
      if (this->cmp(cmpPair, *minSet.rbegin())) {  // it < set中最大的数
        ElemType cmpPairClone =
            std::make_pair(cmpValue.clone(), withValue.clone());
        minSet.insert(cmpPairClone);
        auto i = --minSet.end();
        ElemType v = (*i);  // 得到要被删除的元素
        minSet.erase(i);
        // 销毁AqlValue指向的内存
        v.first.destroy();
        v.second.destroy();
      }
    } else {
      ElemType cmpPairClone =
          std::make_pair(cmpValue.clone(), withValue.clone());
      minSet.insert(cmpPairClone);
    }
  }

  AqlValue get() const override {
    if (minSet.empty()) {
      return AqlValue(AqlValueHintNull());
    }
    VPackBuilder builder;
    size_t previousSize = builder.buffer()->size();
    builder.openArray();
    for (auto& i : minSet) {
      i.second.toVelocyPack(_vpackOption, builder, false);
    }
    builder.close();
    _memoryUsage += builder.buffer()->size() - previousSize;
    return AqlValue(builder.slice(), builder.size());
  }
  void destorySet() {
    std::vector<ElemType> values(minSet.begin(), minSet.end());
    minSet.clear();
    for (auto& [first, second] : values) {
      first.destroy();
      second.destroy();
    }
  }
  using ElemType = std::pair<AqlValue, AqlValue>;

  std::function<bool(const ElemType&, const ElemType&)> cmp{
      [this](const ElemType& lhs, const ElemType& rhs) {
        return AqlValue::Compare(_vpackOptions, lhs.first, rhs.first, true) < 0;
      }};
  std::multiset<ElemType, std::function<bool(const ElemType&, const ElemType&)>>
      minSet{cmp};
};
struct AggregatorMaxNWith final : public AggregatorNeedDynamicMemory {
  explicit AggregatorMaxNWith(velocypack::Options const* opts)
      : AggregatorNeedDynamicMemory(opts) {}

  ~AggregatorMaxNWith() {
    AggregatorNeedDynamicMemory::clear();
    destorySet();
  }

  void reset() override {
    AggregatorNeedDynamicMemory::clear();
    destorySet();
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValue const& nValue = extractFunctionParameterValue(parameters, 1);
    AqlValue const& withValue = extractFunctionParameterValue(parameters, 2);

    if (cmpValue.isNull(true) || withValue.isNull(true) || !nValue.isNumber() ||
        nValue.toInt64() <= 0) {
      return;
    }
    size_t n = static_cast<size_t>(nValue.toInt64());
    ElemType cmpPair = std::make_pair(cmpValue, withValue);
    if (maxSet.size() >= n) {
      if (this->cmp(cmpPair, *maxSet.rbegin())) {  // it > set中最小的数
        ElemType cmpPairClone =
            std::make_pair(cmpValue.clone(), withValue.clone());
        maxSet.insert(cmpPairClone);
        auto i = --maxSet.end();
        ElemType v = (*i);  // 得到要被删除的元素
        maxSet.erase(i);
        // 销毁AqlValue指向的内存
        v.first.destroy();
        v.second.destroy();
      }
    } else {
      ElemType cmpPairClone =
          std::make_pair(cmpValue.clone(), withValue.clone());
      maxSet.insert(cmpPairClone);
    }
  }

  AqlValue get() const override {
    if (maxSet.empty()) {
      return AqlValue(AqlValueHintNull());
    }
    VPackBuilder builder;
    size_t previousSize = builder.buffer()->size();
    builder.openArray();
    for (auto& i : maxSet) {
      i.second.toVelocyPack(_vpackOption, builder, false);
    }
    builder.close();
    _memoryUsage += builder.buffer()->size() - previousSize;
    return AqlValue(builder.slice(), builder.size());
  }
  void destorySet() {
    std::vector<ElemType> values(maxSet.begin(), maxSet.end());
    maxSet.clear();
    for (auto& [first, second] : values) {
      first.destroy();
      second.destroy();
    }
  }
  using ElemType = std::pair<AqlValue, AqlValue>;

  std::function<bool(const ElemType&, const ElemType&)> cmp{
      [this](const ElemType& lhs, const ElemType& rhs) {
        return AqlValue::Compare(_vpackOptions, lhs.first, rhs.first, true) > 0;
      }};
  std::multiset<ElemType, std::function<bool(const ElemType&, const ElemType&)>>
      maxSet{cmp};
};

struct AggregatorGetGroup final : public AggregatorNeedDynamicMemory {
  explicit AggregatorGetGroup(velocypack::Options const* opts)
      : AggregatorNeedDynamicMemory(opts) {
    builder.openArray();
  }

  ~AggregatorGetGroup() {
    AggregatorNeedDynamicMemory::clear();
    builder.clear();
  }

  void reset() override {
    AggregatorNeedDynamicMemory::clear();
    builder.clear();
    builder.openArray();
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& value = extractFunctionParameterValue(parameters, 0);
    size_t previousSize = builder.buffer()->size();
    value.toVelocyPack(_vpackOption, builder, false);
    _memoryUsage += builder.buffer()->size() - previousSize;
  }

  AqlValue get() const override {
    builder.close();
    return AqlValue(builder.slice(), builder.size());
  }

  mutable VPackBuilder builder;
};
struct AggregatorMax final : public Aggregator {
  explicit AggregatorMax(velocypack::Options const* opts)
      : Aggregator(opts), value() {}

  ~AggregatorMax() { value.destroy(); }

  void reset() override { value.erase(); }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    if (value.isEmpty() ||
        AqlValue::Compare(_vpackOptions, value, cmpValue, true) < 0) {
      value.destroy();
      value = cmpValue.clone();
    }
  }

  AqlValue get() const override {
    if (value.isEmpty()) {
      return AqlValue(AqlValueHintNull());
    }
    return value.clone();
  }

  AqlValue value;
};

struct AggregatorToNdArrayf final : public AggregatorNeedDynamicMemory {
  explicit AggregatorToNdArrayf(velocypack::Options const* opts)
      : AggregatorNeedDynamicMemory(opts) {}

  ~AggregatorToNdArrayf() { AggregatorNeedDynamicMemory::clear(); }

  void reset() override { AggregatorNeedDynamicMemory::clear(); }

  void reduce(VPackFunctionParametersView parameters) override {
    if (!isInitialized) {
      init(parameters);
    }
    std::vector<size_t> indice;
    for (size_t i = 2; i < parameters.size() - 1; i++) {
      int index = extractFunctionParameterValue(parameters, i).toInt64();

      if (index < 0) {
        THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                       "Parameter error");
      }
      indice.push_back(static_cast<size_t>(index));
    }
    auto& param =
        extractFunctionParameterValue(parameters, parameters.size() - 1);
    if (param.isInt()) {
      datas->get<float>()[indice] = static_cast<float>(param.toInt64());
      return;
    } else if (param.isfloatOrDouble()) {
      datas->get<float>()[indice] = static_cast<float>(param.toDouble());
    } else if (param.canTurnIntoNdarray()) {
      auto array = param.getTurnIntoNdarray();
      auto arrayPtr = getNdarrayPtr(array);
      if (arrayPtr->getValueType() == arangodb::aql::Ndarray::INT_TYPE) {
        datas->get<float>()[indice] =
            static_cast<float>(arrayPtr->get<int>()[{}]);
      } else if (arrayPtr->getValueType() ==
                 arangodb::aql::Ndarray::FLOAT_TYPE) {
        datas->get<float>()[indice] =
            static_cast<float>(arrayPtr->get<float>()[{}]);
      } else if (arrayPtr->getValueType() ==
                 arangodb::aql::Ndarray::DOUBLE_TYPE) {
        datas->get<float>()[indice] =
            static_cast<float>(arrayPtr->get<double>()[{}]);
      }
    }
  }

  AqlValue get() const override {
    Ndarray* p = datas.release();
    return AqlValue(p);
  }

  void init(VPackFunctionParametersView parameters) {
    AqlValue info = extractFunctionParameterValue(parameters, 0);

    if (!info.isArray()) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE, "Parameter error");
    }
    VPackSlice infoSlice = info.slice();
    size_t dimensions = 0;
    dimensions = infoSlice.length();

    if (dimensions < 1) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE, "Parameter error");
    }

    // 检查参数个数是否正确
    if (parameters.size() != 3 + dimensions) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                     "Parameter number error");
    }

    // 计算空间
    static constexpr uint64_t MaterializationLimit = 10ULL * 1000ULL * 1000ULL;
    size_t n = 1;
    std::vector<size_t> shape;
    std::vector<std::optional<std::string>> axisNames;
    shape.reserve(dimensions);
    for (auto const& i : arangodb::velocypack::ArrayIterator(infoSlice)) {
      int length = 0;
      try {
        if (i["length"].isDouble()) {
          length = i["length"].getDouble();
        } else {
          length = i["length"].getInt();
        }
        if (i.hasKey("axis") && i["axis"].isString()) {
          axisNames.push_back(i["axis"].copyString());
        } else {
          axisNames.push_back(std::nullopt);
        }
      } catch (arangodb::velocypack::Exception e) {
        THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                       "Parameter error");
      }
      if (length < 1) {
        THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                       "Parameter error");
      }
      shape.push_back(length);

      n = n * (length + 1);
    }
    if (n > MaterializationLimit) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                     "it is too big to be materialized");
    }
    float initValue = extractFunctionParameterValue(parameters, 1).toDouble();
    // 置入初值
    datas = std::make_unique<Ndarray>(initValue, shape);
    datas->setAxisNames(axisNames);
    isInitialized = true;
  }

  bool isInitialized = false;
  mutable std::unique_ptr<Ndarray> datas;
};
struct AggregatorToNdArrayd final : public AggregatorNeedDynamicMemory {
  explicit AggregatorToNdArrayd(velocypack::Options const* opts)
      : AggregatorNeedDynamicMemory(opts) {}

  ~AggregatorToNdArrayd() { AggregatorNeedDynamicMemory::clear(); }

  void reset() override { AggregatorNeedDynamicMemory::clear(); }

  void reduce(VPackFunctionParametersView parameters) override {
    if (!isInitialized) {
      init(parameters);
    }
    std::vector<size_t> indice;
    for (size_t i = 2; i < parameters.size() - 1; i++) {
      int index = extractFunctionParameterValue(parameters, i).toInt64();

      if (index < 0) {
        THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                       "Parameter error");
      }
      indice.push_back(static_cast<size_t>(index));
    }
    auto& param =
        extractFunctionParameterValue(parameters, parameters.size() - 1);
    if (param.isInt()) {
      datas->get<double>()[indice] = (float)param.toInt64();
      return;
    } else if (param.isfloatOrDouble()) {
      datas->get<double>()[indice] = (float)param.toDouble();
    } else if (param.canTurnIntoNdarray()) {
      auto array = param.getTurnIntoNdarray();
      auto arrayPtr = getNdarrayPtr(array);
      if (arrayPtr->getValueType() == arangodb::aql::Ndarray::INT_TYPE) {
        datas->get<double>()[indice] = (double)arrayPtr->get<int>()[{}];
      } else if (arrayPtr->getValueType() ==
                 arangodb::aql::Ndarray::FLOAT_TYPE) {
        datas->get<double>()[indice] = (double)arrayPtr->get<float>()[{}];
      } else {
        datas->get<double>()[indice] = (double)arrayPtr->get<double>()[{}];
      }
    }
  }

  AqlValue get() const override {
    Ndarray* p = datas.release();
    return AqlValue(p);
  }

  void init(VPackFunctionParametersView parameters) {
    AqlValue info = extractFunctionParameterValue(parameters, 0);

    if (!info.isArray()) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE, "Parameter error");
    }
    VPackSlice infoSlice = info.slice();
    size_t dimensions = 0;
    dimensions = infoSlice.length();

    if (dimensions < 1) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE, "Parameter error");
    }

    // 检查参数个数是否正确
    if (parameters.size() != 3 + dimensions) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                     "Parameter number error");
    }

    // 计算空间
    static constexpr uint64_t MaterializationLimit = 10ULL * 1000ULL * 1000ULL;
    size_t n = 1;
    std::vector<size_t> shape;
    std::vector<std::optional<std::string>> axisNames;
    shape.reserve(dimensions);
    for (auto const& i : arangodb::velocypack::ArrayIterator(infoSlice)) {
      int length = 0;
      try {
        if (i["length"].isDouble()) {
          length = i["length"].getDouble();
        } else {
          length = i["length"].getInt();
        }
        if (i.hasKey("axis") && i["axis"].isString()) {
          axisNames.push_back(i["axis"].copyString());
        } else {
          axisNames.push_back(std::nullopt);
        }
      } catch (arangodb::velocypack::Exception e) {
        THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                       "Parameter error");
      }
      if (length < 1) {
        THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                       "Parameter error");
      }
      shape.push_back(length);

      n = n * (length + 1);
    }
    if (n > MaterializationLimit) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                     "it is too big to be materialized");
    }
    double initValue = extractFunctionParameterValue(parameters, 1).toDouble();
    // 置入初值
    datas = std::make_unique<Ndarray>(initValue, shape);
    datas->setAxisNames(axisNames);
    isInitialized = true;
  }

  bool isInitialized = false;
  mutable std::unique_ptr<Ndarray> datas;
};
struct AggregatorToNdArrayi final : public AggregatorNeedDynamicMemory {
  explicit AggregatorToNdArrayi(velocypack::Options const* opts)
      : AggregatorNeedDynamicMemory(opts) {}

  ~AggregatorToNdArrayi() { AggregatorNeedDynamicMemory::clear(); }

  void reset() override { AggregatorNeedDynamicMemory::clear(); }

  void reduce(VPackFunctionParametersView parameters) override {
    if (!isInitialized) {
      init(parameters);
    }
    std::vector<size_t> indice;
    for (size_t i = 2; i < parameters.size() - 1; i++) {
      int index = extractFunctionParameterValue(parameters, i).toInt64();

      if (index < 0) {
        THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                       "Parameter error");
      }
      indice.push_back(static_cast<size_t>(index));
    }
    auto& param =
        extractFunctionParameterValue(parameters, parameters.size() - 1);
    if (param.isInt()) {
      datas->get<int>()[indice] = param.toInt64();
      return;
    } else if (param.isfloatOrDouble()) {
      datas->get<int>()[indice] = (int)param.toDouble();
    } else if (param.canTurnIntoNdarray()) {
      auto array = param.getTurnIntoNdarray();
      auto arrayPtr = getNdarrayPtr(array);
      if (arrayPtr->getValueType() == arangodb::aql::Ndarray::INT_TYPE) {
        datas->get<int>()[indice] = (int)arrayPtr->get<int>()[{}];
      } else if (arrayPtr->getValueType() ==
                 arangodb::aql::Ndarray::FLOAT_TYPE) {
        datas->get<int>()[indice] = (int)arrayPtr->get<float>()[{}];
      } else {
        datas->get<int>()[indice] = (int)arrayPtr->get<double>()[{}];
      }
    }
  }

  AqlValue get() const override {
    Ndarray* p = datas.release();
    return AqlValue(p);
  }

  void init(VPackFunctionParametersView parameters) {
    AqlValue info = extractFunctionParameterValue(parameters, 0);

    if (!info.isArray()) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE, "Parameter error");
    }
    VPackSlice infoSlice = info.slice();
    size_t dimensions = 0;
    dimensions = infoSlice.length();

    if (dimensions < 1) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE, "Parameter error");
    }

    // 检查参数个数是否正确
    if (parameters.size() != 3 + dimensions) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                     "Parameter number error");
    }

    // 计算空间
    static constexpr uint64_t MaterializationLimit = 10ULL * 1000ULL * 1000ULL;
    size_t n = 1;
    std::vector<size_t> shape;
    std::vector<std::optional<std::string>> axisNames;
    shape.reserve(dimensions);
    for (auto const& i : arangodb::velocypack::ArrayIterator(infoSlice)) {
      int length = 0;
      try {
        if (i["length"].isDouble()) {
          length = i["length"].getDouble();
        } else {
          length = i["length"].getInt();
        }

        if (i.hasKey("axis") && i["axis"].isString()) {
          axisNames.push_back(i["axis"].copyString());
        } else {
          axisNames.push_back(std::nullopt);
        }
      } catch (arangodb::velocypack::Exception e) {
        THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                       "Parameter error");
      }
      if (length < 1) {
        THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                       "Parameter error");
      }
      shape.push_back(length);

      n = n * (length + 1);
    }
    if (n > MaterializationLimit * 10000) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                     "it is too big to be materialized");
    }
    int initValue = extractFunctionParameterValue(parameters, 1).toInt64();
    // 置入初值
    datas = std::make_unique<Ndarray>(initValue, shape);
    datas->setAxisNames(axisNames);
    isInitialized = true;
  }

  bool isInitialized = false;
  mutable std::unique_ptr<Ndarray> datas;
};

struct AggregatorSum final : public Aggregator {
  explicit AggregatorSum(velocypack::Options const* opts)
      : Aggregator(opts), sum(0.0), invalid(false), invoked(false) {}

  void reset() override {
    sum = 0.0;
    invalid = false;
    invoked = false;
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    if (!invalid) {
      invoked = true;
      if (cmpValue.isNull(true)) {
        // ignore `null` values here
        return;
      }
      if (cmpValue.isNumber()) {
        double const number = cmpValue.toDouble();
        if (!std::isnan(number) && number != HUGE_VAL && number != -HUGE_VAL) {
          sum += number;
          return;
        }
      }
      invalid = true;
    }
  }

  AqlValue get() const override {
    if (invalid || !invoked || std::isnan(sum) || sum == HUGE_VAL ||
        sum == -HUGE_VAL) {
      return AqlValue(AqlValueHintNull());
    }

    double v = sum;
    return AqlValue(AqlValueHintDouble(v));
  }

  double sum;
  bool invalid;
  bool invoked;
};

/// @brief the single-server variant of AVERAGE
struct AggregatorAverage : public Aggregator {
  explicit AggregatorAverage(velocypack::Options const* opts)
      : Aggregator(opts), count(0), sum(0.0), invalid(false) {}

  void reset() override final {
    count = 0;
    sum = 0.0;
    invalid = false;
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    if (!invalid) {
      if (cmpValue.isNull(true)) {
        // ignore `null` values here
        return;
      }
      if (cmpValue.isNumber()) {
        double const number = cmpValue.toDouble();
        if (!std::isnan(number) && number != HUGE_VAL && number != -HUGE_VAL) {
          sum += number;
          ++count;
          return;
        }
      }
      invalid = true;
    }
  }

  virtual AqlValue get() const override {
    if (invalid || count == 0 || std::isnan(sum) || sum == HUGE_VAL ||
        sum == -HUGE_VAL) {
      return AqlValue(AqlValueHintNull());
    }

    TRI_ASSERT(count > 0);

    double v = sum / count;
    return AqlValue(AqlValueHintDouble(v));
  }

  uint64_t count;
  double sum;
  bool invalid;
};

/// @brief the DB server variant of AVERAGE, producing a sum and a count
struct AggregatorAverageStep1 final : public AggregatorAverage {
  explicit AggregatorAverageStep1(velocypack::Options const* opts)
      : AggregatorAverage(opts) {}

  // special version that will produce an array with sum and count separately
  AqlValue get() const override {
    builder.clear();
    builder.openArray();
    if (invalid || count == 0 || std::isnan(sum) || sum == HUGE_VAL ||
        sum == -HUGE_VAL) {
      builder.add(VPackValue(VPackValueType::Null));
      builder.add(VPackValue(VPackValueType::Null));
    } else {
      TRI_ASSERT(count > 0);
      builder.add(VPackValue(sum));
      builder.add(VPackValue(count));
    }
    builder.close();
    return AqlValue(builder.slice());
  }

  mutable arangodb::velocypack::Builder builder;
};

/// @brief the coordinator variant of AVERAGE, aggregating partial sums and
/// counts
struct AggregatorAverageStep2 final : public AggregatorAverage {
  explicit AggregatorAverageStep2(velocypack::Options const* opts)
      : AggregatorAverage(opts) {}

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    if (!cmpValue.isArray()) {
      invalid = true;
      return;
    }

    bool mustDestroy;
    AqlValue const& sumValue = cmpValue.at(0, mustDestroy, false);
    AqlValue const& countValue = cmpValue.at(1, mustDestroy, false);

    if (sumValue.isNull(true) || countValue.isNull(true)) {
      invalid = true;
      return;
    }

    bool failed = false;
    double v = sumValue.toDouble(failed);
    if (failed) {
      invalid = true;
      return;
    }
    sum += v;
    count += countValue.toInt64();
  }
};

/// @brief base functionality for VARIANCE
struct AggregatorVarianceBase : public Aggregator {
  AggregatorVarianceBase(velocypack::Options const* opts, bool population)
      : Aggregator(opts),
        count(0),
        sum(0.0),
        mean(0.0),
        invalid(false),
        population(population) {}

  void reset() override {
    count = 0;
    sum = 0.0;
    mean = 0.0;
    invalid = false;
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    if (!invalid) {
      if (cmpValue.isNull(true)) {
        // ignore `null` values here
        return;
      }
      if (cmpValue.isNumber()) {
        double const number = cmpValue.toDouble();
        if (!std::isnan(number) && number != HUGE_VAL && number != -HUGE_VAL) {
          double const delta = number - mean;
          ++count;
          mean += delta / count;
          sum += delta * (number - mean);
          return;
        }
      }
      invalid = true;
    }
  }

  uint64_t count;
  double sum;
  double mean;
  bool invalid;
  bool const population;
};

/// @brief the single server variant of VARIANCE
struct AggregatorVariance : public AggregatorVarianceBase {
  AggregatorVariance(velocypack::Options const* opts, bool population)
      : AggregatorVarianceBase(opts, population) {}

  AqlValue get() const override {
    if (invalid || count == 0 || (count == 1 && !population) ||
        std::isnan(sum) || sum == HUGE_VAL || sum == -HUGE_VAL) {
      return AqlValue(AqlValueHintNull());
    }

    TRI_ASSERT(count > 0);

    double v;
    if (!population) {
      TRI_ASSERT(count > 1);
      v = sum / (count - 1);
    } else {
      v = sum / count;
    }

    return AqlValue(AqlValueHintDouble(v));
  }
};

/// @brief the DB server variant of VARIANCE/STDDEV
struct AggregatorVarianceBaseStep1 final : public AggregatorVarianceBase {
  AggregatorVarianceBaseStep1(velocypack::Options const* opts, bool population)
      : AggregatorVarianceBase(opts, population) {}

  AqlValue get() const override {
    builder.clear();
    builder.openArray();
    if (invalid || count == 0 || (count == 1 && !population) ||
        std::isnan(sum) || sum == HUGE_VAL || sum == -HUGE_VAL) {
      builder.add(VPackValue(VPackValueType::Null));
      builder.add(VPackValue(VPackValueType::Null));
      builder.add(VPackValue(VPackValueType::Null));
    } else {
      TRI_ASSERT(count > 0);

      builder.add(VPackValue(count));
      builder.add(VPackValue(sum));
      builder.add(VPackValue(mean));
    }
    builder.close();

    return AqlValue(builder.slice());
  }

  mutable arangodb::velocypack::Builder builder;
};

/// @brief the coordinator variant of VARIANCE
struct AggregatorVarianceBaseStep2 : public AggregatorVarianceBase {
  AggregatorVarianceBaseStep2(velocypack::Options const* opts, bool population)
      : AggregatorVarianceBase(opts, population) {}

  void reset() override {
    AggregatorVarianceBase::reset();
    values.clear();
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    if (!cmpValue.isArray()) {
      invalid = true;
      return;
    }

    bool mustDestroy;
    AqlValue const& countValue = cmpValue.at(0, mustDestroy, false);
    AqlValue const& sumValue = cmpValue.at(1, mustDestroy, false);
    AqlValue const& meanValue = cmpValue.at(2, mustDestroy, false);

    if (countValue.isNull(true) || sumValue.isNull(true) ||
        meanValue.isNull(true)) {
      invalid = true;
      return;
    }

    bool failed = false;
    double v1 = sumValue.toDouble(failed);
    if (failed) {
      invalid = true;
      return;
    }
    double v2 = meanValue.toDouble(failed);
    if (failed) {
      invalid = true;
      return;
    }

    int64_t c = countValue.toInt64();
    if (c == 0) {
      invalid = true;
      return;
    }
    count += c;
    sum += v2 * c;
    mean += v2 * c;
    values.emplace_back(std::make_tuple(v1 / c, v2, c));
  }

  AqlValue get() const override {
    if (invalid || count == 0 || (count == 1 && !population)) {
      return AqlValue(AqlValueHintNull());
    }

    double const avg = sum / count;
    double v = 0.0;
    for (auto const& it : values) {
      double variance = std::get<0>(it);
      double mean = std::get<1>(it);
      int64_t count = std::get<2>(it);
      v += count * (variance + std::pow(mean - avg, 2));
    }

    if (!population) {
      TRI_ASSERT(count > 1);
      v /= count - 1;
    } else {
      v /= count;
    }
    return AqlValue(AqlValueHintDouble(v));
  }

  std::vector<std::tuple<double, double, int64_t>> values;
};

/// @brief the single server variant of STDDEV
struct AggregatorStddev : public AggregatorVarianceBase {
  AggregatorStddev(velocypack::Options const* opts, bool population)
      : AggregatorVarianceBase(opts, population) {}

  AqlValue get() const override {
    if (invalid || count == 0 || (count == 1 && !population) ||
        std::isnan(sum) || sum == HUGE_VAL || sum == -HUGE_VAL) {
      return AqlValue(AqlValueHintNull());
    }

    TRI_ASSERT(count > 0);

    double v;
    if (!population) {
      TRI_ASSERT(count > 1);
      v = std::sqrt(sum / (count - 1));
    } else {
      v = std::sqrt(sum / count);
    }

    return AqlValue(AqlValueHintDouble(v));
  }
};

/// @brief the coordinator variant of STDDEV
struct AggregatorStddevBaseStep2 final : public AggregatorVarianceBaseStep2 {
  AggregatorStddevBaseStep2(velocypack::Options const* opts, bool population)
      : AggregatorVarianceBaseStep2(opts, population) {}

  AqlValue get() const override {
    if (invalid || count == 0 || (count == 1 && !population)) {
      return AqlValue(AqlValueHintNull());
    }

    double const avg = sum / count;
    double v = 0.0;
    for (auto const& it : values) {
      double variance = std::get<0>(it);
      double mean = std::get<1>(it);
      int64_t count = std::get<2>(it);
      v += count * (variance + std::pow(mean - avg, 2));
    }

    if (!population) {
      TRI_ASSERT(count > 1);
      v /= count - 1;
    } else {
      v /= count;
    }

    v = std::sqrt(v);
    return AqlValue(AqlValueHintDouble(v));
  }
};

/// @brief the single-server and DB server variant of UNIQUE
struct AggregatorUnique : public Aggregator {
  explicit AggregatorUnique(velocypack::Options const* opts)
      : Aggregator(opts),
        allocator(1024),
        seen(512, basics::VelocyPackHelper::VPackHash(),
             basics::VelocyPackHelper::VPackEqual(_vpackOptions)) {}

  ~AggregatorUnique() { reset(); }

  // cppcheck-suppress virtualCallInConstructor
  void reset() override final {
    seen.clear();
    builder.clear();
    allocator.clear();
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValueMaterializer materializer(_vpackOptions);

    VPackSlice s = materializer.slice(cmpValue);

    if (seen.contains(s)) {
      // already saw the same value
      return;
    }

    char* pos = allocator.store(s.startAs<char>(), s.byteSize());
    seen.emplace(reinterpret_cast<uint8_t const*>(pos));

    if (builder.isClosed()) {
      builder.openArray();
    }
    builder.add(VPackSlice(reinterpret_cast<uint8_t const*>(pos)));
  }

  AqlValue get() const override final {
    // if not yet an array, start one
    if (builder.isClosed()) {
      builder.openArray();
    }

    // always close the Builder
    builder.close();
    return AqlValue(builder.slice());
  }

  MemoryBlockAllocator allocator;
  containers::FlatHashSet<velocypack::Slice,
                          basics::VelocyPackHelper::VPackHash,
                          basics::VelocyPackHelper::VPackEqual>
      seen;
  mutable arangodb::velocypack::Builder builder;
};

/// @brief the coordinator variant of UNIQUE
struct AggregatorUniqueStep2 final : public AggregatorUnique {
  explicit AggregatorUniqueStep2(velocypack::Options const* opts)
      : AggregatorUnique(opts) {}

  void reduce(VPackFunctionParametersView parameters) override final {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValueMaterializer materializer(_vpackOptions);

    VPackSlice s = materializer.slice(cmpValue);

    if (!s.isArray()) {
      return;
    }

    for (VPackSlice it : VPackArrayIterator(s)) {
      if (seen.contains(it)) {
        // already saw the same value
        return;
      }

      char* pos = allocator.store(it.startAs<char>(), it.byteSize());
      seen.emplace(reinterpret_cast<uint8_t const*>(pos));

      if (builder.isClosed()) {
        builder.openArray();
      }
      builder.add(VPackSlice(reinterpret_cast<uint8_t const*>(pos)));
    }
  }
};

/// @brief the single-server and DB server variant of SORTED_UNIQUE
struct AggregatorSortedUnique : public Aggregator {
  explicit AggregatorSortedUnique(velocypack::Options const* opts)
      : Aggregator(opts), allocator(1024), seen(_vpackOptions) {}

  ~AggregatorSortedUnique() { reset(); }

  // cppcheck-suppress virtualCallInConstructor
  void reset() override final {
    seen.clear();
    allocator.clear();
    builder.clear();
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValueMaterializer materializer(_vpackOptions);

    VPackSlice s = materializer.slice(cmpValue);

    if (seen.find(s) != seen.end()) {
      // already saw the same value
      return;
    }

    char* pos = allocator.store(s.startAs<char>(), s.byteSize());
    seen.emplace(reinterpret_cast<uint8_t const*>(pos));
  }

  AqlValue get() const override final {
    builder.openArray();
    for (auto const& it : seen) {
      builder.add(it);
    }

    // always close the Builder
    builder.close();
    return AqlValue(builder.slice());
  }

  MemoryBlockAllocator allocator;
  std::set<velocypack::Slice, basics::VelocyPackHelper::VPackLess<true>> seen;
  mutable arangodb::velocypack::Builder builder;
};

/// @brief the coordinator variant of SORTED_UNIQUE
struct AggregatorSortedUniqueStep2 final : public AggregatorSortedUnique {
  explicit AggregatorSortedUniqueStep2(velocypack::Options const* opts)
      : AggregatorSortedUnique(opts) {}

  void reduce(VPackFunctionParametersView parameters) override final {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValueMaterializer materializer(_vpackOptions);

    VPackSlice s = materializer.slice(cmpValue);

    if (!s.isArray()) {
      return;
    }

    for (VPackSlice it : VPackArrayIterator(s)) {
      if (seen.find(it) != seen.end()) {
        // already saw the same value
        return;
      }

      char* pos = allocator.store(it.startAs<char>(), it.byteSize());
      seen.emplace(reinterpret_cast<uint8_t const*>(pos));
    }
  }
};

struct AggregatorCountDistinct : public Aggregator {
  explicit AggregatorCountDistinct(velocypack::Options const* opts)
      : Aggregator(opts),
        allocator(1024),
        seen(512, basics::VelocyPackHelper::VPackHash(),
             basics::VelocyPackHelper::VPackEqual(_vpackOptions)) {}

  ~AggregatorCountDistinct() { reset(); }

  // cppcheck-suppress virtualCallInConstructor
  void reset() override final {
    seen.clear();
    allocator.clear();
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValueMaterializer materializer(_vpackOptions);

    VPackSlice s = materializer.slice(cmpValue);

    if (seen.contains(s)) {
      // already saw the same value
      return;
    }

    char* pos = allocator.store(s.startAs<char>(), s.byteSize());
    seen.emplace(reinterpret_cast<uint8_t const*>(pos));
  }

  AqlValue get() const override final {
    uint64_t value = seen.size();
    return AqlValue(AqlValueHintUInt(value));
  }

  MemoryBlockAllocator allocator;
  containers::FlatHashSet<velocypack::Slice,
                          basics::VelocyPackHelper::VPackHash,
                          basics::VelocyPackHelper::VPackEqual>
      seen;
};

template<class T>
struct GenericFactory : Aggregator::Factory {
  virtual std::unique_ptr<Aggregator> operator()(
      velocypack::Options const* opts) const override {
    return std::make_unique<T>(opts);
  }
  void createInPlace(void* address,
                     velocypack::Options const* opts) const override {
    new (address) T(opts);
  }
  std::size_t getAggregatorSize() const override { return sizeof(T); }
};

template<class T>
struct GenericVarianceFactory : Aggregator::Factory {
  explicit GenericVarianceFactory(bool population) : population(population) {}

  virtual std::unique_ptr<Aggregator> operator()(
      velocypack::Options const* opts) const override {
    return std::make_unique<T>(opts, population);
  }
  void createInPlace(void* address,
                     velocypack::Options const* opts) const override {
    new (address) T(opts, population);
  }
  std::size_t getAggregatorSize() const override { return sizeof(T); }

  bool population;
};

/// @brief the coordinator variant of COUNT_DISTINCT
struct AggregatorCountDistinctStep2 final : public AggregatorCountDistinct {
  explicit AggregatorCountDistinctStep2(velocypack::Options const* opts)
      : AggregatorCountDistinct(opts) {}

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    AqlValueMaterializer materializer(_vpackOptions);

    VPackSlice s = materializer.slice(cmpValue);

    if (!s.isArray()) {
      return;
    }

    for (VPackSlice it : VPackArrayIterator(s)) {
      if (seen.find(s) != seen.end()) {
        // already saw the same value
        return;
      }

      char* pos = allocator.store(it.startAs<char>(), it.byteSize());
      seen.emplace(reinterpret_cast<uint8_t const*>(pos));
    }
  }
};

struct BitFunctionAnd {
  uint64_t compute(uint64_t value1, uint64_t value2) noexcept {
    return value1 & value2;
  }
};

struct BitFunctionOr {
  uint64_t compute(uint64_t value1, uint64_t value2) noexcept {
    return value1 | value2;
  }
};

struct BitFunctionXOr {
  uint64_t compute(uint64_t value1, uint64_t value2) noexcept {
    return value1 ^ value2;
  }
};

template<typename BitFunction>
struct AggregatorBitFunction : public Aggregator, BitFunction {
  explicit AggregatorBitFunction(velocypack::Options const* opts)
      : Aggregator(opts), result(0), invalid(false), invoked(false) {}

  void reset() override {
    result = 0;
    invalid = false;
    invoked = false;
  }

  void reduce(VPackFunctionParametersView parameters) override {
    AqlValue const& cmpValue = extractFunctionParameterValue(parameters, 0);
    if (!invalid) {
      if (cmpValue.isNull(true)) {
        // ignore `null` values here
        return;
      }
      if (cmpValue.isNumber()) {
        double const number = cmpValue.toDouble();
        if (!std::isnan(number) && number >= 0.0) {
          int64_t value = cmpValue.toInt64();
          if (value <=
              static_cast<int64_t>(functions::bitFunctionsMaxSupportedValue)) {
            TRI_ASSERT(value >= 0 && value <= UINT32_MAX);
            if (invoked) {
              result = this->compute(result, static_cast<uint64_t>(value));
            } else {
              result = static_cast<uint64_t>(value);
              invoked = true;
            }
            return;
          }
        }
      }

      invalid = true;
    }
  }

  AqlValue get() const override {
    if (invalid || !invoked) {
      return AqlValue(AqlValueHintNull());
    }

    return AqlValue(AqlValueHintUInt(result));
  }

  uint64_t result;
  bool invalid;
  bool invoked;
};

struct AggregatorBitAnd : public AggregatorBitFunction<BitFunctionAnd> {
  explicit AggregatorBitAnd(velocypack::Options const* opts)
      : AggregatorBitFunction(opts) {}
};

struct AggregatorBitOr : public AggregatorBitFunction<BitFunctionOr> {
  explicit AggregatorBitOr(velocypack::Options const* opts)
      : AggregatorBitFunction(opts) {}
};

struct AggregatorBitXOr : public AggregatorBitFunction<BitFunctionXOr> {
  explicit AggregatorBitXOr(velocypack::Options const* opts)
      : AggregatorBitFunction(opts) {}
};

/// @brief all available aggregators with their meta data
std::unordered_map<std::string_view, AggregatorInfo> const aggregators = {
    {"LENGTH",
     {std::make_shared<GenericFactory<AggregatorLength>>(), doesNotRequireInput,
      official, "LENGTH",
      "SUM"}},  // we need to sum up the lengths from the DB servers
    {"MIN",
     {std::make_shared<GenericFactory<AggregatorMin>>(), doesRequireInput,
      official, "MIN", "MIN"}},  // min is commutative
    {"MIN_N",
     {std::make_shared<GenericFactory<AggregatorMinN>>(), doesRequireInput,
      official, "MIN_N", "MIN_N"}},  // min is commutative
    {"MIN_WITH",
     {std::make_shared<GenericFactory<AggregatorMinWith>>(), doesRequireInput,
      official, "MIN_WITH", "MIN_WITH"}},  // min is commutative
    {"MIN_N_WITH",
     {std::make_shared<GenericFactory<AggregatorMinNWith>>(), doesRequireInput,
      official, "MIN_N_WITH", "MIN_N_WITH"}},  // min is commutative
    {"MAX",
     {std::make_shared<GenericFactory<AggregatorMax>>(), doesRequireInput,
      official, "MAX", "MAX"}},  // max is commutative
    {"MAX_N",
     {std::make_shared<GenericFactory<AggregatorMaxN>>(), doesRequireInput,
      official, "MAX_N", "MAX_N"}},  // max is commutative
    {"MAX_WITH",
     {std::make_shared<GenericFactory<AggregatorMaxWith>>(), doesRequireInput,
      official, "MAX_WITH", "MAX_WITH"}},  // max is commutative
    {"MAX_N_WITH",
     {std::make_shared<GenericFactory<AggregatorMaxNWith>>(), doesRequireInput,
      official, "MAX_N_WITH", "MAX_N_WITH"}},  // max is commutative
    {"SUM",
     {std::make_shared<GenericFactory<AggregatorSum>>(), doesRequireInput,
      official, "SUM", "SUM"}},  // sum is commutative
    {"AVERAGE",
     {std::make_shared<GenericFactory<AggregatorAverage>>(), doesRequireInput,
      official, "AVERAGE_STEP1", "AVERAGE_STEP2"}},
    {"AVERAGE_STEP1",
     {std::make_shared<GenericFactory<AggregatorAverageStep1>>(),
      doesRequireInput, internalOnly, "", "AVERAGE_STEP1"}},
    {"AVERAGE_STEP2",
     {std::make_shared<GenericFactory<AggregatorAverageStep2>>(),
      doesRequireInput, internalOnly, "", "AVERAGE_STEP2"}},
    {"VARIANCE_POPULATION",
     {std::make_shared<GenericVarianceFactory<AggregatorVariance>>(true),
      doesRequireInput, official, "VARIANCE_POPULATION_STEP1",
      "VARIANCE_POPULATION_STEP2"}},
    {"VARIANCE_POPULATION_STEP1",
     {std::make_shared<GenericVarianceFactory<AggregatorVarianceBaseStep1>>(
          true),
      doesRequireInput, internalOnly, "", "VARIANCE_POPULATION_STEP1"}},
    {"VARIANCE_POPULATION_STEP2",
     {std::make_shared<GenericVarianceFactory<AggregatorVarianceBaseStep2>>(
          true),
      doesRequireInput, internalOnly, "", "VARIANCE_POPULATION_STEP2"}},
    {"VARIANCE_SAMPLE",
     {std::make_shared<GenericVarianceFactory<AggregatorVariance>>(false),
      doesRequireInput, official, "VARIANCE_SAMPLE_STEP1",
      "VARIANCE_SAMPLE_STEP2"}},
    {"VARIANCE_SAMPLE_STEP1",
     {std::make_shared<GenericVarianceFactory<AggregatorVarianceBaseStep1>>(
          false),
      doesRequireInput, internalOnly, "", "VARIANCE_SAMPLE_STEP1"}},
    {"VARIANCE_SAMPLE_STEP2",
     {std::make_shared<GenericVarianceFactory<AggregatorVarianceBaseStep2>>(
          false),
      doesRequireInput, internalOnly, "", "VARIANCE_SAMPLE_STEP2"}},
    {"STDDEV_POPULATION",
     {std::make_shared<GenericVarianceFactory<AggregatorStddev>>(true),
      doesRequireInput, official, "STDDEV_POPULATION_STEP1",
      "STDDEV_POPULATION_STEP2"}},
    {"STDDEV_POPULATION_STEP1",
     {std::make_shared<GenericVarianceFactory<AggregatorVarianceBaseStep1>>(
          true),
      doesRequireInput, internalOnly, "", "STDDEV_POPULATION_STEP1"}},
    {"STDDEV_POPULATION_STEP2",
     {std::make_shared<GenericVarianceFactory<AggregatorStddevBaseStep2>>(true),
      doesRequireInput, internalOnly, "", "STDDEV_POPULATION_STEP2"}},
    {"STDDEV_SAMPLE",
     {std::make_shared<GenericVarianceFactory<AggregatorStddev>>(false),
      doesRequireInput, official, "STDDEV_SAMPLE_STEP1",
      "STDDEV_SAMPLE_STEP2"}},
    {"STDDEV_SAMPLE_STEP1",
     {std::make_shared<GenericVarianceFactory<AggregatorVarianceBaseStep1>>(
          false),
      doesRequireInput, internalOnly, "", "STDDEV_SAMPLE_STEP1"}},
    {"STDDEV_SAMPLE_STEP2",
     {std::make_shared<GenericVarianceFactory<AggregatorStddevBaseStep2>>(
          false),
      doesRequireInput, internalOnly, "", "STDDEV_SAMPLE_STEP2"}},
    {"UNIQUE",
     {std::make_shared<GenericFactory<AggregatorUnique>>(), doesRequireInput,
      official, "UNIQUE", "UNIQUE_STEP2"}},
    {"UNIQUE_STEP2",
     {std::make_shared<GenericFactory<AggregatorUniqueStep2>>(),
      doesRequireInput, internalOnly, "", "UNIQUE_STEP2"}},
    {"SORTED_UNIQUE",
     {std::make_shared<GenericFactory<AggregatorSortedUnique>>(),
      doesRequireInput, official, "SORTED_UNIQUE", "SORTED_UNIQUE_STEP2"}},
    {"SORTED_UNIQUE_STEP2",
     {std::make_shared<GenericFactory<AggregatorSortedUniqueStep2>>(),
      doesRequireInput, internalOnly, "", "SORTED_UNIQUE_STEP2"}},
    {"COUNT_DISTINCT",
     {std::make_shared<GenericFactory<AggregatorCountDistinct>>(),
      doesRequireInput, official, "UNIQUE", "COUNT_DISTINCT_STEP2"}},
    {"COUNT_DISTINCT_STEP2",
     {std::make_shared<GenericFactory<AggregatorCountDistinctStep2>>(),
      doesRequireInput, internalOnly, "", "COUNT_DISTINCT_STEP2"}},
    {"BIT_AND",
     {std::make_shared<GenericFactory<AggregatorBitAnd>>(), doesRequireInput,
      official, "BIT_AND", "BIT_AND"}},
    {"BIT_OR",
     {std::make_shared<GenericFactory<AggregatorBitOr>>(), doesRequireInput,
      official, "BIT_OR", "BIT_OR"}},
    {"BIT_XOR",
     {std::make_shared<GenericFactory<AggregatorBitXOr>>(), doesRequireInput,
      official, "BIT_XOR", "BIT_XOR"}},
    {"GET_GROUP",
     {std::make_shared<GenericFactory<AggregatorGetGroup>>(), doesRequireInput,
      official, "GET_GROUP", "GET_GROUP"}},
    {"AGG_TO_NDARRAYF",
     {std::make_shared<GenericFactory<AggregatorToNdArrayf>>(),
      doesRequireInput, official, "AGG_TO_NDARRAYF", "AGG_TO_NDARRAYF"}},
    {"AGG_TO_NDARRAYD",
     {std::make_shared<GenericFactory<AggregatorToNdArrayd>>(),
      doesRequireInput, official, "AGG_TO_NDARRAYD", "AGG_TO_NDARRAYD"}},
    {"AGG_TO_NDARRAYI",
     {std::make_shared<GenericFactory<AggregatorToNdArrayi>>(),
      doesRequireInput, official, "AGG_TO_NDARRAYI", "AGG_TO_NDARRAYI"}}};

/// @brief aliases (user-visible) for aggregation functions
std::unordered_map<std::string_view, std::string_view> const aliases = {
    {"COUNT", "LENGTH"},                  // COUNT = LENGTH
    {"AVG", "AVERAGE"},                   // AVG = AVERAGE
    {"VARIANCE", "VARIANCE_POPULATION"},  // VARIANCE = VARIANCE_POPULATION
    {"STDDEV", "STDDEV_POPULATION"},      // STDDEV = STDDEV_POPULATION
    {"COUNT_UNIQUE", "COUNT_DISTINCT"}    // COUNT_UNIQUE = COUNT_DISTINCT
};

}  // namespace

std::unique_ptr<Aggregator> Aggregator::fromTypeString(
    velocypack::Options const* opts, std::string_view type) {
  // will always return a valid factory or throw an exception
  auto& factory = Aggregator::factoryFromTypeString(type);

  return factory(opts);
}

std::unique_ptr<Aggregator> Aggregator::fromVPack(
    velocypack::Options const* opts, arangodb::velocypack::Slice slice,
    std::string_view nameAttribute) {
  VPackSlice variable = slice.get(nameAttribute);

  if (variable.isString()) {
    return fromTypeString(opts, variable.stringView());
  }
  THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_INTERNAL, "invalid aggregator type");
}
// 从字符串构造Aggregatorfactory
Aggregator::Factory const& Aggregator::factoryFromTypeString(
    std::string_view type) {
  auto it = ::aggregators.find(translateAlias(type));

  if (it != ::aggregators.end()) {
    return *(it->second.factory);
  }

  // aggregator function name should have been validated before
  THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_INTERNAL, "invalid aggregator type");
}

std::string_view Aggregator::translateAlias(std::string_view name) {
  auto it = ::aliases.find(name);

  if (it != ::aliases.end()) {
    return (*it).second;
  }
  // not an alias
  return name;
}

std::string_view Aggregator::pushToDBServerAs(std::string_view type) {
  auto it = ::aggregators.find(translateAlias(type));

  if (it != ::aggregators.end()) {
    return (*it).second.pushToDBServerAs;
  }
  THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_INTERNAL, "invalid aggregator type");
}

std::string_view Aggregator::runOnCoordinatorAs(std::string_view type) {
  auto it = ::aggregators.find(translateAlias(type));

  if (it != ::aggregators.end()) {
    return (*it).second.runOnCoordinatorAs;
  }
  THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_INTERNAL, "invalid aggregator type");
}

bool Aggregator::isValid(std::string_view type) {
  auto it = ::aggregators.find(translateAlias(type));

  if (it == ::aggregators.end()) {
    return false;
  }
  // check if the aggregator is part of the public API or internal
  return (*it).second.isOfficialAggregator;
}

bool Aggregator::requiresInput(std::string_view type) {
  auto it = ::aggregators.find(translateAlias(type));

  if (it != ::aggregators.end()) {
    return (*it).second.requiresInput;
  }
  THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_INTERNAL, "invalid aggregator type");
}
