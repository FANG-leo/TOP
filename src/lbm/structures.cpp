#include <lbm/structures.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#if defined(TOP_LBM_USE_OMP_TARGET)
  #include <omp.h>
#endif

namespace {
static size_t mesh_total_scalar_count(uint32_t width, uint32_t height) {
  return static_cast<size_t>(width) * height * DIRECTIONS;
}

static size_t mesh_type_scalar_count(uint32_t width, uint32_t height) {
  return static_cast<size_t>(width) * height;
}

static size_t halo_row_scalar_count(const Mesh* mesh) {
  return static_cast<size_t>(mesh->width > 2 ? mesh->width - 2 : 0) * DIRECTIONS;
}

static void copy_mesh_column_between_host_device(const Mesh* mesh, uint32_t x, bool device_to_host) {
#if defined(TOP_LBM_USE_OMP_TARGET)
  if (mesh->device_cells == NULL) {
    return;
  }

  const size_t plane_size = static_cast<size_t>(mesh->width) * mesh->height;
  const size_t column_len = mesh->height;
  const int dst_device = device_to_host ? omp_get_initial_device() : omp_get_default_device();
  const int src_device = device_to_host ? omp_get_default_device() : omp_get_initial_device();
  for (size_t k = 0; k < DIRECTIONS; k++) {
    const size_t offset = k * plane_size + static_cast<size_t>(x) * mesh->height;
    omp_target_memcpy(
      device_to_host ? (mesh->cells + offset) : (mesh->device_cells + offset),
      device_to_host ? (mesh->device_cells + offset) : (mesh->cells + offset),
      column_len * sizeof(double),
      0,
      0,
      dst_device,
      src_device
    );
  }
#else
  (void)mesh;
  (void)x;
  (void)device_to_host;
#endif
}

static void copy_mesh_row_between_host_device(const Mesh* mesh, uint32_t y, bool device_to_host) {
#if defined(TOP_LBM_USE_OMP_TARGET)
  if (mesh->device_cells == NULL || mesh->halo_row_buffer == NULL || mesh->device_halo_row_buffer == NULL) {
    return;
  }

  const size_t plane_size = static_cast<size_t>(mesh->width) * mesh->height;
  const size_t row_scalars = halo_row_scalar_count(mesh);
  double* __restrict device_cells = mesh->device_cells;
  double* __restrict device_row_buffer = mesh->device_halo_row_buffer;
  if (row_scalars == 0) {
    return;
  }

  if (device_to_host) {
    #pragma omp target teams distribute parallel for is_device_ptr(device_cells, device_row_buffer)
    for (size_t idx = 0; idx < row_scalars; idx++) {
      const size_t k = idx / static_cast<size_t>(mesh->width - 2);
      const size_t x = idx % static_cast<size_t>(mesh->width - 2) + 1;
      device_row_buffer[idx] = device_cells[k * plane_size + x * mesh->height + y];
    }

    omp_target_memcpy(
      mesh->halo_row_buffer,
      device_row_buffer,
      row_scalars * sizeof(double),
      0,
      0,
      omp_get_initial_device(),
      omp_get_default_device()
    );

    for (size_t k = 0; k < DIRECTIONS; k++) {
      double* plane = Mesh_direction_plane(const_cast<Mesh*>(mesh), static_cast<uint32_t>(k));
      for (size_t x = 1; x < mesh->width - 1; x++) {
        plane[x * mesh->height + y] = mesh->halo_row_buffer[k * static_cast<size_t>(mesh->width - 2) + (x - 1)];
      }
    }
  } else {
    for (size_t k = 0; k < DIRECTIONS; k++) {
      const double* plane = Mesh_direction_plane_const(mesh, static_cast<uint32_t>(k));
      for (size_t x = 1; x < mesh->width - 1; x++) {
        mesh->halo_row_buffer[k * static_cast<size_t>(mesh->width - 2) + (x - 1)] = plane[x * mesh->height + y];
      }
    }

    omp_target_memcpy(
      device_row_buffer,
      mesh->halo_row_buffer,
      row_scalars * sizeof(double),
      0,
      0,
      omp_get_default_device(),
      omp_get_initial_device()
    );

    #pragma omp target teams distribute parallel for is_device_ptr(device_cells, device_row_buffer)
    for (size_t idx = 0; idx < row_scalars; idx++) {
      const size_t k = idx / static_cast<size_t>(mesh->width - 2);
      const size_t x = idx % static_cast<size_t>(mesh->width - 2) + 1;
      device_cells[k * plane_size + x * mesh->height + y] = device_row_buffer[idx];
    }
  }
#else
  (void)mesh;
  (void)y;
  (void)device_to_host;
#endif
}
}

void Mesh_init(Mesh* mesh, uint32_t width, uint32_t height) {
  // Setup parameters
  mesh->width  = width;
  mesh->height = height;

  // Allocate memory for cells
  mesh->cells = static_cast<double*>(malloc(width * height * DIRECTIONS * sizeof(double)));
  mesh->device_cells = NULL;
  mesh->halo_row_buffer = NULL;
  mesh->device_halo_row_buffer = NULL;

  const size_t row_scalars = static_cast<size_t>(width > 2 ? width - 2 : 0) * DIRECTIONS;
  if (row_scalars > 0) {
    mesh->halo_row_buffer = static_cast<double*>(malloc(row_scalars * sizeof(double)));
  }

#if defined(TOP_LBM_USE_OMP_TARGET)
  if (omp_get_num_devices() > 0) {
    mesh->device_cells = static_cast<double*>(
      omp_target_alloc(mesh_total_scalar_count(width, height) * sizeof(double), omp_get_default_device())
    );
    if (row_scalars > 0) {
      mesh->device_halo_row_buffer = static_cast<double*>(
        omp_target_alloc(row_scalars * sizeof(double), omp_get_default_device())
      );
    }
  }
#endif
}

void Mesh_release(Mesh* mesh) {
  mesh->width  = 0;
  mesh->height = 0;
#if defined(TOP_LBM_USE_OMP_TARGET)
  if (mesh->device_cells != NULL) {
    omp_target_free(mesh->device_cells, omp_get_default_device());
  }
  if (mesh->device_halo_row_buffer != NULL) {
    omp_target_free(mesh->device_halo_row_buffer, omp_get_default_device());
  }
#endif
  mesh->device_cells = NULL;
  mesh->device_halo_row_buffer = NULL;
  free(mesh->halo_row_buffer);
  mesh->halo_row_buffer = NULL;
  free(mesh->cells);
}

bool Mesh_has_device_data(const Mesh* mesh) {
  return mesh->device_cells != NULL;
}

void Mesh_sync_host_to_device(const Mesh* mesh) {
#if defined(TOP_LBM_USE_OMP_TARGET)
  if (mesh->device_cells != NULL) {
    omp_target_memcpy(
      mesh->device_cells,
      mesh->cells,
      mesh_total_scalar_count(mesh->width, mesh->height) * sizeof(double),
      0,
      0,
      omp_get_default_device(),
      omp_get_initial_device()
    );
  }
#else
  (void)mesh;
#endif
}

void Mesh_sync_device_to_host(const Mesh* mesh) {
#if defined(TOP_LBM_USE_OMP_TARGET)
  if (mesh->device_cells != NULL) {
    omp_target_memcpy(
      mesh->cells,
      mesh->device_cells,
      mesh_total_scalar_count(mesh->width, mesh->height) * sizeof(double),
      0,
      0,
      omp_get_initial_device(),
      omp_get_default_device()
    );
  }
#else
  (void)mesh;
#endif
}

void Mesh_sync_halo_send_device_to_host(const Mesh* mesh) {
#if defined(TOP_LBM_USE_OMP_TARGET)
  if (mesh->device_cells == NULL) {
    return;
  }

  if (mesh->width > 2) {
    copy_mesh_column_between_host_device(mesh, 1, true);
    copy_mesh_column_between_host_device(mesh, mesh->width - 2, true);
  }
  if (mesh->height > 2) {
    copy_mesh_row_between_host_device(mesh, 1, true);
    copy_mesh_row_between_host_device(mesh, mesh->height - 2, true);
  }
#else
  (void)mesh;
#endif
}

void Mesh_sync_halo_recv_host_to_device(const Mesh* mesh) {
#if defined(TOP_LBM_USE_OMP_TARGET)
  if (mesh->device_cells == NULL) {
    return;
  }

  if (mesh->width >= 2) {
    copy_mesh_column_between_host_device(mesh, 0, false);
    copy_mesh_column_between_host_device(mesh, mesh->width - 1, false);
  }
  if (mesh->height >= 2) {
    copy_mesh_row_between_host_device(mesh, 0, false);
    copy_mesh_row_between_host_device(mesh, mesh->height - 1, false);
  }
#else
  (void)mesh;
#endif
}

void lbm_mesh_type_t_init(lbm_mesh_type_t* meshtype, uint32_t width, uint32_t height) {
  // Setup parameters
  meshtype->width  = width;
  meshtype->height = height;

  // Allocate memory for cells
  meshtype->types = static_cast<lbm_cell_type_t*>(malloc((width + 2) * height * sizeof(lbm_cell_type_t)));
  meshtype->device_types = NULL;
  if (meshtype->types == NULL) {
    perror("malloc");
    abort();
  }

#if defined(TOP_LBM_USE_OMP_TARGET)
  if (omp_get_num_devices() > 0) {
    meshtype->device_types = static_cast<lbm_cell_type_t*>(
      omp_target_alloc(mesh_type_scalar_count(width + 2, height) * sizeof(lbm_cell_type_t), omp_get_default_device())
    );
  }
#endif
}

void lbm_mesh_type_t_release(lbm_mesh_type_t* mesh) {
  mesh->width  = 0;
  mesh->height = 0;
#if defined(TOP_LBM_USE_OMP_TARGET)
  if (mesh->device_types != NULL) {
    omp_target_free(mesh->device_types, omp_get_default_device());
  }
#endif
  mesh->device_types = NULL;
  free(mesh->types);
}

bool lbm_mesh_type_has_device_data(const lbm_mesh_type_t* mesh) {
  return mesh->device_types != NULL;
}

void lbm_mesh_type_sync_host_to_device(const lbm_mesh_type_t* mesh) {
#if defined(TOP_LBM_USE_OMP_TARGET)
  if (mesh->device_types != NULL) {
    omp_target_memcpy(
      mesh->device_types,
      mesh->types,
      mesh_type_scalar_count(mesh->width, mesh->height) * sizeof(lbm_cell_type_t),
      0,
      0,
      omp_get_default_device(),
      omp_get_initial_device()
    );
  }
#else
  (void)mesh;
#endif
}

void fatal(const char* message) {
  fprintf(stderr, "FATAL ERROR : %s\n", message);
  abort();
}
