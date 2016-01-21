
#ifndef ICEPACK_VISCOSITY_HPP
#define ICEPACK_VISCOSITY_HPP

#include <deal.II/base/symmetric_tensor.h>

namespace icepack {

  using dealii::SymmetricTensor;

  double rate_factor(const double temperature);
  double viscosity(const double temperature, const double strain_rate);

  struct ConstitutiveTensor
  {
    ConstitutiveTensor(const double n);

    SymmetricTensor<4, 2> nonlinear(
      const double temperature,
      const double thickness,
      const SymmetricTensor<2, 2> strain_rate
    ) const;

    SymmetricTensor<4, 2> linearized(
      const double temperature,
      const double thickness,
      const SymmetricTensor<2, 2> strain_rate
    ) const;

    const double n;
  };

}

#endif
