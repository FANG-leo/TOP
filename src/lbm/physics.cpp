#include <lbm/physics.hpp>

#include <cassert>
#include <cstring>
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

#if defined(TOP_LBM_USE_OMP_TARGET)
static inline bool has_omp_target_device() {
  return omp_get_num_devices() > 0;
}
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

static inline void collide_cell_d2q9(
  double* __restrict cell_out,
  const double* __restrict cell_in,
  const double omega,
  const double one_minus_omega
) {
  const double f0 = cell_in[0];
  const double f1 = cell_in[1];
  const double f2 = cell_in[2];
  const double f3 = cell_in[3];
  const double f4 = cell_in[4];
  const double f5 = cell_in[5];
  const double f6 = cell_in[6];
  const double f7 = cell_in[7];
  const double f8 = cell_in[8];

  const double density          = f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7 + f8;
  const double inv_density      = 1.0 / density;
  const double vx               = (f1 - f3 + f5 - f6 - f7 + f8) * inv_density;
  const double vy               = (f2 - f4 + f5 + f6 - f7 - f8) * inv_density;
  const double vx2              = vx * vx;
  const double vy2              = vy * vy;
  const double velocity_norm_2  = vx2 + vy2;
  const double sum              = vx + vy;
  const double diff             = vx - vy;
  const double sum2             = sum * sum;
  const double diff2            = diff * diff;
  const double common           = 1.0 - 1.5 * velocity_norm_2;
  const double rho0             = (4.0 / 9.0) * density;
  const double rho_axis         = (1.0 / 9.0) * density;
  const double rho_diag         = (1.0 / 36.0) * density;

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

double get_vect_norm_2(Vector const a, Vector const b) {
  double res = 0.0;
  for (size_t k = 0; k < DIMENSIONS; k++) {
    res += a[k] * b[k];
  }
  return res;
}

double get_cell_density(lbm_mesh_const_cell_t __restrict cell) {
  assert(cell != NULL);
  double res = 0.0;
  for (size_t k = 0; k < DIRECTIONS; k++) {
    res += cell[k];
  }
  return res;
}

void get_cell_velocity(Vector v, lbm_mesh_const_cell_t __restrict cell, double cell_density) {
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

void compute_cell_collision(lbm_mesh_cell_t __restrict cell_out, lbm_mesh_const_cell_t __restrict cell_in) {
  collide_cell_d2q9(cell_out, cell_in, RELAX_PARAMETER, 1.0 - RELAX_PARAMETER);
}

void compute_bounce_back(lbm_mesh_cell_t __restrict cell) {
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

void compute_inflow_zou_he_poiseuille_distr(const Mesh* mesh, lbm_mesh_cell_t __restrict cell, size_t id_y) {
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

void compute_outflow_zou_he_const_density(lbm_mesh_cell_t __restrict cell) {
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

void special_cells(Mesh* __restrict mesh, lbm_mesh_type_t* __restrict mesh_type, const lbm_comm_t* __restrict mesh_comm) {
#if defined(TOP_LBM_USE_OMP_TARGET)
  if (has_omp_target_device() && Mesh_has_device_data(mesh) && lbm_mesh_type_has_device_data(mesh_type)) {
    const size_t height     = mesh->height;
    const size_t plane_size = Mesh_plane_size(mesh);
    double* __restrict cells = mesh->device_cells;
    const lbm_cell_type_t* __restrict types = mesh_type->device_types;
    const size_t global_y_offset = mesh_comm->y;

    #pragma omp target teams distribute parallel for collapse(2) is_device_ptr(cells, types)
    for (size_t i = 1; i < mesh->width - 1; i++) {
      for (size_t j = 1; j < mesh->height - 1; j++) {
        const size_t idx = i * height + j;
        switch (types[idx]) {
        case CELL_FUILD:
          break;
        case CELL_BOUNCE_BACK: {
          const double f1 = cells[1 * plane_size + idx];
          const double f2 = cells[2 * plane_size + idx];
          const double f3 = cells[3 * plane_size + idx];
          const double f4 = cells[4 * plane_size + idx];
          const double f5 = cells[5 * plane_size + idx];
          const double f6 = cells[6 * plane_size + idx];
          const double f7 = cells[7 * plane_size + idx];
          const double f8 = cells[8 * plane_size + idx];
          cells[1 * plane_size + idx] = f3;
          cells[2 * plane_size + idx] = f4;
          cells[3 * plane_size + idx] = f1;
          cells[4 * plane_size + idx] = f2;
          cells[5 * plane_size + idx] = f7;
          cells[6 * plane_size + idx] = f8;
          cells[7 * plane_size + idx] = f5;
          cells[8 * plane_size + idx] = f6;
          break;
        }
        case CELL_LEFT_IN: {
          const double v = helper_compute_poiseuille(j + global_y_offset, mesh->height);
          const double c0 = cells[0 * plane_size + idx];
          const double c2 = cells[2 * plane_size + idx];
          const double c3 = cells[3 * plane_size + idx];
          const double c4 = cells[4 * plane_size + idx];
          const double c6 = cells[6 * plane_size + idx];
          const double c7 = cells[7 * plane_size + idx];
          const double rho = (c0 + c2 + c4 + 2.0 * (c3 + c6 + c7)) / (1.0 - v);
          cells[1 * plane_size + idx] = c3;
          cells[5 * plane_size + idx] = c7 - 0.5 * (c2 - c4) + (rho * v) / 6.0;
          cells[8 * plane_size + idx] = c6 + 0.5 * (c2 - c4) + (rho * v) / 6.0;
          break;
        }
        case CELL_RIGHT_OUT: {
          const double rho = 1.0;
          const double c0 = cells[0 * plane_size + idx];
          const double c1 = cells[1 * plane_size + idx];
          const double c2 = cells[2 * plane_size + idx];
          const double c4 = cells[4 * plane_size + idx];
          const double c5 = cells[5 * plane_size + idx];
          const double c8 = cells[8 * plane_size + idx];
          const double v = -1.0 + (c0 + c2 + c4 + 2.0 * (c1 + c5 + c8)) / rho;
          cells[3 * plane_size + idx] = c1 - (2.0 / 3.0) * rho * v;
          cells[7 * plane_size + idx] = c5 + 0.5 * (c2 - c4) - (rho * v) / 6.0;
          cells[6 * plane_size + idx] = c8 + 0.5 * (c4 - c2) - (rho * v) / 6.0;
          break;
        }
        }
      }
    }
    return;
  }
#endif

  double cell[DIRECTIONS];
  // Loop on all inner cells
  for (size_t i = 1; i < mesh->width - 1; i++) {
    for (size_t j = 1; j < mesh->height - 1; j++) {
      switch (*(lbm_cell_type_t_get_cell(mesh_type, i, j))) {
      case CELL_FUILD:
        break;
      case CELL_BOUNCE_BACK:
        Mesh_load_cell(mesh, i, j, cell);
        compute_bounce_back(cell);
        Mesh_store_cell(mesh, i, j, cell);
        break;
      case CELL_LEFT_IN:
        Mesh_load_cell(mesh, i, j, cell);
        compute_inflow_zou_he_poiseuille_distr(mesh, cell, j + mesh_comm->y);
        Mesh_store_cell(mesh, i, j, cell);
        break;
      case CELL_RIGHT_OUT:
        Mesh_load_cell(mesh, i, j, cell);
        compute_outflow_zou_he_const_density(cell);
        Mesh_store_cell(mesh, i, j, cell);
        break;
      }
    }
  }
}

void collision(Mesh* __restrict mesh_out, const Mesh* __restrict mesh_in) {
  assert(mesh_in->width == mesh_out->width);
  assert(mesh_in->height == mesh_out->height);

  const size_t width            = mesh_in->width;
  const size_t height           = mesh_in->height;
  const size_t plane_size       = Mesh_plane_size(mesh_in);
  const size_t total_scalars    = plane_size * DIRECTIONS;
  const double omega            = RELAX_PARAMETER;
  const double one_minus_omega = 1.0 - omega;

  const double* __restrict in0 = Mesh_direction_plane_const(mesh_in, 0);
  const double* __restrict in1 = Mesh_direction_plane_const(mesh_in, 1);
  const double* __restrict in2 = Mesh_direction_plane_const(mesh_in, 2);
  const double* __restrict in3 = Mesh_direction_plane_const(mesh_in, 3);
  const double* __restrict in4 = Mesh_direction_plane_const(mesh_in, 4);
  const double* __restrict in5 = Mesh_direction_plane_const(mesh_in, 5);
  const double* __restrict in6 = Mesh_direction_plane_const(mesh_in, 6);
  const double* __restrict in7 = Mesh_direction_plane_const(mesh_in, 7);
  const double* __restrict in8 = Mesh_direction_plane_const(mesh_in, 8);
  double* __restrict out0      = Mesh_direction_plane(mesh_out, 0);
  double* __restrict out1      = Mesh_direction_plane(mesh_out, 1);
  double* __restrict out2      = Mesh_direction_plane(mesh_out, 2);
  double* __restrict out3      = Mesh_direction_plane(mesh_out, 3);
  double* __restrict out4      = Mesh_direction_plane(mesh_out, 4);
  double* __restrict out5      = Mesh_direction_plane(mesh_out, 5);
  double* __restrict out6      = Mesh_direction_plane(mesh_out, 6);
  double* __restrict out7      = Mesh_direction_plane(mesh_out, 7);
  double* __restrict out8      = Mesh_direction_plane(mesh_out, 8);

#if defined(TOP_LBM_USE_OMP_TARGET)
  if (has_omp_target_device() && Mesh_has_device_data(mesh_in) && Mesh_has_device_data(mesh_out) && width > 2 && height > 2) {
    const double* __restrict in_cells = mesh_in->device_cells;
    double* __restrict out_cells      = mesh_out->device_cells;

    #pragma omp target teams distribute parallel for collapse(2) is_device_ptr(in_cells, out_cells)
    for (size_t i = 1; i < width - 1; i++) {
      for (size_t j = 1; j < height - 1; j++) {
        const size_t idx = i * height + j;
        const double f0  = in_cells[0 * plane_size + idx];
        const double f1  = in_cells[1 * plane_size + idx];
        const double f2  = in_cells[2 * plane_size + idx];
        const double f3  = in_cells[3 * plane_size + idx];
        const double f4  = in_cells[4 * plane_size + idx];
        const double f5  = in_cells[5 * plane_size + idx];
        const double f6  = in_cells[6 * plane_size + idx];
        const double f7  = in_cells[7 * plane_size + idx];
        const double f8  = in_cells[8 * plane_size + idx];

        const double density         = f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7 + f8;
        const double inv_density     = 1.0 / density;
        const double vx              = (f1 - f3 + f5 - f6 - f7 + f8) * inv_density;
        const double vy              = (f2 - f4 + f5 + f6 - f7 - f8) * inv_density;
        const double vx2             = vx * vx;
        const double vy2             = vy * vy;
        const double velocity_norm_2 = vx2 + vy2;
        const double sum             = vx + vy;
        const double diff            = vx - vy;
        const double sum2            = sum * sum;
        const double diff2           = diff * diff;
        const double common          = 1.0 - 1.5 * velocity_norm_2;
        const double rho0            = (4.0 / 9.0) * density;
        const double rho_axis        = (1.0 / 9.0) * density;
        const double rho_diag        = (1.0 / 36.0) * density;

        out_cells[0 * plane_size + idx] = one_minus_omega * f0 + omega * (rho0 * common);
        out_cells[1 * plane_size + idx] = one_minus_omega * f1 + omega * (rho_axis * (common + 3.0 * vx + 4.5 * vx2));
        out_cells[2 * plane_size + idx] = one_minus_omega * f2 + omega * (rho_axis * (common + 3.0 * vy + 4.5 * vy2));
        out_cells[3 * plane_size + idx] = one_minus_omega * f3 + omega * (rho_axis * (common - 3.0 * vx + 4.5 * vx2));
        out_cells[4 * plane_size + idx] = one_minus_omega * f4 + omega * (rho_axis * (common - 3.0 * vy + 4.5 * vy2));
        out_cells[5 * plane_size + idx] = one_minus_omega * f5 + omega * (rho_diag * (common + 3.0 * sum + 4.5 * sum2));
        out_cells[6 * plane_size + idx] = one_minus_omega * f6 + omega * (rho_diag * (common + 3.0 * (vy - vx) + 4.5 * diff2));
        out_cells[7 * plane_size + idx] = one_minus_omega * f7 + omega * (rho_diag * (common - 3.0 * sum + 4.5 * sum2));
        out_cells[8 * plane_size + idx] = one_minus_omega * f8 + omega * (rho_diag * (common + 3.0 * diff + 4.5 * diff2));
      }
    }
    return;
  }
#endif

  auto collide_columns = [&](const size_t i_begin, const size_t i_end) {
    for (size_t i = i_begin; i < i_end; i++) {
      const size_t col_base = i * height;
      const double* __restrict in0_col = in0 + col_base + 1;
      const double* __restrict in1_col = in1 + col_base + 1;
      const double* __restrict in2_col = in2 + col_base + 1;
      const double* __restrict in3_col = in3 + col_base + 1;
      const double* __restrict in4_col = in4 + col_base + 1;
      const double* __restrict in5_col = in5 + col_base + 1;
      const double* __restrict in6_col = in6 + col_base + 1;
      const double* __restrict in7_col = in7 + col_base + 1;
      const double* __restrict in8_col = in8 + col_base + 1;
      double* __restrict out0_col      = out0 + col_base + 1;
      double* __restrict out1_col      = out1 + col_base + 1;
      double* __restrict out2_col      = out2 + col_base + 1;
      double* __restrict out3_col      = out3 + col_base + 1;
      double* __restrict out4_col      = out4 + col_base + 1;
      double* __restrict out5_col      = out5 + col_base + 1;
      double* __restrict out6_col      = out6 + col_base + 1;
      double* __restrict out7_col      = out7 + col_base + 1;
      double* __restrict out8_col      = out8 + col_base + 1;

      #pragma omp simd
      for (size_t j = 1; j < height - 1; j++) {
        const size_t row = j - 1;
        const double f0  = in0_col[row];
        const double f1  = in1_col[row];
        const double f2  = in2_col[row];
        const double f3  = in3_col[row];
        const double f4  = in4_col[row];
        const double f5  = in5_col[row];
        const double f6  = in6_col[row];
        const double f7  = in7_col[row];
        const double f8  = in8_col[row];

        const double density         = f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7 + f8;
        const double inv_density     = 1.0 / density;
        const double vx              = (f1 - f3 + f5 - f6 - f7 + f8) * inv_density;
        const double vy              = (f2 - f4 + f5 + f6 - f7 - f8) * inv_density;
        const double vx2             = vx * vx;
        const double vy2             = vy * vy;
        const double velocity_norm_2 = vx2 + vy2;
        const double sum             = vx + vy;
        const double diff            = vx - vy;
        const double sum2            = sum * sum;
        const double diff2           = diff * diff;
        const double common          = 1.0 - 1.5 * velocity_norm_2;
        const double rho0            = (4.0 / 9.0) * density;
        const double rho_axis        = (1.0 / 9.0) * density;
        const double rho_diag        = (1.0 / 36.0) * density;

        out0_col[row] = one_minus_omega * f0 + omega * (rho0 * common);
        out1_col[row] = one_minus_omega * f1 + omega * (rho_axis * (common + 3.0 * vx + 4.5 * vx2));
        out2_col[row] = one_minus_omega * f2 + omega * (rho_axis * (common + 3.0 * vy + 4.5 * vy2));
        out3_col[row] = one_minus_omega * f3 + omega * (rho_axis * (common - 3.0 * vx + 4.5 * vx2));
        out4_col[row] = one_minus_omega * f4 + omega * (rho_axis * (common - 3.0 * vy + 4.5 * vy2));
        out5_col[row] = one_minus_omega * f5 + omega * (rho_diag * (common + 3.0 * sum + 4.5 * sum2));
        out6_col[row] = one_minus_omega * f6 + omega * (rho_diag * (common + 3.0 * (vy - vx) + 4.5 * diff2));
        out7_col[row] = one_minus_omega * f7 + omega * (rho_diag * (common - 3.0 * sum + 4.5 * sum2));
        out8_col[row] = one_minus_omega * f8 + omega * (rho_diag * (common + 3.0 * diff + 4.5 * diff2));
      }
    }
  };

  if (omp_get_max_threads() <= 1) {
    collide_columns(1, width - 1);
    return;
  }

  #pragma omp parallel for schedule(static)
  for (size_t i = 1; i < static_cast<size_t>(width - 1); i++) {
    collide_columns(i, i + 1);
  }
}

void propagation(Mesh* __restrict mesh_out, const Mesh* __restrict mesh_in) {
  const size_t width       = mesh_out->width;
  const size_t height      = mesh_out->height;
  const size_t plane_size  = Mesh_plane_size(mesh_out);
  const size_t total_scalars = plane_size * DIRECTIONS;

  const double* __restrict in0 = Mesh_direction_plane_const(mesh_in, 0);
  const double* __restrict in1 = Mesh_direction_plane_const(mesh_in, 1);
  const double* __restrict in2 = Mesh_direction_plane_const(mesh_in, 2);
  const double* __restrict in3 = Mesh_direction_plane_const(mesh_in, 3);
  const double* __restrict in4 = Mesh_direction_plane_const(mesh_in, 4);
  const double* __restrict in5 = Mesh_direction_plane_const(mesh_in, 5);
  const double* __restrict in6 = Mesh_direction_plane_const(mesh_in, 6);
  const double* __restrict in7 = Mesh_direction_plane_const(mesh_in, 7);
  const double* __restrict in8 = Mesh_direction_plane_const(mesh_in, 8);
  double* __restrict out0      = Mesh_direction_plane(mesh_out, 0);
  double* __restrict out1      = Mesh_direction_plane(mesh_out, 1);
  double* __restrict out2      = Mesh_direction_plane(mesh_out, 2);
  double* __restrict out3      = Mesh_direction_plane(mesh_out, 3);
  double* __restrict out4      = Mesh_direction_plane(mesh_out, 4);
  double* __restrict out5      = Mesh_direction_plane(mesh_out, 5);
  double* __restrict out6      = Mesh_direction_plane(mesh_out, 6);
  double* __restrict out7      = Mesh_direction_plane(mesh_out, 7);
  double* __restrict out8      = Mesh_direction_plane(mesh_out, 8);

  const size_t interior_width_end  = (width > 0) ? (width - 1) : 0;
  const size_t interior_height_end = (height > 0) ? (height - 1) : 0;

  if (width >= 2 && height >= 2) {
    bool interior_done_on_target = false;

    auto propagate_interior_columns = [&](const size_t i_begin, const size_t i_end) {
      for (size_t i = i_begin; i < i_end; i++) {
        const size_t col      = i * height;
        const size_t west_col = (i - 1) * height;
        const size_t east_col = (i + 1) * height;
        const double* __restrict in1_col = in1 + west_col + 1;
        const double* __restrict in2_col = in2 + col;
        const double* __restrict in3_col = in3 + east_col + 1;
        const double* __restrict in4_col = in4 + col + 2;
        const double* __restrict in5_col = in5 + west_col;
        const double* __restrict in6_col = in6 + east_col;
        const double* __restrict in7_col = in7 + east_col + 2;
        const double* __restrict in8_col = in8 + west_col + 2;
        double* __restrict out1_col      = out1 + col + 1;
        double* __restrict out2_col      = out2 + col + 1;
        double* __restrict out3_col      = out3 + col + 1;
        double* __restrict out4_col      = out4 + col + 1;
        double* __restrict out5_col      = out5 + col + 1;
        double* __restrict out6_col      = out6 + col + 1;
        double* __restrict out7_col      = out7 + col + 1;
        double* __restrict out8_col      = out8 + col + 1;

        #pragma omp simd
        for (size_t j = 1; j < interior_height_end; j++) {
          const size_t row = j - 1;
          out1_col[row] = in1_col[row];
          out2_col[row] = in2_col[row];
          out3_col[row] = in3_col[row];
          out4_col[row] = in4_col[row];
          out5_col[row] = in5_col[row];
          out6_col[row] = in6_col[row];
          out7_col[row] = in7_col[row];
          out8_col[row] = in8_col[row];
        }
      }
    };

#if defined(TOP_LBM_USE_OMP_TARGET)
    if (has_omp_target_device() && Mesh_has_device_data(mesh_in) && Mesh_has_device_data(mesh_out) && width > 2 && height > 2) {
      const double* __restrict in_cells = mesh_in->device_cells;
      double* __restrict out_cells      = mesh_out->device_cells;

      #pragma omp target teams distribute parallel for is_device_ptr(in_cells, out_cells)
      for (size_t idx = 0; idx < plane_size; idx++) {
        out_cells[idx] = in_cells[idx];
      }

      #pragma omp target teams distribute parallel for collapse(2) is_device_ptr(in_cells, out_cells)
      for (size_t i = 1; i < interior_width_end; i++) {
        for (size_t j = 1; j < interior_height_end; j++) {
          const size_t idx      = i * height + j;
          const size_t west_idx = (i - 1) * height + j;
          const size_t east_idx = (i + 1) * height + j;

          out_cells[1 * plane_size + idx] = in_cells[1 * plane_size + west_idx];
          out_cells[2 * plane_size + idx] = in_cells[2 * plane_size + (idx - 1)];
          out_cells[3 * plane_size + idx] = in_cells[3 * plane_size + east_idx];
          out_cells[4 * plane_size + idx] = in_cells[4 * plane_size + (idx + 1)];
          out_cells[5 * plane_size + idx] = in_cells[5 * plane_size + (west_idx - 1)];
          out_cells[6 * plane_size + idx] = in_cells[6 * plane_size + (east_idx - 1)];
          out_cells[7 * plane_size + idx] = in_cells[7 * plane_size + (east_idx + 1)];
          out_cells[8 * plane_size + idx] = in_cells[8 * plane_size + (west_idx + 1)];
          }
        }
      interior_done_on_target = true;
    }
#endif

    auto propagate_top_bottom = [&](const size_t i_begin, const size_t i_end) {
      if (width > 2) {
        const size_t bottom_j = 0;
        const size_t top_j    = height - 1;

        for (size_t i = i_begin; i < i_end; i++) {
          const size_t col      = i * height;
          const size_t west_col = (i - 1) * height;
          const size_t east_col = (i + 1) * height;
          const size_t bottom   = col + bottom_j;
          const size_t top      = col + top_j;

          out1[bottom] = in1[west_col + bottom_j];
          out3[bottom] = in3[east_col + bottom_j];
          out4[bottom] = in4[bottom + 1];
          out7[bottom] = in7[east_col + 1];
          out8[bottom] = in8[west_col + 1];

          out1[top] = in1[west_col + top_j];
          out2[top] = in2[top - 1];
          out3[top] = in3[east_col + top_j];
          out5[top] = in5[west_col + (top_j - 1)];
          out6[top] = in6[east_col + (top_j - 1)];
        }
      }
    };

    auto propagate_left_right = [&]() {
      if (height > 2) {
        const size_t left_col  = 0;
        const size_t right_col = (width - 1) * height;

        #pragma omp simd
        for (size_t j = 1; j < interior_height_end; j++) {
          const size_t left_idx  = left_col + j;
          const size_t right_idx = right_col + j;

          out2[left_idx] = in2[left_idx - 1];
          out3[left_idx] = in3[height + j];
          out4[left_idx] = in4[left_idx + 1];
          out6[left_idx] = in6[height + (j - 1)];
          out7[left_idx] = in7[height + (j + 1)];

          out1[right_idx] = in1[right_col - height + j];
          out2[right_idx] = in2[right_idx - 1];
          out4[right_idx] = in4[right_idx + 1];
          out5[right_idx] = in5[right_col - height + (j - 1)];
          out8[right_idx] = in8[right_col - height + (j + 1)];
        }
      }
    };

    auto propagate_corners = [&]() {
      const size_t right_col = (width - 1) * height;
      const size_t top_j     = height - 1;

      // Bottom-left corner (0, 0)
      out3[0] = in3[height];
      out4[0] = in4[1];
      out7[0] = in7[height + 1];

      // Top-left corner (0, height - 1)
      out2[top_j] = in2[top_j - 1];
      out3[top_j] = in3[height + top_j];
      out6[top_j] = in6[height + (top_j - 1)];

      // Bottom-right corner (width - 1, 0)
      out1[right_col] = in1[right_col - height];
      out4[right_col] = in4[right_col + 1];
      out8[right_col] = in8[right_col - height + 1];

      // Top-right corner (width - 1, height - 1)
      out1[right_col + top_j] = in1[right_col - height + top_j];
      out2[right_col + top_j] = in2[right_col + (top_j - 1)];
      out5[right_col + top_j] = in5[right_col - height + (top_j - 1)];
    };

    if (omp_get_max_threads() <= 1) {
      if (!interior_done_on_target) {
        std::memcpy(out0, in0, plane_size * sizeof(double));
      }
      if (!interior_done_on_target) {
        propagate_interior_columns(1, interior_width_end);
      }
      propagate_top_bottom(1, interior_width_end);
      propagate_left_right();
      propagate_corners();
    } else {
      if (!interior_done_on_target) {
        std::memcpy(out0, in0, plane_size * sizeof(double));
      }
      #pragma omp parallel
      {
        if (!interior_done_on_target) {
          #pragma omp for schedule(static)
          for (size_t i = 1; i < interior_width_end; i++) {
            propagate_interior_columns(i, i + 1);
          }
        }

        #pragma omp for schedule(static)
        for (size_t i = 1; i < interior_width_end; i++) {
          propagate_top_bottom(i, i + 1);
        }

        #pragma omp single
        {
          propagate_left_right();
        }

        #pragma omp single
        {
          propagate_corners();
        }
      }
    }
  } else {
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
            Mesh_set_value(mesh_out, ii, jj, k, Mesh_get_value(mesh_in, i, j, k));
          }
        }
      }
    }
  }
}
