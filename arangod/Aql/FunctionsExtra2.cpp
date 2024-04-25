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
  if (filterValue.canTurnIntoNdarray()) {
    filterValue.turnIntoNdarray();
  }
  if (leftValue.canTurnIntoNdarray()) {
    leftValue.turnIntoNdarray();
  }
  if (rightValue.canTurnIntoNdarray()) {
    rightValue.turnIntoNdarray();
  }
  if (!filterValue.isNdArray()) {
    // not a Ndarray
    registerWarning(expressionContext, "NdarrayWhere",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }

  if (filterValue.getNdArray()->getValueType() != Ndarray::INT_TYPE) {
    // value type error
    registerWarning(expressionContext, "NdarrayWhere",
                    TRI_ERROR_QUERY_ARRAY_EXPECTED);
    return AqlValue(AqlValueHintNull());
  }
  auto& filter = filterValue.getNdArray()->get<int>();
  if (leftValue.isNdArray() && rightValue.isNdArray()) {
    return AqlValue(Ndop2::where(filter, *leftValue.getNdArray(),
                                 *rightValue.getNdArray()));
  } else if (leftValue.isNdArray()) {
    if (rightValue.isInt()) {
      return AqlValue(Ndop2::where(filter, *leftValue.getNdArray(),
                                   static_cast<int>(rightValue.toInt64())));
    } else if (rightValue.isfloatOrDouble()) {
      return AqlValue(
          Ndop2::where(filter, *leftValue.getNdArray(), rightValue.toDouble()));
    } else {
      registerWarning(expressionContext, "NdarrayWhere",
                      TRI_ERROR_QUERY_ARRAY_EXPECTED);
      return AqlValue(AqlValueHintNull());
    }
  } else if (rightValue.isNdArray()) {
    if (leftValue.isInt()) {
      return AqlValue(Ndop2::where(filter,
                                   static_cast<int>(leftValue.toInt64()),
                                   *rightValue.getNdArray()));
    } else if (leftValue.isfloatOrDouble()) {
      return AqlValue(
          Ndop2::where(filter, leftValue.toDouble(), *rightValue.getNdArray()));
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