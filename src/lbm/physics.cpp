#include <lbm/physics.hpp>

#include <cassert>
#include <cstdlib>

#include <omp.h>

#include <lbm/communications.hpp>
#include <lbm/config.hpp>
#include <lbm/structures.hpp>

#if DIRECTIONS == 9 && DIMENSIONS == 2
/// Definition of the 9 base vectors used to discretize the directions on each mesh.
const Vector direction_matrix[DIRECTIONS] = {
  // clang-format off
  {+0.0, +0.0},
  {+1.0, +0.0}, {+0.0, +1.0}, {-1.0, +0.0}, {+0.0, -1.0},
  {+1.0, +1.0}, {-1.0, +1.0}, {-1.0, -1.0}, {+1.0, -1.0},
  // clang-format on
};
#else
#error Need to define adapted direction matrix.
#endif

#if DIRECTIONS == 9
/// Weigths used to compensate the differences in lenght of the 9 directional vectors.
const double equil_weight[DIRECTIONS] = {
  // clang-format off
  4.0 / 9.0,
  1.0 / 9.0, 1.0 / 9.0, 1.0 / 9.0, 1.0 / 9.0,
  1.0 / 36.0, 1.0 / 36.0, 1.0 / 36.0, 1.0 / 36.0,
  // clang-format on
};

/// Opposite directions for bounce back implementation
const int opposite_of[DIRECTIONS] = {0, 3, 4, 1, 2, 7, 8, 5, 6};
#else
#error Need to define adapted equilibrium distribution function
#endif

static inline double compute_equilibrium_profile_components(
  const double vx,
  const double vy,
  const double density,
  const double velocity_norm_2,
  const int direction
) {
  static constexpr double direction_x[DIRECTIONS] = {0.0, 1.0, 0.0, -1.0, 0.0, 1.0, -1.0, -1.0, 1.0};
  static constexpr double direction_y[DIRECTIONS] = {0.0, 0.0, 1.0, 0.0, -1.0, 1.0, 1.0, -1.0, -1.0};

  const double p      = direction_x[direction] * vx + direction_y[direction] * vy;
  const double common = 1.0 - 1.5 * velocity_norm_2;
  return equil_weight[direction] * density * (common + 3.0 * p + 4.5 * p * p);
}

double get_vect_norm_2(Vector const a, Vector const b) {
  double res = 0.0;
  for (size_t k = 0; k < DIMENSIONS; k++) {
    res += a[k] * b[k];
  }
  return res;
}

double get_cell_density(const lbm_mesh_cell_t cell) {
  assert(cell != NULL);
  double res = 0.0;
  for (size_t k = 0; k < DIRECTIONS; k++) {
    res += cell[k];
  }
  return res;
}

void get_cell_velocity(Vector v, const lbm_mesh_cell_t cell, double cell_density) {
  assert(v != NULL);
  assert(cell != NULL);

  // Loop on all dimensions
  for (size_t d = 0; d < DIMENSIONS; d++) {
    v[d] = 0.0;

    // Sum all directions
    for (size_t k = 0; k < DIRECTIONS; k++) {
      v[d] += cell[k] * direction_matrix[k][d];
    }

    // Normalize
    v[d] /= cell_density;
  }
}

double compute_equilibrium_profile(Vector velocity, double density, int direction) {
  return compute_equilibrium_profile_components(
    velocity[0],
    velocity[1],
    density,
    velocity[0] * velocity[0] + velocity[1] * velocity[1],
    direction
  );
}

void compute_cell_collision(lbm_mesh_cell_t cell_out, const lbm_mesh_cell_t cell_in) {
  const double f0 = cell_in[0];
  const double f1 = cell_in[1];
  const double f2 = cell_in[2];
  const double f3 = cell_in[3];
  const double f4 = cell_in[4];
  const double f5 = cell_in[5];
  const double f6 = cell_in[6];
  const double f7 = cell_in[7];
  const double f8 = cell_in[8];

  const double density     = f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7 + f8;
  const double inv_density = 1.0 / density;
  const double vx          = (f1 - f3 + f5 - f6 - f7 + f8) * inv_density;
  const double vy          = (f2 - f4 + f5 + f6 - f7 - f8) * inv_density;
  const double vx2         = vx * vx;
  const double vy2         = vy * vy;
  const double velocity_norm_2 = vx2 + vy2;
  const double sum         = vx + vy;
  const double diff        = vx - vy;
  const double sum2        = sum * sum;
  const double diff2       = diff * diff;

  const double omega       = RELAX_PARAMETER;
  const double one_minus_omega = 1.0 - omega;
  const double common      = 1.0 - 1.5 * velocity_norm_2;
  const double rho0        = (4.0 / 9.0) * density;
  const double rho_axis    = (1.0 / 9.0) * density;
  const double rho_diag    = (1.0 / 36.0) * density;

  const double feq0 = rho0 * common;
  const double feq1 = rho_axis * (common + 3.0 * vx + 4.5 * vx2);
  const double feq2 = rho_axis * (common + 3.0 * vy + 4.5 * vy2);
  const double feq3 = rho_axis * (common - 3.0 * vx + 4.5 * vx2);
  const double feq4 = rho_axis * (common - 3.0 * vy + 4.5 * vy2);
  const double feq5 = rho_diag * (common + 3.0 * sum + 4.5 * sum2);
  const double feq6 = rho_diag * (common + 3.0 * (vy - vx) + 4.5 * diff2);
  const double feq7 = rho_diag * (common - 3.0 * sum + 4.5 * sum2);
  const double feq8 = rho_diag * (common + 3.0 * diff + 4.5 * diff2);

  cell_out[0] = one_minus_omega * f0 + omega * feq0;
  cell_out[1] = one_minus_omega * f1 + omega * feq1;
  cell_out[2] = one_minus_omega * f2 + omega * feq2;
  cell_out[3] = one_minus_omega * f3 + omega * feq3;
  cell_out[4] = one_minus_omega * f4 + omega * feq4;
  cell_out[5] = one_minus_omega * f5 + omega * feq5;
  cell_out[6] = one_minus_omega * f6 + omega * feq6;
  cell_out[7] = one_minus_omega * f7 + omega * feq7;
  cell_out[8] = one_minus_omega * f8 + omega * feq8;
}

void compute_bounce_back(lbm_mesh_cell_t cell) {
  double tmp[DIRECTIONS];
  for (size_t k = 0; k < DIRECTIONS; k++) {
    tmp[k] = cell[opposite_of[k]];
  }
  for (size_t k = 0; k < DIRECTIONS; k++) {
    cell[k] = tmp[k];
  }
}

double helper_compute_poiseuille(const size_t i, const size_t size) {
  const double y = (double)(i - 1);
  const double L = (double)(size - 1);
  return 4.0 * INFLOW_MAX_VELOCITY / (L * L) * (L * y - y * y);
}

void compute_inflow_zou_he_poiseuille_distr(const Mesh* mesh, lbm_mesh_cell_t cell, size_t id_y) {
#if DIRECTIONS != 9
#error Implemented only for 9 directions
#endif

  // Set macroscopic fluid info
  // Poiseuille distribution on X and null on Y
  // We just want the norm, so `v = v_x`
  const double v = helper_compute_poiseuille(id_y, mesh->height);

  // Compute rho from U and inner flow on surface
  const double rho = (cell[0] + cell[2] + cell[4] + 2 * (cell[3] + cell[6] + cell[7])) / (1.0 - v);

  // Now compute unknown microscopic values
  cell[1] = cell[3]; // + (2.0/3.0) * density * v_y <--- no velocity on Y so v_y = 0
  cell[5] = cell[7] - (1.0 / 2.0) * (cell[2] - cell[4])
            + (1.0 / 6.0) * (rho * v); // + (1.0/2.0) * rho * v_y    <--- no velocity on Y so v_y = 0
  cell[8] = cell[6] + (1.0 / 2.0) * (cell[2] - cell[4])
            + (1.0 / 6.0) * (rho * v); //- (1.0/2.0) * rho * v_y    <--- no velocity on Y so v_y = 0

  // No need to copy already known one as the value will be "loss" in the wall at propagatation time
}

void compute_outflow_zou_he_const_density(lbm_mesh_cell_t cell) {
#if DIRECTIONS != 9
#error Implemented only for 9 directions
#endif

  double const rho = 1.0;
  // Compute macroscopic velocity depending on inner flow going onto the wall
  const double v = -1.0 + (1.0 / rho) * (cell[0] + cell[2] + cell[4] + 2 * (cell[1] + cell[5] + cell[8]));

  // Now can compute unknown microscopic values
  cell[3] = cell[1] - (2.0 / 3.0) * rho * v;
  cell[7] = cell[5]
            + (1.0 / 2.0) * (cell[2] - cell[4])
            // - (1.0/2.0) * (rho * v_y)    <--- no velocity on Y so v_y = 0
            - (1.0 / 6.0) * (rho * v);
  cell[6] = cell[8]
            + (1.0 / 2.0) * (cell[4] - cell[2])
            // + (1.0/2.0) * (rho * v_y)    <--- no velocity on Y so v_y = 0
            - (1.0 / 6.0) * (rho * v);
}

void special_cells(Mesh* mesh, lbm_mesh_type_t* mesh_type, const lbm_comm_t* mesh_comm) {
  // Loop on all inner cells
  for (size_t i = 1; i < mesh->width - 1; i++) {
    for (size_t j = 1; j < mesh->height - 1; j++) {
      switch (*(lbm_cell_type_t_get_cell(mesh_type, i, j))) {
      case CELL_FUILD:
        break;
      case CELL_BOUNCE_BACK:
        compute_bounce_back(Mesh_get_cell(mesh, i, j));
        break;
      case CELL_LEFT_IN:
        compute_inflow_zou_he_poiseuille_distr(mesh, Mesh_get_cell(mesh, i, j), j + mesh_comm->y);
        break;
      case CELL_RIGHT_OUT:
        compute_outflow_zou_he_const_density(Mesh_get_cell(mesh, i, j));
        break;
      }
    }
  }
}

void collision(Mesh* mesh_out, const Mesh* mesh_in) {
  assert(mesh_in->width == mesh_out->width);
  assert(mesh_in->height == mesh_out->height);

  // Loop on all inner cells
  for (size_t j = 1; j < mesh_in->height - 1; j++) {
    for (size_t i = 1; i < mesh_in->width - 1; i++) {
      compute_cell_collision(Mesh_get_cell(mesh_out, i, j), Mesh_get_cell(mesh_in, i, j));
    }
  }
}

void propagation(Mesh* mesh_out, const Mesh* mesh_in) {
  const size_t width       = mesh_out->width;
  const size_t height      = mesh_out->height;
  const size_t cell_stride = DIRECTIONS;
  const size_t col_stride  = height * cell_stride;

  const double* __restrict in = mesh_in->cells;
  double* __restrict out      = mesh_out->cells;

  // Interior cells dominate runtime, so propagate them with a branchless pull-style kernel.
  for (size_t i = 1; i + 1 < width; i++) {
    const size_t dst_col   = i * col_stride;
    const size_t west_col  = (i - 1) * col_stride;
    const size_t east_col  = (i + 1) * col_stride;

    for (size_t j = 1; j + 1 < height; j++) {
      const size_t dst  = dst_col + j * cell_stride;
      const size_t west = west_col + j * cell_stride;
      const size_t east = east_col + j * cell_stride;

      out[dst + 0] = in[dst + 0];
      out[dst + 1] = in[west + 1];
      out[dst + 2] = in[dst_col + (j - 1) * cell_stride + 2];
      out[dst + 3] = in[east + 3];
      out[dst + 4] = in[dst_col + (j + 1) * cell_stride + 4];
      out[dst + 5] = in[west_col + (j - 1) * cell_stride + 5];
      out[dst + 6] = in[east_col + (j - 1) * cell_stride + 6];
      out[dst + 7] = in[east_col + (j + 1) * cell_stride + 7];
      out[dst + 8] = in[west_col + (j + 1) * cell_stride + 8];
    }
  }

  // Borders are a small fraction of the mesh; keep the generic safe handling here.
  for (size_t i = 0; i < width; i++) {
    for (size_t j = 0; j < height; j++) {
      const bool is_border = (i == 0 || j == 0 || i + 1 == width || j + 1 == height);
      if (!is_border) {
        continue;
      }
      for (size_t k = 0; k < DIRECTIONS; k++) {
        const ssize_t ii = (i + direction_matrix[k][0]);
        const ssize_t jj = (j + direction_matrix[k][1]);
        if ((ii >= 0 && ii < static_cast<ssize_t>(width)) && (jj >= 0 && jj < static_cast<ssize_t>(height))) {
          Mesh_get_cell(mesh_out, ii, jj)[k] = Mesh_get_cell(mesh_in, i, j)[k];
        }
      }
    }
  }
}
