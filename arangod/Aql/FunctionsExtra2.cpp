#include "Aql/AqlValue.h"
#include "Functions.h"
#include <Aql/NdarrayOperatorDisableSIMD.hpp>

using namespace arangodb;
using namespace basics;
using namespace aql;

AqlValue functions::NdarrayWhere(
    arangodb::aql::ExpressionContext* expressionContext, AstNode const&,
    VPackFunctionParametersView parameters) {
  AqlValue filterValue = extractFunctionParameterValue(parameters, 0);
  AqlValue leftValue = extractFunctionParameterValue(parameters, 1);
  AqlValue rightValue = extractFunctionParameterValue(parameters, 2);

  if (!filterValue.canTurnIntoNdarray()) {
    // not a Ndarray
    registerWarning(expressionContext, "NdarrayWhere",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  auto filterPtr = filterValue.getTurnIntoNdarray();

  if (getNdarrayPtr(filterPtr)->getValueType() != Ndarray::INT_TYPE) {
    // value type error
    registerWarning(expressionContext, "NdarrayWhere",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  auto& filter = getNdarrayPtr(filterPtr)->get<int>();
  if (leftValue.canTurnIntoNdarray() && rightValue.canTurnIntoNdarray()) {
    auto leftPtr = leftValue.getTurnIntoNdarray();
    auto rightPtr = rightValue.getTurnIntoNdarray();
    return AqlValue(Ndop2::where(filter, *getNdarrayPtr(leftPtr),
                                 *getNdarrayPtr(rightPtr)));
  } else if (leftValue.canTurnIntoNdarray()) {
    auto leftPtr = leftValue.getTurnIntoNdarray();
    if (rightValue.isInt()) {
      return AqlValue(Ndop2::where(filter, *getNdarrayPtr(leftPtr),
                                   static_cast<int>(rightValue.toInt64())));
    } else if (rightValue.isfloatOrDouble()) {
      return AqlValue(
          Ndop2::where(filter, *getNdarrayPtr(leftPtr), rightValue.toDouble()));
    } else {
      registerWarning(expressionContext, "NdarrayWhere",
                      TRI_ERROR_QUERY_ARRAY_EXPECTED);
      return AqlValue(AqlValueHintNull());
    }
  } else if (rightValue.canTurnIntoNdarray()) {
    auto rightPtr = rightValue.getTurnIntoNdarray();
    if (leftValue.isInt()) {
      return AqlValue(Ndop2::where(filter,
                                   static_cast<int>(leftValue.toInt64()),
                                   *getNdarrayPtr(rightPtr)));
    } else if (leftValue.isfloatOrDouble()) {
      return AqlValue(
          Ndop2::where(filter, leftValue.toDouble(), *getNdarrayPtr(rightPtr)));
    } else {
      registerWarning(expressionContext, "NdarrayWhere",
                      TRI_ERROR_QUERY_ARRAY_EXPECTED);
      return AqlValue(AqlValueHintNull());
    }
  } else {
    if (leftValue.isInt()) {
      if (rightValue.isInt()) {
        return AqlValue(Ndop2::where(filter,
                                     static_cast<int>(leftValue.toInt64()),
                                     static_cast<int>(rightValue.toInt64())));
      } else if (rightValue.isfloatOrDouble()) {
        return AqlValue(Ndop2::where(filter,
                                     static_cast<int>(leftValue.toInt64()),
                                     rightValue.toDouble()));
      } else {
        registerWarning(expressionContext, "NdarrayWhere",
                        TRI_ERROR_QUERY_ARRAY_EXPECTED);
        return AqlValue(AqlValueHintNull());
      }

    } else if (leftValue.isfloatOrDouble()) {
      if (rightValue.isInt()) {
        return AqlValue(Ndop2::where(filter, leftValue.toDouble(),
                                     static_cast<int>(rightValue.toInt64())));
      } else if (rightValue.isfloatOrDouble()) {
        return AqlValue(
            Ndop2::where(filter, leftValue.toDouble(), rightValue.toDouble()));
      } else {
        registerWarning(expressionContext, "NdarrayWhere",
                        TRI_ERROR_QUERY_ARRAY_EXPECTED);
        return AqlValue(AqlValueHintNull());
      }
    } else {
      registerWarning(expressionContext, "NdarrayWhere",
                      TRI_ERROR_QUERY_ARRAY_EXPECTED);
      return AqlValue(AqlValueHintNull());
    }
  }
}