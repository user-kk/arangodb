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
/// @author Max Neunhoeffer
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "Containers/SmallVector.h"

namespace arangodb {
namespace velocypack {
class Builder;
class Slice;
}  // namespace velocypack
namespace aql {

struct Variable;
using InVarsType = containers::SmallVector<Variable const*, 2>;

/// @brief CollectOptions
struct CollectOptions final {
  /// @brief selected aggregation method
  enum class CollectMethod { UNDEFINED, HASH, SORTED, DISTINCT, COUNT };

  /// @brief constructor, using default values
  CollectOptions();

  CollectOptions(CollectOptions const& other) = default;

  CollectOptions& operator=(CollectOptions const& other) = default;

  /// @brief constructor
  explicit CollectOptions(arangodb::velocypack::Slice);

  /// @brief whether or not the method can be used
  bool canUseMethod(CollectMethod method) const;

  /// @brief whether or not the method should be used
  bool shouldUseMethod(CollectMethod method) const;

  /// @brief convert the options to VelocyPack
  void toVelocyPack(arangodb::velocypack::Builder&) const;

  /// @brief get the aggregation method from a string
  static CollectMethod methodFromString(std::string_view);

  /// @brief stringify the aggregation method
  static std::string_view methodToString(CollectOptions::CollectMethod method);

  CollectMethod method;
};

struct GroupVarInfo final {
  Variable const* outVar;
  Variable const* inVar;
};

struct AggregateVarInfo final {
  Variable const* outVar;
  InVarsType inVars;
  std::string type;
};

}  // namespace aql
}  // namespace arangodb
