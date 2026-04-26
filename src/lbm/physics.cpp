#include <lbm/physics.hpp>

#include <cassert>
#include <cstring>
#include <cstdlib>

#include <omp.h>

#include <lbm/communications.hpp>
#include <lbm/config.hpp>
#include <lbm/structures.hpp>

namespace {
template <typename T>
static inline T* assume_aligned_64(T* ptr) {
  return static_cast<T*>(__builtin_assume_aligned(ptr, 64));
}

template <typename T>
static inline const T* assume_aligned_64(const T* ptr) {
  return static_cast<const T*>(__builtin_assume_aligned(ptr, 64));
}
}

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

  const size_t width       = mesh_in->width;
  const size_t height      = mesh_in->height;
  const double omega       = RELAX_PARAMETER;
  const double one_minus_omega = 1.0 - omega;

  const double* __restrict in0 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 0));
  const double* __restrict in1 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 1));
  const double* __restrict in2 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 2));
  const double* __restrict in3 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 3));
  const double* __restrict in4 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 4));
  const double* __restrict in5 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 5));
  const double* __restrict in6 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 6));
  const double* __restrict in7 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 7));
  const double* __restrict in8 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 8));
  double* __restrict out0      = assume_aligned_64(Mesh_direction_plane(mesh_out, 0));
  double* __restrict out1      = assume_aligned_64(Mesh_direction_plane(mesh_out, 1));
  double* __restrict out2      = assume_aligned_64(Mesh_direction_plane(mesh_out, 2));
  double* __restrict out3      = assume_aligned_64(Mesh_direction_plane(mesh_out, 3));
  double* __restrict out4      = assume_aligned_64(Mesh_direction_plane(mesh_out, 4));
  double* __restrict out5      = assume_aligned_64(Mesh_direction_plane(mesh_out, 5));
  double* __restrict out6      = assume_aligned_64(Mesh_direction_plane(mesh_out, 6));
  double* __restrict out7      = assume_aligned_64(Mesh_direction_plane(mesh_out, 7));
  double* __restrict out8      = assume_aligned_64(Mesh_direction_plane(mesh_out, 8));

  // Parallelize the independent interior columns first; this is the lowest-risk OpenMP entry point.
  #pragma omp parallel for schedule(static)
  for (size_t i = 1; i < static_cast<size_t>(width - 1); i++) {
    const size_t col_base = i * height;
    const double* __restrict p0 = in0 + col_base + 1;
    const double* __restrict p1 = in1 + col_base + 1;
    const double* __restrict p2 = in2 + col_base + 1;
    const double* __restrict p3 = in3 + col_base + 1;
    const double* __restrict p4 = in4 + col_base + 1;
    const double* __restrict p5 = in5 + col_base + 1;
    const double* __restrict p6 = in6 + col_base + 1;
    const double* __restrict p7 = in7 + col_base + 1;
    const double* __restrict p8 = in8 + col_base + 1;
    double* __restrict q0       = out0 + col_base + 1;
    double* __restrict q1       = out1 + col_base + 1;
    double* __restrict q2       = out2 + col_base + 1;
    double* __restrict q3       = out3 + col_base + 1;
    double* __restrict q4       = out4 + col_base + 1;
    double* __restrict q5       = out5 + col_base + 1;
    double* __restrict q6       = out6 + col_base + 1;
    double* __restrict q7       = out7 + col_base + 1;
    double* __restrict q8       = out8 + col_base + 1;

    #pragma omp simd
    for (size_t j = 1; j < height - 1; j++) {
      const double f0 = *p0++;
      const double f1 = *p1++;
      const double f2 = *p2++;
      const double f3 = *p3++;
      const double f4 = *p4++;
      const double f5 = *p5++;
      const double f6 = *p6++;
      const double f7 = *p7++;
      const double f8 = *p8++;

      const double density      = f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7 + f8;
      const double inv_density  = 1.0 / density;
      const double inv_density2 = inv_density * inv_density;
      const double mx           = (f1 - f3 + f5 - f6 - f7 + f8);
      const double my           = (f2 - f4 + f5 + f6 - f7 - f8);
      const double mx2          = mx * mx;
      const double my2          = my * my;
      const double momentum_sum = mx + my;
      const double momentum_dif = mx - my;
      const double common       = 1.0 - 1.5 * (mx2 + my2) * inv_density2;
      const double rho0         = (4.0 / 9.0) * density;
      const double rho_axis     = (1.0 / 9.0) * density;
      const double rho_diag     = (1.0 / 36.0) * density;
      const double axis_x       = 3.0 * mx * inv_density;
      const double axis_y       = 3.0 * my * inv_density;
      const double diag_sum     = 3.0 * momentum_sum * inv_density;
      const double diag_dif     = 3.0 * momentum_dif * inv_density;
      const double momentum_x   = 4.5 * mx2 * inv_density2;
      const double momentum_y   = 4.5 * my2 * inv_density2;
      const double momentum_sum2 = 4.5 * momentum_sum * momentum_sum * inv_density2;
      const double momentum_dif2 = 4.5 * momentum_dif * momentum_dif * inv_density2;

      *q0++ = one_minus_omega * f0 + omega * (rho0 * common);
      *q1++ = one_minus_omega * f1 + omega * (rho_axis * (common + axis_x + momentum_x));
      *q2++ = one_minus_omega * f2 + omega * (rho_axis * (common + axis_y + momentum_y));
      *q3++ = one_minus_omega * f3 + omega * (rho_axis * (common - axis_x + momentum_x));
      *q4++ = one_minus_omega * f4 + omega * (rho_axis * (common - axis_y + momentum_y));
      *q5++ = one_minus_omega * f5 + omega * (rho_diag * (common + diag_sum + momentum_sum2));
      *q6++ = one_minus_omega * f6 + omega * (rho_diag * (common - diag_dif + momentum_dif2));
      *q7++ = one_minus_omega * f7 + omega * (rho_diag * (common - diag_sum + momentum_sum2));
      *q8++ = one_minus_omega * f8 + omega * (rho_diag * (common + diag_dif + momentum_dif2));
    }
  }
}

void propagation(Mesh* __restrict mesh_out, const Mesh* __restrict mesh_in) {
  const size_t width       = mesh_out->width;
  const size_t height      = mesh_out->height;
  const size_t plane_size  = Mesh_plane_size(mesh_out);

  const double* __restrict in0 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 0));
  const double* __restrict in1 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 1));
  const double* __restrict in2 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 2));
  const double* __restrict in3 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 3));
  const double* __restrict in4 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 4));
  const double* __restrict in5 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 5));
  const double* __restrict in6 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 6));
  const double* __restrict in7 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 7));
  const double* __restrict in8 = assume_aligned_64(Mesh_direction_plane_const(mesh_in, 8));
  double* __restrict out0      = assume_aligned_64(Mesh_direction_plane(mesh_out, 0));
  double* __restrict out1      = assume_aligned_64(Mesh_direction_plane(mesh_out, 1));
  double* __restrict out2      = assume_aligned_64(Mesh_direction_plane(mesh_out, 2));
  double* __restrict out3      = assume_aligned_64(Mesh_direction_plane(mesh_out, 3));
  double* __restrict out4      = assume_aligned_64(Mesh_direction_plane(mesh_out, 4));
  double* __restrict out5      = assume_aligned_64(Mesh_direction_plane(mesh_out, 5));
  double* __restrict out6      = assume_aligned_64(Mesh_direction_plane(mesh_out, 6));
  double* __restrict out7      = assume_aligned_64(Mesh_direction_plane(mesh_out, 7));
  double* __restrict out8      = assume_aligned_64(Mesh_direction_plane(mesh_out, 8));

  std::memcpy(out0, in0, plane_size * sizeof(double));

  // Interior cells dominate runtime, so propagate them with a branchless pull-style kernel.
  const size_t interior_width_end  = (width > 0) ? (width - 1) : 0;
  const size_t interior_height_end = (height > 0) ? (height - 1) : 0;

  if (width >= 2 && height >= 2) {
    #pragma omp parallel
    {
      #pragma omp for schedule(static)
      for (size_t i = 1; i < interior_width_end; i++) {
        const size_t col      = i * height;
        const size_t west_col = (i - 1) * height;
        const size_t east_col = (i + 1) * height;
        const double* __restrict west1 = in1 + west_col + 1;
        const double* __restrict west5 = in5 + west_col;
        const double* __restrict west8 = in8 + west_col + 2;
        const double* __restrict ctr2  = in2 + col;
        const double* __restrict ctr4  = in4 + col + 2;
        const double* __restrict east3 = in3 + east_col + 1;
        const double* __restrict east6 = in6 + east_col;
        const double* __restrict east7 = in7 + east_col + 2;
        double* __restrict dst1        = out1 + col + 1;
        double* __restrict dst2        = out2 + col + 1;
        double* __restrict dst3        = out3 + col + 1;
        double* __restrict dst4        = out4 + col + 1;
        double* __restrict dst5        = out5 + col + 1;
        double* __restrict dst6        = out6 + col + 1;
        double* __restrict dst7        = out7 + col + 1;
        double* __restrict dst8        = out8 + col + 1;

        #pragma omp simd
        for (size_t j = 1; j < interior_height_end; j++) {
          *dst1++ = *west1++;
          *dst2++ = *ctr2++;
          *dst3++ = *east3++;
          *dst4++ = *ctr4++;
          *dst5++ = *west5++;
          *dst6++ = *east6++;
          *dst7++ = *east7++;
          *dst8++ = *west8++;
        }
      }

      // Handle the borders separately so the dominant interior kernel stays branch-free.
      #pragma omp single
      {
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
      }

      if (width > 2) {
        const size_t bottom_j = 0;
        const size_t top_j    = height - 1;

        #pragma omp for schedule(static)
        for (size_t i = 1; i < interior_width_end; i++) {
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

      #pragma omp single
      {
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
