#pragma once
#include <variant>
#include <xtensor-blas/xlinalg.hpp>
#include <xtensor/xmath.hpp>
#include <xtensor/xstrided_view.hpp>
#include <xtensor/xtensor_forward.hpp>
#include <xtensor/xrandom.hpp>
#include "Ndarray.hpp"
#include <cstdlib>
namespace arangodb {
namespace aql {

class NdarrayOperator {
 public:
  enum BinaryOperator { ADD, SUB, MUL, DIV, MOD, MATMUL };
  enum SingleAggOperator { SUM, COUNT_NON_ZERO };
  enum CompareOperator { EQ, NE, LT, LE, GT, GE, AND, OR };
  static Ndarray* reshape(const Ndarray* array, const std::vector<int>& shape) {
    Ndarray* ret = new Ndarray(*array);
    std::visit([&shape](auto& data) { data.reshape(shape); }, ret->_data);
    return ret;
  }
  template<typename T1, typename T2>
  static Ndarray* compute(const T1& lhs, const T2& rhs, BinaryOperator op) {
    if constexpr (std::is_same_v<Ndarray, T1> &&
                  std::is_same_v<Ndarray, T2>) {  // 都是ndarry
      switch (lhs._type) {
        case Ndarray::INT_TYPE: {
          switch (rhs._type) {
            case Ndarray::INT_TYPE: {
              return makeNdarry<int>(lhs.template get<int>(),
                                     rhs.template get<int>(), op);
            }
            case Ndarray::FLOAT_TYPE: {
              return makeNdarry<float>(lhs.template get<int>(),
                                       rhs.template get<float>(), op);
            }
            case Ndarray::DOUBLE_TYPE: {
              return makeNdarry<double>(lhs.template get<int>(),
                                        rhs.template get<double>(), op);
            }
            case Ndarray::ERROR: {
              THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                             "Not initialized");
            }
          }
        }
        case Ndarray::FLOAT_TYPE: {
          switch (rhs._type) {
            case Ndarray::INT_TYPE: {
              return makeNdarry<float>(lhs.template get<float>(),
                                       rhs.template get<int>(), op);
            }
            case Ndarray::FLOAT_TYPE: {
              return makeNdarry<float>(lhs.template get<float>(),
                                       rhs.template get<float>(), op);
            }
            case Ndarray::DOUBLE_TYPE: {
              return makeNdarry<double>(lhs.template get<float>(),
                                        rhs.template get<double>(), op);
            }
            case Ndarray::ERROR: {
              THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                             "Not initialized");
            }
          }
        }
        case Ndarray::DOUBLE_TYPE: {
          switch (rhs._type) {
            case Ndarray::INT_TYPE: {
              return makeNdarry<double>(lhs.template get<double>(),
                                        rhs.template get<int>(), op);
            }
            case Ndarray::FLOAT_TYPE: {
              return makeNdarry<double>(lhs.template get<double>(),
                                        rhs.template get<float>(), op);
            }
            case Ndarray::DOUBLE_TYPE: {
              return makeNdarry<double>(lhs.template get<double>(),
                                        rhs.template get<double>(), op);
            }
            case Ndarray::ERROR: {
              THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                             "Not initialized");
            }
          }
        }
        case Ndarray::ERROR: {
          THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                         "Not initialized");
        }
      }
    } else if constexpr (std::is_same_v<Ndarray, T1> &&
                         !std::is_same_v<Ndarray, T2>) {  // lhs是,rhs不是
      switch (lhs._type) {
        case Ndarray::INT_TYPE: {
          if constexpr (std::is_same_v<T2, int>) {
            return makeNdarry<int>(lhs.template get<int>(), rhs, op);
          } else if constexpr (std::is_same_v<
                                   T2, double>) {  // 标量不会传进来float型
            return makeNdarry<double>(lhs.template get<int>(), rhs, op);
          }
        }
        case Ndarray::FLOAT_TYPE: {
          if constexpr (std::is_same_v<T2, int>) {
            return makeNdarry<float>(lhs.template get<float>(), rhs, op);
          } else if constexpr (std::is_same_v<
                                   T2, double>) {  // 标量不会传进来float型
            return makeNdarry<double>(lhs.template get<float>(), rhs, op);
          }
        }
        case Ndarray::DOUBLE_TYPE: {
          if constexpr (std::is_same_v<T2, int>) {
            return makeNdarry<double>(lhs.template get<double>(), rhs, op);
          } else if constexpr (std::is_same_v<
                                   T2, double>) {  // 标量不会传进来float型
            return makeNdarry<double>(lhs.template get<double>(), rhs, op);
          }
        }
        case Ndarray::ERROR: {
          THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                         "Not initialized");
        }
      }
    } else if constexpr (!std::is_same_v<Ndarray, T1> &&
                         std::is_same_v<Ndarray, T2>) {  // lhs不是,rhs是
      switch (rhs._type) {
        case Ndarray::INT_TYPE: {
          if constexpr (std::is_same_v<T1, int>) {
            return makeNdarry<int>(lhs, rhs.template get<int>(), op);
          } else if constexpr (std::is_same_v<
                                   T1, double>) {  // 标量不会传进来float型
            return makeNdarry<double>(lhs, rhs.template get<int>(), op);
          }
        }
        case Ndarray::FLOAT_TYPE: {
          if constexpr (std::is_same_v<T1, int>) {
            return makeNdarry<float>(lhs, rhs.template get<float>(), op);
          } else if constexpr (std::is_same_v<
                                   T1, double>) {  // 标量不会传进来float型
            return makeNdarry<double>(lhs, rhs.template get<float>(), op);
          }
        }
        case Ndarray::DOUBLE_TYPE: {
          if constexpr (std::is_same_v<T1, int>) {
            return makeNdarry<double>(lhs, rhs.template get<double>(), op);
          } else if constexpr (std::is_same_v<
                                   T1, double>) {  // 标量不会传进来float型
            return makeNdarry<double>(lhs, rhs.template get<double>(), op);
          }
        }
        case Ndarray::ERROR: {
          THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                         "Not initialized");
        }
      }
    }
    THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                   "They can't all be scalars");
  }

  static Ndarray* aggCompute(const Ndarray* v, SingleAggOperator op,
                             std::vector<size_t> axis) {
    Ndarray* ret = new Ndarray;
    switch (op) {
      case SUM: {
        if (v->_type == Ndarray::INT_TYPE) {
          if (axis.empty()) {
            ret->_data.emplace<xt::xarray<int>>(xt::sum(v->get<int>()));
          } else {
            ret->_data.emplace<xt::xarray<int>>(xt::sum(v->get<int>(), axis));
          }
        } else if (v->_type == Ndarray::DOUBLE_TYPE) {
          if (axis.empty()) {
            ret->_data.emplace<xt::xarray<double>>(xt::sum(v->get<double>()));
          } else {
            ret->_data.emplace<xt::xarray<double>>(
                xt::sum(v->get<double>(), axis));
          }
        } else if (v->_type == Ndarray::FLOAT_TYPE) {
          if (axis.empty()) {
            ret->_data.emplace<xt::xarray<float>>(xt::sum(v->get<float>()));
          } else {
            ret->_data.emplace<xt::xarray<float>>(
                xt::sum(v->get<float>(), axis));
          }
        }
        ret->_type = v->_type;
        break;
      }
      case COUNT_NON_ZERO: {
        std::visit(
            [ret, &axis](auto data) {
              if (axis.empty()) {
                ret->_data.emplace<xt::xarray<int>>(xt::count_nonzero(data));
              } else {
                ret->_data.emplace<xt::xarray<int>>(
                    xt::count_nonzero(data, axis));
              }
            },
            v->_data);
        ret->_type = Ndarray::INT_TYPE;
        break;
      }
    }
    return ret;
  }

  template<typename T1, typename T2>
  static Ndarray* CompareCompute(const T1& lhs, const T2& rhs,
                                 CompareOperator op) {
    if constexpr (std::is_same_v<Ndarray, T1> &&
                  std::is_same_v<Ndarray, T2>) {  // 都是ndarry
      switch (lhs._type) {
        case Ndarray::INT_TYPE: {
          switch (rhs._type) {
            case Ndarray::INT_TYPE: {
              return makeNdarry(lhs.template get<int>(),
                                rhs.template get<int>(), op);
            }
            case Ndarray::FLOAT_TYPE: {
              return makeNdarry(lhs.template get<int>(),
                                rhs.template get<float>(), op);
            }
            case Ndarray::DOUBLE_TYPE: {
              return makeNdarry(lhs.template get<int>(),
                                rhs.template get<double>(), op);
            }
            case Ndarray::ERROR: {
              THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                             "Not initialized");
            }
          }
        }
        case Ndarray::FLOAT_TYPE: {
          switch (rhs._type) {
            case Ndarray::INT_TYPE: {
              return makeNdarry(lhs.template get<float>(),
                                rhs.template get<int>(), op);
            }
            case Ndarray::FLOAT_TYPE: {
              return makeNdarry(lhs.template get<float>(),
                                rhs.template get<float>(), op);
            }
            case Ndarray::DOUBLE_TYPE: {
              return makeNdarry(lhs.template get<float>(),
                                rhs.template get<double>(), op);
            }
            case Ndarray::ERROR: {
              THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                             "Not initialized");
            }
          }
        }
        case Ndarray::DOUBLE_TYPE: {
          switch (rhs._type) {
            case Ndarray::INT_TYPE: {
              return makeNdarry(lhs.template get<double>(),
                                rhs.template get<int>(), op);
            }
            case Ndarray::FLOAT_TYPE: {
              return makeNdarry(lhs.template get<double>(),
                                rhs.template get<float>(), op);
            }
            case Ndarray::DOUBLE_TYPE: {
              return makeNdarry(lhs.template get<double>(),
                                rhs.template get<double>(), op);
            }
            case Ndarray::ERROR: {
              THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                             "Not initialized");
            }
          }
        }
        case Ndarray::ERROR: {
          THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                         "Not initialized");
        }
      }
    } else if constexpr (std::is_same_v<Ndarray, T1> &&
                         !std::is_same_v<Ndarray, T2>) {  // lhs是,rhs不是
      switch (lhs._type) {
        case Ndarray::INT_TYPE: {
          if constexpr (std::is_same_v<T2, int>) {
            return makeNdarry(lhs.template get<int>(), rhs, op);
          } else if constexpr (std::is_same_v<
                                   T2, double>) {  // 标量不会传进来float型
            return makeNdarry(lhs.template get<int>(), rhs, op);
          }
        }
        case Ndarray::FLOAT_TYPE: {
          if constexpr (std::is_same_v<T2, int>) {
            return makeNdarry(lhs.template get<float>(), rhs, op);
          } else if constexpr (std::is_same_v<
                                   T2, double>) {  // 标量不会传进来float型
            return makeNdarry(lhs.template get<float>(), rhs, op);
          }
        }
        case Ndarray::DOUBLE_TYPE: {
          if constexpr (std::is_same_v<T2, int>) {
            return makeNdarry(lhs.template get<double>(), rhs, op);
          } else if constexpr (std::is_same_v<
                                   T2, double>) {  // 标量不会传进来float型
            return makeNdarry(lhs.template get<double>(), rhs, op);
          }
        }
        case Ndarray::ERROR: {
          THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                         "Not initialized");
        }
      }
    } else if constexpr (!std::is_same_v<Ndarray, T1> &&
                         std::is_same_v<Ndarray, T2>) {  // lhs不是,rhs是
      switch (rhs._type) {
        case Ndarray::INT_TYPE: {
          if constexpr (std::is_same_v<T1, int>) {
            return makeNdarry(lhs, rhs.template get<int>(), op);
          } else if constexpr (std::is_same_v<
                                   T1, double>) {  // 标量不会传进来float型
            return makeNdarry(lhs, rhs.template get<int>(), op);
          }
        }
        case Ndarray::FLOAT_TYPE: {
          if constexpr (std::is_same_v<T1, int>) {
            return makeNdarry(lhs, rhs.template get<float>(), op);
          } else if constexpr (std::is_same_v<
                                   T1, double>) {  // 标量不会传进来float型
            return makeNdarry(lhs, rhs.template get<float>(), op);
          }
        }
        case Ndarray::DOUBLE_TYPE: {
          if constexpr (std::is_same_v<T1, int>) {
            return makeNdarry(lhs, rhs.template get<double>(), op);
          } else if constexpr (std::is_same_v<
                                   T1, double>) {  // 标量不会传进来float型
            return makeNdarry(lhs, rhs.template get<double>(), op);
          }
        }
        case Ndarray::ERROR: {
          THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                         "Not initialized");
        }
      }
    }
    THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                   "They can't all be scalars");
  }

  static Ndarray* transpose(const Ndarray* v,
                            const std::vector<int>& permutation) {
    Ndarray* ret = new Ndarray;
    if (v->_type == Ndarray::INT_TYPE) {
      if (permutation.empty()) {
        ret->_data.emplace<xt::xarray<int>>(xt::transpose(v->get<int>()));
      } else {
        ret->_data.emplace<xt::xarray<int>>(
            xt::transpose(v->get<int>(), permutation));
      }

    } else if (v->_type == Ndarray::DOUBLE_TYPE) {
      if (permutation.empty()) {
        ret->_data.emplace<xt::xarray<double>>(xt::transpose(v->get<double>()));
      } else {
        ret->_data.emplace<xt::xarray<double>>(
            xt::transpose(v->get<double>(), permutation));
      }
    } else if (v->_type == Ndarray::FLOAT_TYPE) {
      if (permutation.empty()) {
        ret->_data.emplace<xt::xarray<float>>(xt::transpose(v->get<float>()));
      } else {
        ret->_data.emplace<xt::xarray<float>>(
            xt::transpose(v->get<float>(), permutation));
      }
    }
    ret->_type = v->_type;
    return ret;
  }
  static Ndarray* abs(const Ndarray* array) {
    Ndarray* ret = new Ndarray;
    if (array->_type == Ndarray::INT_TYPE) {
      xt::xarray<int> v = xt::abs(array->get<int>());
      ret->_data = std::move(v);
    } else if (array->_type == Ndarray::DOUBLE_TYPE) {
      xt::xarray<double> v = xt::abs(array->get<double>());
      ret->_data = std::move(v);
    } else if (array->_type == Ndarray::FLOAT_TYPE) {
      xt::xarray<float> v = xt::abs(array->get<float>());
      ret->_data = std::move(v);
    }
    ret->_type = array->_type;
    return ret;
  }
  static Ndarray* sqrt(const Ndarray* array) {
    Ndarray* ret = new Ndarray;
    xt::xarray<double> v;
    std::visit([&v](auto& data) { v = xt::sqrt(data); }, array->_data);
    ret->_data = std::move(v);
    ret->_type = Ndarray::DOUBLE_TYPE;
    return ret;
  }
  static Ndarray* power(const Ndarray* array, double i) {
    Ndarray* ret = new Ndarray;
    xt::xarray<double> v;
    std::visit([&v, i](auto& data) { v = xt::pow(data, i); }, array->_data);
    ret->_data = std::move(v);
    ret->_type = Ndarray::DOUBLE_TYPE;
    return ret;
  }

  static double norm2(const Ndarray* array) {
    if (array->_type == Ndarray::INT_TYPE) {
      return xt::sqrt(xt::sum(xt::pow(array->get<int>(), 2)))[{}];
    } else if (array->_type == Ndarray::FLOAT_TYPE) {
      return xt::sqrt(xt::sum(xt::pow(array->get<float>(), 2)))[{}];
    } else {
      return xt::sqrt(xt::sum(xt::pow(array->get<double>(), 2)))[{}];
    }
  }
  // min-max标准化
  static Ndarray* normalization(const Ndarray* array) {
    Ndarray* ret = new Ndarray;
    ret->_type = Ndarray::DOUBLE_TYPE;
    if (array->_type == Ndarray::INT_TYPE) {
      auto& matrix = array->get<int>();

      xt::xarray<double> max_values = xt::amax(matrix, 1);
      // 防止除0
      for (auto& i : max_values) {
        if (std::abs(i) < 1e-6) {
          i = 1;
        }
      }
      xt::xarray<double> result =
          xt::eval(matrix / xt::expand_dims(max_values, 1));
      ret->_data = std::move(result);

    } else if (array->_type == Ndarray::FLOAT_TYPE) {
      auto& matrix = array->get<float>();
      xt::xarray<double> max_values = xt::amax(matrix, 1);
      // 防止除0
      for (auto& i : max_values) {
        if (std::abs(i) < 1e-6) {
          i = 1;
        }
      }
      xt::xarray<double> result =
          xt::eval(matrix / xt::expand_dims(max_values, 1));
      ret->_data = std::move(result);

    } else {
      auto& matrix = array->get<double>();
      xt::xarray<double> max_values = xt::amax(matrix, 1);
      // 防止除0
      for (auto& i : max_values) {
        if (std::abs(i) < 1e-6) {
          i = 1;
        }
      }
      xt::xarray<double> result =
          xt::eval(matrix / xt::expand_dims(max_values, 1));
      ret->_data = std::move(result);
    }

    return ret;
  }

  static Ndarray* rand(const std::vector<int>& shape) {
    Ndarray* ret = new Ndarray;
    ret->_type = Ndarray::FLOAT_TYPE;
    xt::xarray<float> v = xt::random::rand<float>(shape);
    ret->_data = std::move(v);
    return ret;
    return nullptr;
  }

  static Ndarray* exp(const Ndarray* array) {
    Ndarray* ret = new Ndarray;
    xt::xarray<double> v;
    std::visit([&v](auto& data) { v = xt::exp(data); }, array->_data);
    ret->_data = std::move(v);
    ret->_type = Ndarray::DOUBLE_TYPE;
    return ret;
  }

  static Ndarray* inv(const Ndarray* array) {
    Ndarray* ret = new Ndarray;
    if (array->_type == Ndarray::INT_TYPE) {
      xt::xarray<double> v =
          xt::linalg::inv(xt::xarray<double>(array->get<int>()));
      ret->_data = std::move(v);
      ret->_type = Ndarray::DOUBLE_TYPE;
    } else if (array->_type == Ndarray::DOUBLE_TYPE) {
      xt::xarray<double> v = xt::linalg::inv(array->get<double>());
      ret->_data = std::move(v);
      ret->_type = Ndarray::DOUBLE_TYPE;
    } else if (array->_type == Ndarray::FLOAT_TYPE) {
      xt::xarray<float> v = xt::linalg::inv(array->get<float>());
      ret->_data = std::move(v);
      ret->_type = Ndarray::FLOAT_TYPE;
    }
    return ret;
  }
  static Ndarray* slice(const Ndarray* array,
                        const xt::xstrided_slice_vector& k) {
    Ndarray* ret = new Ndarray;
    if (array->_type == Ndarray::INT_TYPE) {
      xt::xarray<int> v = xt::strided_view(array->get<int>(), k);
      ret->_data = std::move(v);
    } else if (array->_type == Ndarray::DOUBLE_TYPE) {
      xt::xarray<double> v = xt::strided_view(array->get<double>(), k);
      ret->_data = std::move(v);
    } else if (array->_type == Ndarray::FLOAT_TYPE) {
      xt::xarray<float> v = xt::strided_view(array->get<float>(), k);
      ret->_data = std::move(v);
    }
    ret->_type = array->_type;
    return ret;
  }
  static Ndarray* getNot(const Ndarray* array) {
    Ndarray* ret = new Ndarray;
    if (array->_type != Ndarray::INT_TYPE) {
      THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                     "Logical operators must be int");
    }
    ret->_data.emplace<xt::xarray<int>>(!array->get<int>());
    ret->setType<int>();
    return ret;
  }
  static Ndarray* getNegative(const Ndarray* array) {
    Ndarray* ret = new Ndarray;
    if (array->_type == Ndarray::INT_TYPE) {
      xt::xarray<int> v = (-array->get<int>());
      ret->_data = std::move(v);
    } else if (array->_type == Ndarray::DOUBLE_TYPE) {
      xt::xarray<double> v = (-array->get<double>());
      ret->_data = std::move(v);
    } else if (array->_type == Ndarray::FLOAT_TYPE) {
      xt::xarray<float> v = (-array->get<float>());
      ret->_data = std::move(v);
    }

    ret->_type = array->_type;
    return ret;
  }

 private:
  template<typename T, typename T1, typename T2>
  static Ndarray* makeNdarry(const T1& lhs, const T2& rhs, BinaryOperator op) {
    Ndarray* ret = new Ndarray;
    // 计算
    switch (op) {
      case ADD: {
        ret->_data.emplace<xt::xarray<T>>(lhs + rhs);
        break;
      }
      case SUB: {
        ret->_data.emplace<xt::xarray<T>>(lhs - rhs);
        break;
      }
      case MUL: {
        ret->_data.emplace<xt::xarray<T>>(lhs * rhs);
        break;
      }
      case DIV: {
        ret->_data.emplace<xt::xarray<T>>(lhs / rhs);
        break;
      }
      case MOD: {
        if constexpr (std::is_same_v<int, T> &&
                      ((std::is_same_v<T1, xt::xarray<int>> &&
                        std::is_same_v<int, T2>) ||
                       (std::is_same_v<T2, xt::xarray<int>> &&
                        std::is_same_v<int, T1>) ||
                       (std::is_same_v<T1, xt::xarray<int>> &&
                        std::is_same_v<T2, xt::xarray<int>>))) {
          ret->_data.emplace<xt::xarray<T>>(lhs % rhs);
        } else {
          THROW_ARANGO_EXCEPTION_MESSAGE(
              TRI_ERROR_QUERY_PARSE,
              "Only integer mod is supported in Ndarray");
        }
        break;
      }
      case MATMUL: {
        if constexpr (is_xarray<T1>::value && is_xarray<T2>::value) {
          xt::xarray<float> tmpl = lhs;
          xt::xarray<double> tmpr = rhs;

          xt::xarray<T> tmp = xt::linalg::dot(tmpl, tmpr);
          ret->_data = std::move(tmp);
        }
        break;
      }
    }

    ret->setType<T>();
    return ret;
  }

  template<typename T1, typename T2>
  static Ndarray* makeNdarry(const T1& lhs, const T2& rhs, CompareOperator op) {
    Ndarray* ret = new Ndarray;
    // 计算
    switch (op) {
      case EQ: {
        ret->_data.emplace<xt::xarray<int>>(xt::equal(lhs, rhs));
        break;
      }
      case NE: {
        ret->_data.emplace<xt::xarray<int>>(xt::not_equal(lhs, rhs));
        break;
      }
      case LT: {
        ret->_data.emplace<xt::xarray<int>>(lhs < rhs);
        break;
      }
      case LE: {
        ret->_data.emplace<xt::xarray<int>>(lhs <= rhs);
        break;
      }
      case GT: {
        ret->_data.emplace<xt::xarray<int>>(lhs > rhs);
        break;
      }
      case GE: {
        ret->_data.emplace<xt::xarray<int>>(lhs >= rhs);
        break;
      }
      case AND: {
        ret->_data.emplace<xt::xarray<int>>(lhs && rhs);
        break;
      }
      case OR: {
        ret->_data.emplace<xt::xarray<int>>(lhs || rhs);
        break;
      }
    }
    ret->setType<int>();
    return ret;
  }
};

using Ndop = NdarrayOperator;

}  // namespace aql
}  // namespace arangodb