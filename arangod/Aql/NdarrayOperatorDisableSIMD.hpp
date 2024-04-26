#pragma once
#undef XTENSOR_USE_XSIMD
#include "Ndarray.hpp"
#include <xtensor/xmath.hpp>
#include <xtensor/xstrided_view.hpp>
#include <xtensor/xtensor_forward.hpp>
namespace arangodb {
namespace aql {
class NdarrayOperatorDisableSIMD {
 public:
  template<typename T1, typename T2>
  static Ndarray* where(const xt::xarray<int>& filter, const T1& lhs,
                        const T2& rhs) {
    Ndarray* ret = new Ndarray;

    if constexpr (std::is_same_v<Ndarray, T1> &&
                  std::is_same_v<Ndarray, T2>) {  // 都是ndarry
      switch (lhs._type) {
        case Ndarray::INT_TYPE: {
          switch (rhs._type) {
            case Ndarray::INT_TYPE: {
              ret->setType<int>();
              ret->_data.emplace<xt::xarray<int>>(xt::where(
                  filter, lhs.template get<int>(), rhs.template get<int>()));
              return ret;
            }
            case Ndarray::FLOAT_TYPE: {
              ret->setType<float>();
              ret->_data.emplace<xt::xarray<float>>(xt::where(
                  filter, lhs.template get<int>(), rhs.template get<float>()));
              return ret;
            }
            case Ndarray::DOUBLE_TYPE: {
              ret->setType<double>();
              ret->_data.emplace<xt::xarray<double>>(xt::where(
                  filter, lhs.template get<int>(), rhs.template get<double>()));
              return ret;
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
              ret->setType<float>();
              ret->_data.emplace<xt::xarray<float>>(xt::where(
                  filter, lhs.template get<float>(), rhs.template get<int>()));
              return ret;
            }
            case Ndarray::FLOAT_TYPE: {
              ret->setType<float>();
              ret->_data.emplace<xt::xarray<float>>(
                  xt::where(filter, lhs.template get<float>(),
                            rhs.template get<float>()));
              return ret;
            }
            case Ndarray::DOUBLE_TYPE: {
              ret->setType<double>();
              ret->_data.emplace<xt::xarray<double>>(
                  xt::where(filter, lhs.template get<float>(),
                            rhs.template get<double>()));
              return ret;
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
              ret->setType<double>();
              ret->_data.emplace<xt::xarray<double>>(xt::where(
                  filter, lhs.template get<double>(), rhs.template get<int>()));
              return ret;
            }
            case Ndarray::FLOAT_TYPE: {
              ret->setType<double>();
              ret->_data.emplace<xt::xarray<double>>(
                  xt::where(filter, lhs.template get<double>(),
                            rhs.template get<float>()));
              return ret;
            }
            case Ndarray::DOUBLE_TYPE: {
              ret->setType<double>();
              ret->_data.emplace<xt::xarray<double>>(
                  xt::where(filter, lhs.template get<double>(),
                            rhs.template get<double>()));
              return ret;
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
            ret->setType<int>();
            ret->_data.emplace<xt::xarray<int>>(
                xt::where(filter, lhs.template get<int>(), rhs));
            return ret;
          } else if constexpr (std::is_same_v<
                                   T2,
                                   double>) {  // 标量不会传进来float型
            ret->setType<double>();
            ret->_data.emplace<xt::xarray<double>>(
                xt::where(filter, lhs.template get<int>(), rhs));
            return ret;
          }
        }
        case Ndarray::FLOAT_TYPE: {
          if constexpr (std::is_same_v<T2, int>) {
            ret->setType<float>();
            ret->_data.emplace<xt::xarray<float>>(
                xt::where(filter, lhs.template get<float>(), rhs));
            return ret;
          } else if constexpr (std::is_same_v<
                                   T2,
                                   double>) {  // 标量不会传进来float型
            ret->setType<double>();
            ret->_data.emplace<xt::xarray<double>>(
                xt::where(filter, lhs.template get<float>(), rhs));
            return ret;
          }
        }
        case Ndarray::DOUBLE_TYPE: {
          ret->setType<double>();
          ret->_data.emplace<xt::xarray<double>>(
              xt::where(filter, lhs.template get<double>(), rhs));
          return ret;
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
            ret->setType<int>();
            ret->_data.emplace<xt::xarray<int>>(
                xt::where(filter, lhs, rhs.template get<int>()));
            return ret;
          } else if constexpr (std::is_same_v<
                                   T1,
                                   double>) {  // 标量不会传进来float型
            ret->setType<double>();
            ret->_data.emplace<xt::xarray<double>>(
                xt::where(filter, lhs, rhs.template get<int>()));
            return ret;
          }
        }
        case Ndarray::FLOAT_TYPE: {
          if constexpr (std::is_same_v<T1, int>) {
            ret->setType<float>();
            ret->_data.emplace<xt::xarray<float>>(
                xt::where(filter, lhs, rhs.template get<float>()));
            return ret;
          } else if constexpr (std::is_same_v<
                                   T1,
                                   double>) {  // 标量不会传进来float型
            ret->setType<double>();
            ret->_data.emplace<xt::xarray<double>>(
                xt::where(filter, lhs, rhs.template get<float>()));
            return ret;
          }
        }
        case Ndarray::DOUBLE_TYPE: {
          ret->setType<double>();
          ret->_data.emplace<xt::xarray<double>>(
              xt::where(filter, lhs, rhs.template get<double>()));
          return ret;
        }
        case Ndarray::ERROR: {
          THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                         "Not initialized");
        }
      }
    }

    if constexpr (std::is_same_v<T1, int> && std::is_same_v<T2, int>) {
      ret->setType<int>();
      ret->_data.emplace<xt::xarray<int>>(xt::where(filter, lhs, rhs));
      return ret;
    } else if constexpr ((std::is_same_v<T1, int> &&
                          std::is_same_v<T2, double>) ||
                         (std::is_same_v<T1, double> &&
                          std::is_same_v<T2, int>) ||
                         (std::is_same_v<T1, double> &&
                          std::is_same_v<T2, double>)) {
      ret->setType<double>();
      ret->_data.emplace<xt::xarray<double>>(xt::where(filter, lhs, rhs));
      return ret;
    }

    THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE,
                                   "They can't all be scalars");
  }
};
using Ndop2 = NdarrayOperatorDisableSIMD;
}  // namespace aql
}  // namespace arangodb
