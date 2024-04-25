#include "Aql/AqlValue.h"
#include "Aql/NdarrayOperator.hpp"
#include "Functions.h"
#include <cassert>
#include <cstddef>
#include <set>
#include <string>
#include <utility>

#include "Aql/AqlFunctionFeature.h"
#include "Aql/AqlValueMaterializer.h"
#include "Aql/Expression.h"
#include "Aql/ExpressionContext.h"
#include "Aql/Function.h"
#include "Aql/Query.h"

#include "Basics/VelocyPackHelper.h"
#include "Indexes/Index.h"
#include "Rest/Version.h"
#include "StorageEngine/TransactionState.h"
#include "Transaction/Context.h"
#include "Transaction/Helpers.h"
#include "Transaction/Methods.h"
#include "Utils/ExecContext.h"

#include <velocypack/Collection.h>
#include <velocypack/Dumper.h>
#include <velocypack/Iterator.h>
#include <velocypack/Sink.h>

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
AqlValue functions::AggToNdArrayf(
    arangodb::aql::ExpressionContext* expressionContext, AstNode const&,
    VPackFunctionParametersView parameters) {
  THROW_ARANGO_EXCEPTION_MESSAGE(
      TRI_ERROR_QUERY_PARSE,
      "ToArrayf() must be invoked as aggregate function");
}
AqlValue functions::AggToNdArrayd(arangodb::aql::ExpressionContext*,
                                  AstNode const&, VPackFunctionParametersView) {
  THROW_ARANGO_EXCEPTION_MESSAGE(
      TRI_ERROR_QUERY_PARSE,
      "ToArrayf() must be invoked as aggregate function");
};
AqlValue functions::AggToNdArrayi(arangodb::aql::ExpressionContext*,
                                  AstNode const&, VPackFunctionParametersView) {
  THROW_ARANGO_EXCEPTION_MESSAGE(
      TRI_ERROR_QUERY_PARSE,
      "ToArrayf() must be invoked as aggregate function");
}

AqlValue functions::ToNdArrayf(
    arangodb::aql::ExpressionContext* expressionContext, AstNode const&,
    VPackFunctionParametersView parameters) {
  AqlValue value = extractFunctionParameterValue(parameters, 0);
  AqlValue nameValue = extractFunctionParameterValue(parameters, 1);
  AqlValue shapeValue = extractFunctionParameterValue(parameters, 2);
  if (!value.isArray() && !value.canTurnIntoNdarray()) {
    // not an array or a Ndarray
    registerWarning(expressionContext, "ToNdArrayf",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }

  transaction::Methods* trx = &expressionContext->trx();
  auto* vopts = &trx->vpackOptions();

  AqlValueMaterializer materializer(vopts);
  VPackSlice slice;

  if (value.isRange()) {
    slice = materializer.slice(value);
  }

  if (!nameValue.isNone() && !nameValue.isArray()) {
    registerWarning(expressionContext, "ToNdArrayf",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  if (!shapeValue.isNone() && !shapeValue.isArray()) {
    registerWarning(expressionContext, "ToNdArrayf",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  std::vector<std::optional<std::string>> names;
  if (!nameValue.isNone()) {
    names.reserve(nameValue.length());
    for (auto i : VPackArrayIterator(nameValue.slice())) {
      if (i.isString()) {
        names.push_back(i.toString());
      } else {
        names.push_back(std::nullopt);
      }
    }
  }

  std::vector<int> shape;
  if (!shapeValue.isNone()) {
    shape.reserve(shapeValue.length());
    for (auto i : VPackArrayIterator(shapeValue.slice())) {
      shape.push_back(i.getInt());
    }
  }
  if (value.isArray()) {
    return AqlValue(Ndarray::fromVPackArray<float>(
        value.isRange() ? slice : value.slice(), names, shape));
  } else if (value.isVackNdarray()) {
    return AqlValue(
        Ndarray::fromOtherVPack<float>(value.slice(), names, shape));
  } else {  // Ndarray
    return AqlValue(
        Ndarray::fromOtherNdarray<float>(value.getNdArray(), names, shape));
  }
}

AqlValue functions::ToNdArrayd(
    arangodb::aql::ExpressionContext* expressionContext, AstNode const&,
    VPackFunctionParametersView parameters) {
  AqlValue value = extractFunctionParameterValue(parameters, 0);
  AqlValue nameValue = extractFunctionParameterValue(parameters, 1);
  AqlValue shapeValue = extractFunctionParameterValue(parameters, 2);
  if (!value.isArray() && !value.canTurnIntoNdarray()) {
    // not an array
    registerWarning(expressionContext, "ToNdArrayd",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }

  transaction::Methods* trx = &expressionContext->trx();
  auto* vopts = &trx->vpackOptions();

  AqlValueMaterializer materializer(vopts);
  VPackSlice slice;

  if (value.isRange()) {
    slice = materializer.slice(value);
  }

  if (!nameValue.isNone() && !nameValue.isArray()) {
    registerWarning(expressionContext, "ToNdArrayd",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  if (!shapeValue.isNone() && !shapeValue.isArray()) {
    registerWarning(expressionContext, "ToNdArrayd",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  std::vector<std::optional<std::string>> names;
  if (!nameValue.isNone()) {
    names.reserve(nameValue.length());
    for (auto i : VPackArrayIterator(nameValue.slice())) {
      if (i.isString()) {
        names.push_back(i.toString());
      } else {
        names.push_back(std::nullopt);
      }
    }
  }

  std::vector<int> shape;
  if (!shapeValue.isNone()) {
    shape.reserve(shapeValue.length());
    for (auto i : VPackArrayIterator(shapeValue.slice())) {
      shape.push_back(i.getInt());
    }
  }

  if (value.isArray()) {
    return AqlValue(Ndarray::fromVPackArray<double>(
        value.isRange() ? slice : value.slice(), names, shape));
  } else if (value.isVackNdarray()) {
    return AqlValue(
        Ndarray::fromOtherVPack<double>(value.slice(), names, shape));
  } else {  // Ndarray
    return AqlValue(
        Ndarray::fromOtherNdarray<double>(value.getNdArray(), names, shape));
  }
}

AqlValue functions::ToNdArrayi(
    arangodb::aql::ExpressionContext* expressionContext, AstNode const&,
    VPackFunctionParametersView parameters) {
  AqlValue value = extractFunctionParameterValue(parameters, 0);
  AqlValue nameValue = extractFunctionParameterValue(parameters, 1);
  AqlValue shapeValue = extractFunctionParameterValue(parameters, 2);
  if (!value.isArray() && !value.canTurnIntoNdarray()) {
    // not an array
    registerWarning(expressionContext, "ToNdArrayi",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }

  transaction::Methods* trx = &expressionContext->trx();
  auto* vopts = &trx->vpackOptions();

  AqlValueMaterializer materializer(vopts);
  VPackSlice slice;

  if (value.isRange()) {
    slice = materializer.slice(value);
  }

  if (!nameValue.isNone() && !nameValue.isArray()) {
    registerWarning(expressionContext, "ToNdArrayi",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  if (!shapeValue.isNone() && !shapeValue.isArray()) {
    registerWarning(expressionContext, "ToNdArrayi",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  std::vector<std::optional<std::string>> names;
  if (!nameValue.isNone()) {
    names.reserve(nameValue.length());
    for (auto i : VPackArrayIterator(nameValue.slice())) {
      if (i.isString()) {
        names.push_back(i.toString());
      } else {
        names.push_back(std::nullopt);
      }
    }
  }

  std::vector<int> shape;
  if (!shapeValue.isNone()) {
    shape.reserve(shapeValue.length());
    for (auto i : VPackArrayIterator(shapeValue.slice())) {
      shape.push_back(i.getInt());
    }
  }

  if (value.isArray()) {
    return AqlValue(Ndarray::fromVPackArray<int>(
        value.isRange() ? slice : value.slice(), names, shape));
  } else if (value.isVackNdarray()) {
    return AqlValue(Ndarray::fromOtherVPack<int>(value.slice(), names, shape));
  } else {  // Ndarray
    return AqlValue(
        Ndarray::fromOtherNdarray<int>(value.getNdArray(), names, shape));
  }
}

AqlValue functions::MatMul(ExpressionContext* expressionContext, AstNode const&,
                           VPackFunctionParametersView parameters) {
  AqlValue lhs = extractFunctionParameterValue(parameters, 0);
  AqlValue rhs = extractFunctionParameterValue(parameters, 1);
  if (lhs.canTurnIntoNdarray()) {
    lhs.turnIntoNdarray();
  }
  if (rhs.canTurnIntoNdarray()) {
    rhs.turnIntoNdarray();
  }
  if (!lhs.isNdArray() || !rhs.isNdArray()) {
    // not a Ndarray
    registerWarning(expressionContext, "MatMul",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  return AqlValue(Ndop::compute(*(lhs.getNdArray()), *(rhs.getNdArray()),
                                Ndop::BinaryOperator::MATMUL));
}

AqlValue functions::MatTranspose(ExpressionContext* expressionContext,
                                 AstNode const&,
                                 VPackFunctionParametersView parameters) {
  AqlValue value = extractFunctionParameterValue(parameters, 0);
  AqlValue axisValue = extractFunctionParameterValue(parameters, 1);
  if (value.canTurnIntoNdarray()) {
    value.turnIntoNdarray();
  }
  if (!value.isNdArray()) {
    // not a Ndarray
    registerWarning(expressionContext, "MatTranspose",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  if (!axisValue.isNone() && !axisValue.isArray()) {
    registerWarning(expressionContext, "MatTranspose",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }

  std::vector<int> permutation;
  if (!axisValue.isNone()) {
    permutation.reserve(axisValue.length());
    for (auto i : VPackArrayIterator(axisValue.slice())) {
      permutation.push_back(i.getInt());
    }
  }

  return AqlValue(Ndop::transpose(value.getNdArray(), permutation));
}

AqlValue functions::Reshape(ExpressionContext* expressionContext,
                            AstNode const&,
                            VPackFunctionParametersView parameters) {
  AqlValue value = extractFunctionParameterValue(parameters, 0);
  AqlValue shapeValue = extractFunctionParameterValue(parameters, 1);
  if (value.canTurnIntoNdarray()) {
    value.turnIntoNdarray();
  }
  if (!value.isNdArray()) {
    // not a Ndarray
    registerWarning(expressionContext, "Reshape",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  if (!shapeValue.isArray() || shapeValue.length() == 0 ||
      !shapeValue.slice().at(0).isInteger()) {
    registerWarning(expressionContext, "Reshape",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  std::vector<int> shape;
  shape.reserve(shapeValue.length());
  for (auto i : VPackArrayIterator(shapeValue.slice())) {
    shape.push_back(i.getInt());
  }
  return AqlValue(Ndop::reshape(value.getNdArray(), shape));
}

AqlValue functions::Shape(ExpressionContext* expressionContext, AstNode const&,
                          VPackFunctionParametersView parameters) {
  AqlValue value = extractFunctionParameterValue(parameters, 0);
  AqlValue shapeValue = extractFunctionParameterValue(parameters, 1);
  if (!value.canTurnIntoNdarray()) {
    // not a Ndarray
    registerWarning(expressionContext, "Reshape",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  transaction::Methods* trx = &expressionContext->trx();
  transaction::BuilderLeaser builder(trx);
  if (shapeValue.isNone()) {
    value.getNdArrayShape(*builder.get());
    return AqlValue(builder->slice(), builder->size());
  }
  if (shapeValue.isInt()) {
    if (shapeValue.toInt64() < 0) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE, "out of range");
    }
    value.getNdArrayShape(*builder.get(), shapeValue.toInt64());
    return AqlValue(builder->slice(), builder->size());
  }
  if (shapeValue.isString()) {
    value.getNdArrayShape(*builder.get(), shapeValue.slice().toString());
    return AqlValue(builder->slice(), builder->size());
  }

  registerWarning(expressionContext, "Shape", TRI_ERROR_TYPE_ERROR);
  return AqlValue(AqlValueHintNull());
}

AqlValue functions::Inv(ExpressionContext* expressionContext, AstNode const&,
                        VPackFunctionParametersView parameters) {
  AqlValue value = extractFunctionParameterValue(parameters, 0);
  if (value.canTurnIntoNdarray()) {
    value.turnIntoNdarray();
  }
  if (!value.isNdArray()) {
    // not a Ndarray
    registerWarning(expressionContext, "Reshape",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  return AqlValue(Ndop::inv(value.getNdArray()));
}

AqlValue functions::Dimension(ExpressionContext* expressionContext,
                              AstNode const&,
                              VPackFunctionParametersView parameters) {
  AqlValue value = extractFunctionParameterValue(parameters, 0);
  if (value.canTurnIntoNdarray()) {
    return AqlValue(AqlValueHintUInt(value.getNdArrayDimension()));
  }

  // not a Ndarray
  registerWarning(expressionContext, "Dimension",
                  TRI_ERROR_QUERY_ARRAY_EXPECTED);
  return AqlValue(AqlValueHintNull());
}

AqlValue functions::DocumentView(
    arangodb::aql::ExpressionContext* expressionContext, AstNode const&,
    VPackFunctionParametersView parameters) {
  AqlValue value = extractFunctionParameterValue(parameters, 0);
  if (value.isVackNdarray()) {
    return value;
  }

  if (value.isNdArray()) {
    return AqlValue(value.slice());
  }
  registerWarning(expressionContext, "DocumentView",
                  TRI_ERROR_QUERY_ARRAY_EXPECTED);
  return AqlValue(AqlValueHintNull());
}

AqlValue functions::NdarraySum(
    arangodb::aql::ExpressionContext* expressionContext, AstNode const&,
    VPackFunctionParametersView parameters) {
  AqlValue value = extractFunctionParameterValue(parameters, 0);
  AqlValue axisValue = extractFunctionParameterValue(parameters, 1);

  if (value.canTurnIntoNdarray()) {
    value.turnIntoNdarray();
  }
  if (!value.isNdArray()) {
    // not a Ndarray
    registerWarning(expressionContext, "NdarraySum",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }

  if (!axisValue.isNone() && !axisValue.isArray() && !axisValue.isInt()) {
    registerWarning(expressionContext, "NdarraySum",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  if (axisValue.isInt() && axisValue.toInt64() < 0) {
    registerWarning(expressionContext, "NdarraySum",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }

  std::vector<size_t> axis;
  if (axisValue.isArray()) {
    axis.reserve(axisValue.length());
    for (auto i : VPackArrayIterator(axisValue.slice())) {
      axis.push_back(i.getInt());
    }
  } else if (axisValue.isInt()) {
    axis.push_back(axisValue.toInt64());
  }
  return AqlValue(Ndop::aggCompute(value.getNdArray(),
                                   arangodb::aql::NdarrayOperator::SUM, axis));
}

AqlValue functions::NdarrayCountNonZero(
    arangodb::aql::ExpressionContext* expressionContext, AstNode const&,
    VPackFunctionParametersView parameters) {
  AqlValue value = extractFunctionParameterValue(parameters, 0);
  AqlValue axisValue = extractFunctionParameterValue(parameters, 1);

  if (value.canTurnIntoNdarray()) {
    value.turnIntoNdarray();
  }
  if (!value.isNdArray()) {
    // not a Ndarray
    registerWarning(expressionContext, "NdarraySum",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }

  if (!axisValue.isNone() && !axisValue.isArray() && !axisValue.isInt()) {
    registerWarning(expressionContext, "NdarraySum",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  if (axisValue.isInt() && axisValue.toInt64() < 0) {
    registerWarning(expressionContext, "NdarraySum",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }

  std::vector<size_t> axis;
  if (axisValue.isArray()) {
    axis.reserve(axisValue.length());
    for (auto i : VPackArrayIterator(axisValue.slice())) {
      axis.push_back(i.getInt());
    }
  } else if (axisValue.isInt()) {
    axis.push_back(axisValue.toInt64());
  }
  return AqlValue(
      Ndop::aggCompute(value.getNdArray(),
                       arangodb::aql::NdarrayOperator::COUNT_NON_ZERO, axis));
}