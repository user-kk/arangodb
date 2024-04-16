#include "Assertions/Assert.h"
#include "Functions.h"
#include <cassert>
#include <cstddef>
#include <set>
#include <utility>

#include "Aql/AqlFunctionFeature.h"
#include "Aql/AqlValueMaterializer.h"
#include "Aql/Expression.h"
#include "Aql/ExpressionContext.h"
#include "Aql/Function.h"
#include "Aql/Query.h"

#include "Basics/VelocyPackHelper.h"
#include "Cluster/ClusterInfo.h"
#include "IResearch/IResearchFilterFactory.h"
#include "IResearch/VelocyPackHelper.h"
#include "Indexes/Index.h"
#include "Rest/Version.h"
#include "StorageEngine/TransactionState.h"
#include "Transaction/Context.h"
#include "Transaction/Helpers.h"
#include "Transaction/Methods.h"
#include "Utils/ExecContext.h"
#ifdef USE_V8
#include "V8/v8-vpack.h"
#endif
#include "VocBase/KeyGenerator.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/Methods/Collections.h"

#ifdef USE_ENTERPRISE
#include "Enterprise/VocBase/SmartGraphSchema.h"
#endif

#include <absl/strings/str_cat.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <date/date.h>
#include <date/iso_week.h>
#include <date/tz.h>
#include <s2/s2loop.h>
#include <s2/s2polygon.h>

#include <unicode/schriter.h>
#include <unicode/stsearch.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

#include <velocypack/Collection.h>
#include <velocypack/Dumper.h>
#include <velocypack/Iterator.h>
#include <velocypack/Sink.h>

#ifdef __APPLE__
#include <regex>
#endif

#include <arpa/inet.h>

#include <absl/crc/crc32c.h>
#include <absl/strings/escaping.h>

using namespace arangodb;
using namespace basics;
using namespace aql;
using namespace std::chrono;

AqlValue functions::MinN(ExpressionContext* expressionContext, AstNode const&,
                         VPackFunctionParametersView parameters) {
  AqlValue const& value = extractFunctionParameterValue(parameters, 0);
  AqlValue const& nValue = extractFunctionParameterValue(parameters, 1);

  if (!value.isArray()) {
    // not an array
    registerWarning(expressionContext, "MINN", TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  if (!nValue.isNumber() || nValue.toInt64() <= 0) {
    registerWarning(expressionContext, "MINN", TRI_ERROR_TYPE_ERROR);
    return AqlValue(AqlValueHintNull());
  }
  size_t n = static_cast<size_t>(nValue.toInt64());

  transaction::Methods* trx = &expressionContext->trx();
  auto* vopts = &trx->vpackOptions();

  AqlValueMaterializer materializer(vopts);
  VPackSlice slice = materializer.slice(value);

  auto options = trx->transactionContextPtr()->getVPackOptions();
  auto cmp = [options](VPackSlice lhs, VPackSlice rhs) {
    return basics::VelocyPackHelper::compare(lhs, rhs, true, options) < 0;
  };
  std::multiset<VPackSlice, decltype(cmp)> minSet(cmp);
  for (VPackSlice it : VPackArrayIterator(slice)) {
    if (it.isNull()) {
      continue;
    }

    if (minSet.size() >= n) {
      if (cmp(it, *minSet.rbegin())) {  // it < set中最大的数
        minSet.insert(it);
        minSet.erase(--minSet.end());
      }
    } else {
      minSet.insert(it);
    }
  }

  if (minSet.empty()) {
    return AqlValue(AqlValueHintNull());
  }
  transaction::BuilderLeaser builder(trx);
  builder->openArray();

  for (auto i : minSet) {
    builder->add(i.resolveExternal());
  }
  builder->close();
  return AqlValue(builder->slice(), builder->size());
}

AqlValue functions::MaxN(ExpressionContext* expressionContext, AstNode const&,
                         VPackFunctionParametersView parameters) {
  AqlValue const& value = extractFunctionParameterValue(parameters, 0);
  AqlValue const& nValue = extractFunctionParameterValue(parameters, 1);

  if (!value.isArray()) {
    // not an array
    registerWarning(expressionContext, "MAXN", TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  if (!nValue.isNumber() || nValue.toInt64() <= 0) {
    registerWarning(expressionContext, "MAXN", TRI_ERROR_TYPE_ERROR);
    return AqlValue(AqlValueHintNull());
  }
  size_t n = static_cast<size_t>(nValue.toInt64());

  transaction::Methods* trx = &expressionContext->trx();
  auto* vopts = &trx->vpackOptions();

  AqlValueMaterializer materializer(vopts);
  VPackSlice slice = materializer.slice(value);

  auto options = trx->transactionContextPtr()->getVPackOptions();
  auto cmp = [options](VPackSlice lhs, VPackSlice rhs) {
    return basics::VelocyPackHelper::compare(lhs, rhs, true, options) > 0;
  };
  std::multiset<VPackSlice, decltype(cmp)> minSet(cmp);
  for (VPackSlice it : VPackArrayIterator(slice)) {
    if (it.isNull()) {
      continue;
    }

    if (minSet.size() >= n) {
      if (cmp(it, *minSet.rbegin())) {  // it > set中最小的数
        minSet.insert(it);
        minSet.erase(--minSet.end());
      }
    } else {
      minSet.insert(it);
    }
  }

  if (minSet.empty()) {
    return AqlValue(AqlValueHintNull());
  }
  transaction::BuilderLeaser builder(trx);
  builder->openArray();

  for (auto i : minSet) {
    builder->add(i.resolveExternal());
  }
  builder->close();
  return AqlValue(builder->slice(), builder->size());
}

AqlValue functions::MinWith(ExpressionContext* expressionContext,
                            AstNode const&,
                            VPackFunctionParametersView parameters) {
  AqlValue const& value = extractFunctionParameterValue(parameters, 0);
  AqlValue const& withValue = extractFunctionParameterValue(parameters, 1);

  if (!value.isArray() || !withValue.isArray()) {
    // not an array
    registerWarning(expressionContext, "MINWITH",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }

  transaction::Methods* trx = &expressionContext->trx();
  auto* vopts = &trx->vpackOptions();

  AqlValueMaterializer materializer(vopts);
  VPackSlice slice = materializer.slice(value);
  AqlValueMaterializer materializer2(vopts);
  VPackSlice withSlice = materializer2.slice(withValue);

  if (slice.length() != withSlice.length()) {
    registerWarning(expressionContext, "MINWITH",
                    TRI_ERROR_QUERY_FUNCTION_RUNTIME_ERROR);
    return AqlValue(AqlValueHintNull());
  }

  VPackSlice minValue;
  std::vector<VPackSlice> minWithValues;
  auto options = trx->transactionContextPtr()->getVPackOptions();

  for (size_t i = 0; i < slice.length(); i++) {
    VPackSlice it = slice.at(i);
    if (it.isNull()) {
      continue;
    }
    if (minValue.isNone()) {
      minValue = it;
      minWithValues.clear();
      minWithValues.push_back(withSlice.at(i));
    } else {
      int ret = basics::VelocyPackHelper::compare(it, minValue, true, options);
      if (ret < 0) {
        minValue = it;
        minWithValues.clear();
        minWithValues.push_back(withSlice.at(i));
      } else if (ret == 0) {
        minWithValues.push_back(withSlice.at(i));
      }
    }
  }
  if (minWithValues.empty()) {
    return AqlValue(AqlValueHintNull());
  }
  transaction::BuilderLeaser builder(trx);
  builder->openArray();

  for (auto i : minWithValues) {
    builder->add(i.resolveExternal());
  }
  builder->close();
  return AqlValue(builder->slice(), builder->size());
}

AqlValue functions::MaxWith(ExpressionContext* expressionContext,
                            AstNode const&,
                            VPackFunctionParametersView parameters) {
  AqlValue const& value = extractFunctionParameterValue(parameters, 0);
  AqlValue const& withValue = extractFunctionParameterValue(parameters, 1);

  if (!value.isArray() || !withValue.isArray()) {
    // not an array
    registerWarning(expressionContext, "MAXWITH",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }

  transaction::Methods* trx = &expressionContext->trx();
  auto* vopts = &trx->vpackOptions();

  AqlValueMaterializer materializer(vopts);
  VPackSlice slice = materializer.slice(value);
  AqlValueMaterializer materializer2(vopts);
  VPackSlice withSlice = materializer2.slice(withValue);

  if (slice.length() != withSlice.length()) {
    registerWarning(expressionContext, "MAXWITH",
                    TRI_ERROR_QUERY_FUNCTION_RUNTIME_ERROR);
    return AqlValue(AqlValueHintNull());
  }

  VPackSlice maxValue;
  std::vector<VPackSlice> maxWithValues;
  auto options = trx->transactionContextPtr()->getVPackOptions();

  for (size_t i = 0; i < slice.length(); i++) {
    VPackSlice it = slice.at(i);
    if (it.isNull()) {
      continue;
    }
    if (maxValue.isNone()) {
      maxValue = it;
      maxWithValues.clear();
      maxWithValues.push_back(withSlice.at(i));
    } else {
      int ret = basics::VelocyPackHelper::compare(it, maxValue, true, options);
      if (ret > 0) {
        maxValue = it;
        maxWithValues.clear();
        maxWithValues.push_back(withSlice.at(i));
      } else if (ret == 0) {
        maxWithValues.push_back(withSlice.at(i));
      }
    }
  }
  if (maxWithValues.empty()) {
    return AqlValue(AqlValueHintNull());
  }
  transaction::BuilderLeaser builder(trx);
  builder->openArray();

  for (auto i : maxWithValues) {
    builder->add(i.resolveExternal());
  }
  builder->close();
  return AqlValue(builder->slice(), builder->size());
}

AqlValue functions::MinNWith(ExpressionContext* expressionContext,
                             AstNode const&,
                             VPackFunctionParametersView parameters) {
  AqlValue const& value = extractFunctionParameterValue(parameters, 0);
  AqlValue const& nValue = extractFunctionParameterValue(parameters, 1);
  AqlValue const& withValue = extractFunctionParameterValue(parameters, 2);

  if (!value.isArray() || !withValue.isArray()) {
    // not an array
    registerWarning(expressionContext, "MINNWITH",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  if (!nValue.isNumber() || nValue.toInt64() <= 0) {
    registerWarning(expressionContext, "MINNWITH", TRI_ERROR_TYPE_ERROR);
    return AqlValue(AqlValueHintNull());
  }
  size_t n = static_cast<size_t>(nValue.toInt64());

  transaction::Methods* trx = &expressionContext->trx();
  auto* vopts = &trx->vpackOptions();

  AqlValueMaterializer materializer(vopts);
  VPackSlice slice = materializer.slice(value);
  AqlValueMaterializer materializer2(vopts);
  VPackSlice withSlice = materializer2.slice(withValue);

  if (slice.length() != withSlice.length()) {
    registerWarning(expressionContext, "MINNWITH",
                    TRI_ERROR_QUERY_FUNCTION_RUNTIME_ERROR);
    return AqlValue(AqlValueHintNull());
  }

  using ElemType = std::pair<VPackSlice, VPackSlice>;

  auto options = trx->transactionContextPtr()->getVPackOptions();
  auto cmp = [options](ElemType lhs, ElemType rhs) {
    return basics::VelocyPackHelper::compare(lhs.first, rhs.first, true,
                                             options) < 0;
  };
  std::multiset<ElemType, decltype(cmp)> minSet(cmp);

  for (size_t i = 0; i < slice.length(); i++) {
    ElemType it = std::make_pair(slice.at(i), withSlice.at(i));
    if (it.first.isNull()) {
      continue;
    }

    if (minSet.size() >= n) {
      if (cmp(it, *minSet.rbegin())) {  // it < set中最大的数
        minSet.insert(it);
        minSet.erase(--minSet.end());
      }
    } else {
      minSet.insert(it);
    }
  }

  if (minSet.empty()) {
    return AqlValue(AqlValueHintNull());
  }
  transaction::BuilderLeaser builder(trx);
  builder->openArray();

  for (auto i : minSet) {
    builder->add(i.second.resolveExternal());
  }
  builder->close();
  return AqlValue(builder->slice(), builder->size());
}

AqlValue functions::MaxNWith(ExpressionContext* expressionContext,
                             AstNode const&,
                             VPackFunctionParametersView parameters) {
  AqlValue const& value = extractFunctionParameterValue(parameters, 0);
  AqlValue const& nValue = extractFunctionParameterValue(parameters, 1);
  AqlValue const& withValue = extractFunctionParameterValue(parameters, 2);

  if (!value.isArray() || !withValue.isArray()) {
    // not an array
    registerWarning(expressionContext, "MAXNWITH",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  if (!nValue.isNumber() || nValue.toInt64() <= 0) {
    registerWarning(expressionContext, "MAXNWITH", TRI_ERROR_TYPE_ERROR);
    return AqlValue(AqlValueHintNull());
  }
  size_t n = static_cast<size_t>(nValue.toInt64());

  transaction::Methods* trx = &expressionContext->trx();
  auto* vopts = &trx->vpackOptions();

  AqlValueMaterializer materializer(vopts);
  VPackSlice slice = materializer.slice(value);
  AqlValueMaterializer materializer2(vopts);
  VPackSlice withSlice = materializer2.slice(withValue);

  if (slice.length() != withSlice.length()) {
    registerWarning(expressionContext, "MAXNWITH",
                    TRI_ERROR_QUERY_FUNCTION_RUNTIME_ERROR);
    return AqlValue(AqlValueHintNull());
  }

  using ElemType = std::pair<VPackSlice, VPackSlice>;

  auto options = trx->transactionContextPtr()->getVPackOptions();
  auto cmp = [options](ElemType lhs, ElemType rhs) {
    return basics::VelocyPackHelper::compare(lhs.first, rhs.first, true,
                                             options) > 0;
  };
  std::multiset<ElemType, decltype(cmp)> minSet(cmp);

  for (size_t i = 0; i < slice.length(); i++) {
    ElemType it = std::make_pair(slice.at(i), withSlice.at(i));
    if (it.first.isNull()) {
      continue;
    }

    if (minSet.size() >= n) {
      if (cmp(it, *minSet.rbegin())) {  // it > set中最小的数
        minSet.insert(it);
        minSet.erase(--minSet.end());
      }
    } else {
      minSet.insert(it);
    }
  }

  if (minSet.empty()) {
    return AqlValue(AqlValueHintNull());
  }
  transaction::BuilderLeaser builder(trx);
  builder->openArray();

  for (auto i : minSet) {
    builder->add(i.second.resolveExternal());
  }
  builder->close();
  return AqlValue(builder->slice(), builder->size());
}