
#include <deal.II/base/quadrature_lib.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/solver_cg.h>
#include <deal.II/lac/precondition.h>
#include <deal.II/lac/sparse_ilu.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_tools.h>

#include <deal.II/grid/grid_refinement.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/grid/tria_boundary_lib.h>

#include <deal.II/numerics/vector_tools.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/error_estimator.h>


#include "shallow_shelf.hpp"
#include "ice_thickness.hpp"
#include "physical_constants.hpp"


namespace ShallowShelfApproximation
{
  using namespace dealii;

  constexpr double strain_rate = 0.2;  // 1 / year
  constexpr double nu_guess = 0.5 * pow(A0_cold *
                                        exp(-Q_cold / (idealgas * Temp)) *
                                        strain_rate * strain_rate, -1.0/3);


  ShallowShelf::ShallowShelf (Triangulation<2>&  _triangulation,
                              const Function<2>& _surface,
                              const Function<2>& _bed,
                              const TensorFunction<1, 2>& _boundary_velocity)
    :
    surface (_surface),
    bed (_bed),
    thickness (IceThickness(surface, bed)),
    boundary_velocity (_boundary_velocity),
    triangulation (_triangulation),
    dof_handler (triangulation),
    fe (FE_Q<2>(1), 2)
  {}


  ShallowShelf::~ShallowShelf ()
  {
    dof_handler.clear ();
  }


  void ShallowShelf::setup_system ()
  {
    dof_handler.distribute_dofs (fe);
    hanging_node_constraints.clear ();
    DoFTools::make_hanging_node_constraints (dof_handler,
                                             hanging_node_constraints);
    hanging_node_constraints.close ();
    sparsity_pattern.reinit (dof_handler.n_dofs(),
                             dof_handler.n_dofs(),
                             dof_handler.max_couplings_between_dofs());
    DoFTools::make_sparsity_pattern (dof_handler, sparsity_pattern);

    hanging_node_constraints.condense (sparsity_pattern);

    sparsity_pattern.compress();

    system_matrix.reinit (sparsity_pattern);

    solution.reinit (dof_handler.n_dofs());
    system_rhs.reinit (dof_handler.n_dofs());
  }


  void ShallowShelf::assemble_system ()
  {
    QGauss<2> quadrature_formula(2);
    QGauss<1> face_quadrature_formula(2);

    FEValues<2> fe_values (fe, quadrature_formula,
                           update_values            | update_gradients |
                           update_quadrature_points | update_JxW_values);

    FEFaceValues<2> fe_face_values (fe, face_quadrature_formula,
                                    update_values | update_quadrature_points |
                                    update_normal_vectors | update_JxW_values);

    const unsigned int   dofs_per_cell = fe.dofs_per_cell;
    const unsigned int   n_q_points    = quadrature_formula.size();
    const unsigned int   n_face_q_points = face_quadrature_formula.size();

    FullMatrix<double>   cell_matrix (dofs_per_cell, dofs_per_cell);
    Vector<double>       cell_rhs (dofs_per_cell);

    std::vector<types::global_dof_index> local_dof_indices (dofs_per_cell);

    // Assuming constant viscosity for now, ignoring nonlinearity.
    ConstantFunction<2> nu(nu_guess);

    std::vector<double> nu_values (n_q_points);
    std::vector<double> thickness_values (n_q_points);
    std::vector< Tensor<1, 2> > surface_gradient_values (n_q_points,
                                                         Tensor<1, 2>());

    // Loop over every cell in the triangulation
    for (auto cell: dof_handler.active_cell_iterators())
      {
        cell_matrix = 0;
        cell_rhs    = 0;

        fe_values.reinit (cell);

        // Getting values of coefficients / RHS at the quadrature points
        nu.value_list         (fe_values.get_quadrature_points(),
                               nu_values);
        thickness.value_list  (fe_values.get_quadrature_points(),
                               thickness_values);
        surface.gradient_list (fe_values.get_quadrature_points(),
                               surface_gradient_values);

        // Build the cell stiffness matrix
        for (unsigned int i = 0; i < dofs_per_cell; ++i)
          {
            const unsigned int
              component_i = fe.system_to_component_index(i).first;

            for (unsigned int j = 0; j < dofs_per_cell; ++j)
              {
                const unsigned int
                  component_j = fe.system_to_component_index(j).first;

                // Loop over all the quadrature points in the cell
                for (unsigned int q_point = 0; q_point < n_q_points; ++q_point)
                  {
                    cell_matrix(i,j)
                      +=
                      // First term: 2 * (nu * d_i u_i, d_j v_j)
                      //               + (nu * d_i u_j, d_j v_i).
                      (
                       2 *
                       (fe_values.shape_grad(i,q_point)[component_i] *
                        fe_values.shape_grad(j,q_point)[component_j])
                       +
                       (fe_values.shape_grad(i,q_point)[component_j] *
                        fe_values.shape_grad(j,q_point)[component_i])
                       +
                       // Second term: (nu * nabla u_i, nabla v_j)
                       ((component_i == component_j) ?
                        (fe_values.shape_grad(i,q_point) *
                         fe_values.shape_grad(j,q_point))
                        : 0)
                       )
                      *
                      nu_values[q_point] *
                      thickness_values[q_point] *
                      fe_values.JxW(q_point);
                  }
              }
          }

        // Build the cell right-hand side
        // First, add up contributions from the ice driving stress...
        for (unsigned int i = 0; i < dofs_per_cell; ++i)
          {
            const unsigned int
              component_i = fe.system_to_component_index(i).first;

            for (unsigned int q_point = 0; q_point < n_q_points; ++q_point)
              cell_rhs(i) -= rho_ice * gravity *
                             fe_values.shape_value(i, q_point) *
                             thickness_values[q_point] *
                             surface_gradient_values[q_point][component_i] *
                             fe_values.JxW(q_point);
          }

        // ... then add up contributions from the boundary condition at the
        // ice calving front.
        for (unsigned int face_number = 0;
             face_number < GeometryInfo<2>::faces_per_cell;
             ++face_number)
          if (cell->face(face_number)->at_boundary()
              and
              cell->face(face_number)->boundary_indicator() == 1)
            {
              fe_face_values.reinit (cell, face_number);
              for (unsigned int q_point = 0; q_point < n_face_q_points; ++q_point)
                {
                  const Point<2> x = fe_face_values.quadrature_point(q_point);
                  // Depth `b` of the ice base; note that this could be either
                  // equal to or greater than the bed elevation depending on if
                  // the ice is grounded or not.
                  const double h = thickness.value(x);
                  const double b = surface.value(x) - thickness.value(x);
                  const Tensor<1, 2> neumann_value
                    = 0.5 * gravity * (rho_ice * h * h - rho_water * b * b) *
                      fe_face_values.normal_vector(q_point);
                  for (unsigned int i = 0; i < dofs_per_cell; ++i)
                    {
                      const unsigned int
                        component_i = fe.system_to_component_index(i).first;
                      cell_rhs(i) += neumann_value[component_i] *
                                     fe_face_values.shape_value(i, q_point) *
                                     fe_face_values.JxW(q_point);
                    }
                }
            }


        // Add cell RHS/stiffness matrix to their global counterparts
        cell->get_dof_indices (local_dof_indices);
        for (unsigned int i = 0; i < dofs_per_cell; ++i)
          {
            for (unsigned int j = 0; j < dofs_per_cell; ++j)
              system_matrix.add (local_dof_indices[i],
                                 local_dof_indices[j],
                                 cell_matrix(i,j));

            system_rhs(local_dof_indices[i]) += cell_rhs(i);
          }

      } // End of loop over `cell`


    hanging_node_constraints.condense (system_matrix);
    hanging_node_constraints.condense (system_rhs);

    std::map<types::global_dof_index,double> boundary_values;
    VectorTools::interpolate_boundary_values
      (dof_handler,
       0,
       VectorFunctionFromTensorFunction<2> (boundary_velocity),
       boundary_values);

    MatrixTools::apply_boundary_values (boundary_values,
                                        system_matrix,
                                        solution,
                                        system_rhs);

  } // End of AssembleSystem


  void ShallowShelf::solve ()
  {
    SolverControl solver_control (1000, 1.0e-12);
    SolverCG<>    cg (solver_control);

    SparseILU<double> preconditioner;
    preconditioner.initialize(system_matrix);

    cg.solve (system_matrix, solution, system_rhs,
              preconditioner);

    hanging_node_constraints.distribute (solution);
  }


  void ShallowShelf::refine_grid ()
  {
    Vector<float> estimated_error_per_cell (triangulation.n_active_cells());

    KellyErrorEstimator<2>::estimate (dof_handler,
                                      QGauss<1>(2),
                                      typename FunctionMap<2>::type(),
                                      solution,
                                      estimated_error_per_cell);

    GridRefinement::refine_and_coarsen_fixed_number (triangulation,
                                                     estimated_error_per_cell,
                                                     0.3, 0.03);

    triangulation.execute_coarsening_and_refinement ();
  }


  void ShallowShelf::output_results (const unsigned int cycle) const
  {
    std::string filename = "solution-";
    filename += ('0' + cycle);
    Assert (cycle < 10, ExcInternalError());

    filename += ".vtk";
    std::ofstream output (filename.c_str());

    DataOut<2> data_out;
    data_out.attach_dof_handler (dof_handler);

    std::vector<std::string> solution_names;
    solution_names.push_back ("x_velocity");
    solution_names.push_back ("y_velocity");

    data_out.add_data_vector (solution, solution_names);
    data_out.build_patches ();
    data_out.write_vtk (output);
  }


  void ShallowShelf::run ()
  {
    for (unsigned int cycle = 0; cycle < 3; ++cycle)
      {
        std::cout << "Cycle " << cycle << ':' << std::endl;

        if (cycle == 0)
          {
            triangulation.refine_global (2);
          }
        else
          refine_grid ();

        std::cout << "   Number of active cells:       "
                  << triangulation.n_active_cells()
                  << std::endl;

        setup_system ();

        std::cout << "   Number of degrees of freedom: "
                  << dof_handler.n_dofs()
                  << std::endl;

        assemble_system ();
        solve ();
        output_results (cycle);
      }
  }


} // End of ShallowShelfApproximation namespace
