#ifndef dplyr_hybrid_mean_h
#define dplyr_hybrid_mean_h

#include <dplyr/hybrid/HybridVectorScalarResult.h>
#include <dplyr/hybrid/Dispatch.h>

namespace dplyr {
namespace hybrid {

namespace internal {

template <int RTYPE, bool NA_RM, typename Data, template <int, bool, typename> class Impl >
class SimpleDispatchImpl : public HybridVectorScalarResult < REALSXP, Data, SimpleDispatchImpl<RTYPE, NA_RM, Data, Impl> > {
public :
  typedef typename Rcpp::Vector<RTYPE>::stored_type STORAGE;

  typedef HybridVectorScalarResult<REALSXP, Data, SimpleDispatchImpl > Parent ;
  typedef typename Data::slicing_index Index;

  SimpleDispatchImpl(const Data& data, Column vec) :
    Parent(data),
    data_ptr(Rcpp::internal::r_vector_start<RTYPE>(vec.data)),
    is_summary(vec.is_summary)
  {}

  double process(const Index& indices) const {
    return Impl<RTYPE, NA_RM, Index>::process(data_ptr, indices, is_summary);
  }

private:
  STORAGE* data_ptr;
  bool is_summary;
} ;

template <
  typename Data,
  template <int, bool, typename> class Impl
  >
class SimpleDispatch {
public:
  typedef typename Data::slicing_index Index;

  SimpleDispatch(const Data& data_, Column variable_, bool narm_):
    data(data_),
    variable(variable_),
    narm(narm_)
  {}

  SEXP summarise() const {
    return operate(Summary());
  }

  SEXP window() const {
    return operate(Window());
  }

private:
  const Data& data;
  Column variable;
  bool narm;

  template <typename Operation>
  SEXP operate(const Operation& op) const {
    // dispatch to the method below based on na.rm
    if (narm) {
      return operate_narm<Operation, true>(op);
    } else {
      return operate_narm<Operation, false>(op);
    }
  }

  template <typename Operation, bool NARM>
  SEXP operate_narm(const Operation& op) const {
    // try to dispatch to the right class
    switch (TYPEOF(variable.data)) {
    case INTSXP:
      return op(SimpleDispatchImpl<INTSXP, NARM, Data, Impl>(data, variable));
    case REALSXP:
      return op(SimpleDispatchImpl<REALSXP, NARM, Data, Impl>(data, variable));
    case LGLSXP:
      return op(SimpleDispatchImpl<LGLSXP, NARM, Data, Impl>(data, variable));
    }

    // give up, effectively let R evaluate the call
    return R_UnboundValue;
  }

};

// ------- mean

template <int RTYPE, bool NA_RM, typename Index>
struct MeanImpl {
  static double process(typename Rcpp::traits::storage_type<RTYPE>::type* ptr,  const Index& indices, bool is_summary) {
    typedef typename Rcpp::traits::storage_type<RTYPE>::type STORAGE;

    // already summarised, e.g. when summarise( x = ..., y = mean(x))
    // we need r coercion as opposed to a simple cast to double because of NA
    if (is_summary) {
      return Rcpp::internal::r_coerce<RTYPE, REALSXP>(ptr[indices.group()]);
    }

    long double res = 0.0;
    int n = indices.size();
    int m = n;
    for (int i = 0; i < n; i++) {
      STORAGE value = ptr[ indices[i] ];

      // REALSXP and !NA_RM: we don't test for NA here because += NA will give NA
      // this is faster in the most common case where there are no NA
      // if there are NA, we could return quicker as in the version for
      // INTSXP, but we would penalize the most common case
      //
      // INTSXP, LGLSXP: no shortcut, need to test
      if (NA_RM || RTYPE == INTSXP || RTYPE == LGLSXP) {
        if (Rcpp::traits::is_na<RTYPE>(value)) {
          if (!NA_RM) {
            return NA_REAL;
          }

          --m;
          continue;
        }
      }

      res += value;
    }
    if (m == 0) return R_NaN;
    res /= m;

    // Correcting accuracy of result, see base R implementation
    if (R_FINITE(res)) {
      long double t = 0.0;
      for (int i = 0; i < n; i++) {
        STORAGE value = ptr[indices[i]];
        if (!NA_RM || ! Rcpp::traits::is_na<RTYPE>(value)) {
          t += value - res;
        }
      }
      res += t / m;
    }

    return (double)res;
  }
};

// ------------- var

inline double square(double x) {
  return x * x;
}

template <int RTYPE, bool NA_RM, typename Index>
struct VarImpl {
  typedef typename Rcpp::Vector<RTYPE>::stored_type STORAGE;

  static double process(typename Rcpp::traits::storage_type<RTYPE>::type* data_ptr,  const Index& indices, bool is_summary) {
    // already summarised, e.g. when summarise( x = ..., y = var(x)), so x is of length 1 -> NA
    if (is_summary) {
      return NA_REAL;
    }

    int n = indices.size();
    if (n <= 1) return NA_REAL;
    double m = MeanImpl<RTYPE, NA_RM, Index>::process(data_ptr, indices, is_summary);

    if (!R_FINITE(m)) return m;

    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < n; i++) {
      STORAGE current = data_ptr[indices[i]];
      if (NA_RM && Rcpp::Vector<RTYPE>::is_na(current)) continue;
      sum += internal::square(current - m);
      count++;
    }
    if (count <= 1) return NA_REAL;
    return sum / (count - 1);
  }

};

template <int RTYPE, bool NA_RM, typename Index>
struct SdImpl {
  static double process(typename Rcpp::traits::storage_type<RTYPE>::type* data_ptr,  const Index& indices, bool is_summary) {
    return sqrt(VarImpl<RTYPE, NA_RM, Index>::process(data_ptr, indices, is_summary));
  }
};


} // namespace internal

template <typename Data>
internal::SimpleDispatch<Data, internal::MeanImpl> mean_(const Data& data, Column variable, bool narm) {
  return internal::SimpleDispatch<Data, internal::MeanImpl>(data, variable, narm);
}

template <typename Data>
internal::SimpleDispatch<Data, internal::VarImpl> var_(const Data& data, Column variable, bool narm) {
  return internal::SimpleDispatch<Data, internal::VarImpl>(data, variable, narm);
}

template <typename Data>
internal::SimpleDispatch<Data, internal::SdImpl> sd_(const Data& data, Column variable, bool narm) {
  return internal::SimpleDispatch<Data, internal::SdImpl>(data, variable, narm);
}

}
}


#endif