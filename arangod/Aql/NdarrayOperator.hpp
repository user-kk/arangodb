#include <xtensor-blas/xlinalg.hpp>
#include <xtensor/xmath.hpp>
#include <xtensor/xstrided_view.hpp>
#include <xtensor/xtensor_forward.hpp>
#include "Ndarray.hpp"
namespace arangodb {
namespace aql {

class NdarrayOperator {
 public:
  enum BinaryOperator { ADD, SUB, MUL, DIV, MOD, MATMUL };
  static Ndarray* reshape(Ndarray* array, const std::vector<int>& shape) {
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
  static Ndarray* transpose(Ndarray* v, const std::vector<int>& permutation) {
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
  static Ndarray* abs(Ndarray* array) {
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
  static Ndarray* sqrt(Ndarray* array) {
    Ndarray* ret = new Ndarray;
    xt::xarray<double> v;
    std::visit([&v](auto& data) { v = xt::sqrt(data); }, array->_data);
    ret->_data = std::move(v);
    ret->_type = Ndarray::DOUBLE_TYPE;
    return ret;
  }
  static Ndarray* power(Ndarray* array, double i) {
    Ndarray* ret = new Ndarray;
    xt::xarray<double> v;
    std::visit([&v, i](auto& data) { v = xt::pow(data, i); }, array->_data);
    ret->_data = std::move(v);
    ret->_type = Ndarray::DOUBLE_TYPE;
    return ret;
  }

  static Ndarray* exp(Ndarray* array) {
    Ndarray* ret = new Ndarray;
    xt::xarray<double> v;
    std::visit([&v](auto& data) { v = xt::exp(data); }, array->_data);
    ret->_data = std::move(v);
    ret->_type = Ndarray::DOUBLE_TYPE;
    return ret;
  }

  static Ndarray* inv(Ndarray* array) {
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
  static Ndarray* slice(Ndarray* array, const xt::xstrided_slice_vector& k) {
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
          ret->_data.emplace<xt::xarray<T>>(xt::linalg::dot(lhs, rhs));
        }
        break;
      }
    }

    ret->setType<T>();
    return ret;
  }
};

using Ndop = NdarrayOperator;

}  // namespace aql
}  // namespace arangodb