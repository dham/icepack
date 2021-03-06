# Copyright (C) 2017-2019 by Daniel Shapero <shapero@uw.edu>
#
# This file is part of icepack.
#
# icepack is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# The full text of the license can be found in the file LICENSE in the
# icepack source directory or at <http://www.gnu.org/licenses/>.

import numpy as np
import firedrake
from firedrake import interpolate, as_vector
import icepack, icepack.models
from icepack.constants import gravity, rho_ice, rho_water, glen_flow_law as n

# The domain is a 20km x 20km square, with ice flowing in from the left.
Lx, Ly = 20.0e3, 20.0e3

# The inflow velocity is 100 m/year; the ice shelf decreases from 500m thick
# to 100m; and the temperature is a constant -19C.
u0 = 100.0
h0, dh = 500.0, 100.0
T = 254.15


# This is an exact solution for the velocity of a floating ice shelf with
# constant temperature and linearly decreasing thickness. See Greve and
# Blatter for the derivation.
def exact_u(x):
    rho = rho_ice * (1 - rho_ice / rho_water)
    Z = icepack.rate_factor(T) * (rho * gravity * h0 / 4)**n
    q = 1 - (1 - (dh/h0) * (x/Lx))**(n + 1)
    du = Z * q * Lx * (h0/dh) / (n + 1)
    return u0 + du


# We'll use the same perturbation to `u` throughout these tests.
def perturb_u(x, y):
    px, py = x/Lx, y/Ly
    q = 16 * px * (1 - px) * py * (1 - py)
    return 60 * q * (px - 0.5)


def norm(v):
    return icepack.norm(v, norm_type='H1')


# Check that the diagnostic solver converges with the expected rate as the
# mesh is refined using an exact solution of the ice shelf model.
def test_diagnostic_solver_convergence():
    # Create an ice shelf model
    ice_shelf = icepack.models.IceShelf()
    opts = {'dirichlet_ids': [1], 'side_wall_ids': [3, 4], 'tol': 1e-12}

    # Solve the ice shelf model for successively higher mesh resolution
    for degree in range(1, 4):
        delta_x, error = [], []
        for N in range(16, 97 - 32 * (degree - 1), 4):
            mesh = firedrake.RectangleMesh(N, N, Lx, Ly)
            x, y = firedrake.SpatialCoordinate(mesh)

            V = firedrake.VectorFunctionSpace(mesh, 'CG', degree)
            Q = firedrake.FunctionSpace(mesh, 'CG', degree)

            u_exact = interpolate(as_vector((exact_u(x), 0)), V)
            u_guess = interpolate(u_exact + as_vector((perturb_u(x, y), 0)), V)

            h = interpolate(h0 - dh * x / Lx, Q)
            A = interpolate(firedrake.Constant(icepack.rate_factor(T)), Q)

            u = ice_shelf.diagnostic_solve(h=h, A=A, u0=u_guess, **opts)
            error.append(norm(u_exact - u) / norm(u_exact))
            delta_x.append(Lx / N)

            print(delta_x[-1], error[-1])

        # Fit the error curve and check that the convergence rate is what we
        # expect
        log_delta_x = np.log2(np.array(delta_x))
        log_error = np.log2(np.array(error))
        slope, intercept = np.polyfit(log_delta_x, log_error, 1)

        print("log(error) ~= {:g} * log(dx) + {:g}".format(slope, intercept))
        assert slope > degree + 0.8


# Check that the diagnostic solver converges with the expected rate as the
# mesh is refined when we use an alternative parameterization of the model.
def test_diagnostic_solver_parameterization():
    # Define a new viscosity functional, parameterized in terms of the
    # rheology `B` instead of the fluidity `A`
    from firedrake import inner, grad, sym, dx, tr as trace, Identity, sqrt

    def M(eps, B):
        I = Identity(2)
        tr = trace(eps)
        eps_e = sqrt((inner(eps, eps) + tr**2) / 2)
        mu = 0.5 * B * eps_e**(1/n - 1)
        return 2 * mu * (eps + tr * I)

    def eps(u):
        return sym(grad(u))

    def viscosity(u, h, B):
        return n/(n + 1) * h * inner(M(eps(u), B), eps(u)) * dx

    # Make a model object with our new viscosity functional
    ice_shelf = icepack.models.IceShelf(viscosity=viscosity)
    opts = {'dirichlet_ids': [1, 3, 4], 'tol': 1e-12}

    # Same as before
    delta_x, error = [], []
    for N in range(16, 65, 4):
        mesh = firedrake.RectangleMesh(N, N, Lx, Ly)
        x, y = firedrake.SpatialCoordinate(mesh)

        degree = 2
        V = firedrake.VectorFunctionSpace(mesh, 'CG', degree)
        Q = firedrake.FunctionSpace(mesh, 'CG', degree)

        u_exact = interpolate(as_vector((exact_u(x), 0)), V)
        u_guess = interpolate(as_vector((exact_u(x) + perturb_u(x, y), 0)), V)
        h = interpolate(h0 - dh * x / Lx, Q)
        B = interpolate(firedrake.Constant(icepack.rate_factor(T)**(-1/n)), Q)

        u = ice_shelf.diagnostic_solve(h=h, B=B, u0=u_guess, **opts)
        error.append(norm(u_exact - u) / norm(u_exact))
        delta_x.append(Lx / N)

        print(delta_x[-1], error[-1])

    log_delta_x = np.log2(np.array(delta_x))
    log_error = np.log2(np.array(error))
    slope, intercept = np.polyfit(log_delta_x, log_error, 1)

    print("log(error) ~= {:g} * log(dx) + {:g}".format(slope, intercept))
    assert slope > degree - 0.05


def perturb_v(x, y):
    px, py = x/Lx, y/Ly
    return 20 * px * (py - 0.5)


# Check that the diagnostic solver gives a sensible result when we add friction
# at the side walls. There is probably no analytical solution for this so all
# we have is a sanity test.
def test_diagnostic_solver_side_friction():
    ice_shelf = icepack.models.IceShelf()
    opts = {'dirichlet_ids': [1], 'side_wall_ids': [3, 4], 'tol': 1e-12}

    mesh = firedrake.RectangleMesh(32, 32, Lx, Ly)
    degree = 2
    V = firedrake.VectorFunctionSpace(mesh, 'CG', degree)
    Q = firedrake.FunctionSpace(mesh, 'CG', degree)

    x, y = firedrake.SpatialCoordinate(mesh)
    u_initial = interpolate(as_vector((exact_u(x), 0)), V)
    h = interpolate(h0 - dh * x / Lx, Q)
    A = interpolate(firedrake.Constant(icepack.rate_factor(T)), Q)

    # Choose the side wall friction coefficient so that, assuming the ice is
    # sliding at the maximum speed for the solution without friction, the
    # stress is 10 kPa.
    from icepack.constants import weertman_sliding_law as m
    τ = 0.01
    u_max = icepack.norm(u_initial, norm_type='Linfty')
    Cs = firedrake.Constant(τ * u_max**(-1/m))
    u = ice_shelf.diagnostic_solve(u0=u_initial, h=h, A=A, Cs=Cs, **opts)

    assert icepack.norm(u) < icepack.norm(u_initial)

