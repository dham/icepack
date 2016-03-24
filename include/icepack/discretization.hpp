
#ifndef ICEPACK_DISCRETIZATION_HPP
#define ICEPACK_DISCRETIZATION_HPP

#include <tuple>
#include <memory>

#include <deal.II/base/quadrature_lib.h>
#include <deal.II/grid/tria.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/lac/sparsity_pattern.h>
#include <deal.II/lac/dynamic_sparsity_pattern.h>

namespace icepack {

  using dealii::QGauss;
  using dealii::Triangulation;
  using dealii::FE_Q;
  using dealii::FESystem;
  using dealii::DoFHandler;
  using dealii::SparsityPattern;
  using dealii::DynamicSparsityPattern;
  using dealii::ConstraintMatrix;
  using dealii::SmartPointer;
  using dealii::DoFTools::make_hanging_node_constraints;
  using dealii::DoFTools::make_sparsity_pattern;

  /**
   * Default update flags for `dealii::FEValues` objects when iterating over
   * the degrees of freedom of a finite element field.
   */
  /*
  namespace DefaultUpdateFlags {
    using dealii::UpdateFlags;

    extern const UpdateFlags flags;
    extern const UpdateFlags face_flags;
  }
  */


  /**
   * This struct is just for getting the right finite element class for vector
   * or scalar discretizations as the case may be.
   */
  template <int rank, int dim> struct fe_field;

  template <int dim> struct fe_field<0, dim>
  {
    using FE = FE_Q<dim>;
    static FE fe(const size_t p) { return FE(p); }
  };

  template <int dim> struct fe_field<1, dim>
  {
    using FE = FESystem<dim>;
    static FE fe(const size_t p) { return FE(FE_Q<dim>(p), dim); }
  };


  /**
   * This class encapsulates all the data needed to discretize finite element
   * fields of a particular tensor rank, e.g. scalar, vector or tensor fields.
   */
  template <int rank, int dim>
  class FieldDiscretization
  {
  public:
    using FE = typename fe_field<rank, dim>::FE;

    FieldDiscretization(const Triangulation<dim>& tria, const unsigned int p)
      :
      fe(fe_field<rank, dim>::fe(p)),
      dof_handler(tria)
    {
      dof_handler.distribute_dofs(fe);

      constraints.clear();
      make_hanging_node_constraints(dof_handler, constraints);
      constraints.close();

      DynamicSparsityPattern dsp(dof_handler.n_dofs());
      make_sparsity_pattern(dof_handler, dsp, constraints, false);
      sparsity.copy_from(dsp);
    }

    FieldDiscretization(const FieldDiscretization<rank, dim>&) = delete;

    ~FieldDiscretization()
    {
      dof_handler.clear();
    }

    const FE& get_fe() const { return fe; }
    const DoFHandler<dim>& get_dof_handler() const { return dof_handler; }
    const SparsityPattern& get_sparsity() const { return sparsity; }
    const ConstraintMatrix& get_constraints() const { return constraints; }

  protected:
    FE fe;
    DoFHandler<dim> dof_handler;
    SparsityPattern sparsity;
    ConstraintMatrix constraints;
  };


  /**
   * This class encapsulates all the data needed to discretize finite element
   * fields of any tensor rank.
   *
   * In practice, we only need ranks 0 and 1. This class acts as an aggregate
   * of FieldDiscretization objects for each rank.
   */
  template <int dim>
  class Discretization : public dealii::Subscriptor
  {
  public:
    using Scalar = FieldDiscretization<0, dim>;
    using Vector = FieldDiscretization<1, dim>;

    Discretization(const Triangulation<dim>& tria, const unsigned int p)
      :
      p(p),
      tria(&tria)
    {
      std::get<0>(ranks) = std::make_unique<Scalar>(tria, p);
      std::get<1>(ranks) = std::make_unique<Vector>(tria, p);
    }

    Discretization(const Discretization<dim>&) = delete;

    template <int rank>
    const FieldDiscretization<rank, dim>&
    field_discretization(const fe_field<rank, dim>&) const
    {
      return *std::get<rank>(ranks);
    }

    const Scalar& scalar() const
    {
      return *std::get<0>(ranks);
    }

    const Vector& vector() const
    {
      return *std::get<1>(ranks);
    }

    const Triangulation<dim>& get_triangulation() const
    {
      return *tria;
    }

    QGauss<dim> quad() const
    {
      return QGauss<dim>(p + 1);
    }

    QGauss<dim-1> face_quad() const
    {
      return QGauss<dim - 1>(p + 1);
    }

    unsigned int degree() const
    {
      return p;
    }

  protected:
    const unsigned int p;
    SmartPointer<const Triangulation<dim>> tria;
    std::tuple<std::unique_ptr<Scalar>, std::unique_ptr<Vector>> ranks;
  };

}

#endif