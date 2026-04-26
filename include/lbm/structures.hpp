#pragma once

#include <cstdint>
#include <cstdio>

#include <lbm/config.hpp>
#include <lbm/tpl.hpp>

/// @brief A cell is an array of double `DIRECTIONS` to store microscopic / probabilities (`f_i`).
typedef double* lbm_mesh_cell_t;
typedef const double* lbm_mesh_const_cell_t;

/// @brief Representation of a vector to manipulate macroscopic velocities.
typedef double Vector[DIMENSIONS];

/// @brief Defines a mesh for the local domain.
/// This mesh contains a border for * phantom meshes of a cell.
typedef struct Mesh {
  /// Cells of a mesh of dimension `MESH_WIDTH` * `MESH_HEIGHT`.
  lbm_mesh_cell_t cells;
  /// Optional device-resident copy used by OpenMP target experiments.
  lbm_mesh_cell_t device_cells;
  /// Host staging buffer for packed halo rows.
  double* halo_row_buffer;
  /// Device staging buffer for packed halo rows.
  double* device_halo_row_buffer;
  /// Width of the local mesh (phantom meshes included).
  uint32_t width;
  /// Height of the local mesh (phantom meshes included).
  uint32_t height;
} Mesh;

/// @brief Cell types definitions in order to know which process to apply when computing.
typedef enum lbm_cell_type_e {
  /// Standard fluid cell. Applies to collisions.
  CELL_FUILD,
  /// Obstacle of top/bottom border cell. Applies to reflexions.
  CELL_BOUNCE_BACK,
  /// In-border cell. Applies to `Zou/He` with fixed `V`.
  CELL_LEFT_IN,
  /// Out-border cell. Applies to `Zou/He` with constant density gradiant.
  CELL_RIGHT_OUT
} lbm_cell_type_t;

/// @brief Array storing the information on the types of cells.
typedef struct lbm_mesh_type_s {
  /// Mesh's types of cells of dimension `MESH_WIDTH` * `MESH_HEIGHT`.
  lbm_cell_type_t* types;
  /// Optional device-resident copy used by OpenMP target experiments.
  lbm_cell_type_t* device_types;
  /// Width of the local mesh (phantom meshes included).
  uint32_t width;
  /// Height of the local mesh (phantom meshes included).
  uint32_t height;
} lbm_mesh_type_t;

/// @brief Header structure for the header of the output file.
typedef struct lbm_file_header_s {
  /// For validating the format of the file.
  uint32_t magick;
  /// Total width of the simulated mesh (phantom meshes included).
  uint32_t mesh_width;
  /// Total height of the simulated mesh (phantom meshes included).
  uint32_t mesh_height;
  /// Number of vertical lines.
  uint32_t lines;
} lbm_file_header_t;

/// @brief An entry of the file with both macroscopic quantities.
typedef struct lbm_file_entry_s {
  /// Velocity.
  float v;
  /// Density.
  float rho;
} lbm_file_entry_t;

/// @brief Structure to read the output file.
typedef struct lbm_data_file_s {
  FILE* fp;
  lbm_file_header_t header;
  lbm_file_entry_t* entries;
} lbm_data_file_t;
#define rt_tpl_sync(comm,fence,sym) catof(resolve,_tpl,_sync)<decltype(fence)>(sym)

/// @brief Initializes the local mesh.
/// @param mesh Mesh to initialize.
/// @param width Width of the mesh (phantom meshes included).
/// @param height Height of the mesh (phantom meshes included).
void Mesh_init(Mesh* mesh, uint32_t width, uint32_t height);

/// @brief Frees the memory of a mesh.
void Mesh_release(Mesh* mesh);

/// @brief Returns whether the mesh has device-resident storage.
bool Mesh_has_device_data(const Mesh* mesh);

/// @brief Copies the mesh contents from host memory to device memory when enabled.
void Mesh_sync_host_to_device(const Mesh* mesh);

/// @brief Copies the mesh contents from device memory to host memory when enabled.
void Mesh_sync_device_to_host(const Mesh* mesh);

/// @brief Copies only the interior halo-send boundary from device to host.
void Mesh_sync_halo_send_device_to_host(const Mesh* mesh);

/// @brief Copies only the ghost halo-receive boundary from host to device.
void Mesh_sync_halo_recv_host_to_device(const Mesh* mesh);

/// @brief Initializes the local mesh type.
/// @param mesh Mesh type to initialize.
/// @param width Width of the mesh (phantom meshes included).
/// @param height Height of the mesh (phantom meshes included).
void lbm_mesh_type_t_init(lbm_mesh_type_t* mesh, uint32_t width, uint32_t height);

/// @brief Frees the memory of a mesh.
void lbm_mesh_type_t_release(lbm_mesh_type_t* mesh);

/// @brief Returns whether the mesh type has device-resident storage.
bool lbm_mesh_type_has_device_data(const lbm_mesh_type_t* mesh);

/// @brief Copies the mesh type contents from host memory to device memory when enabled.
void lbm_mesh_type_sync_host_to_device(const lbm_mesh_type_t* mesh);

/// @brief Saves the current frame.
void save_frame(FILE* fp, const Mesh* mesh);

/// @brief Prints a fatal error message.
void fatal(const char* message);

/// @brief Retrieves a cell of a mesh given its coordinates.
static inline lbm_mesh_cell_t Mesh_get_cell(const Mesh* mesh, int x, int y) {
  (void)mesh;
  (void)x;
  (void)y;
  return NULL;
}

/// @brief Retrieves a column of a mesh given the `x` coordinate.
static inline lbm_mesh_cell_t Mesh_get_col(const Mesh* mesh, int x) {
  (void)mesh;
  (void)x;
  return NULL;
}

static inline size_t Mesh_scalar_index(const Mesh* mesh, uint32_t x, uint32_t y) {
  return static_cast<size_t>(x) * mesh->height + y;
}

static inline size_t Mesh_plane_size(const Mesh* mesh) {
  return static_cast<size_t>(mesh->width) * mesh->height;
}

static inline double* Mesh_direction_plane(const Mesh* mesh, uint32_t direction) {
  return &mesh->cells[static_cast<size_t>(direction) * Mesh_plane_size(mesh)];
}

static inline const double* Mesh_direction_plane_const(const Mesh* mesh, uint32_t direction) {
  return &mesh->cells[static_cast<size_t>(direction) * Mesh_plane_size(mesh)];
}

static inline double Mesh_get_value(const Mesh* mesh, uint32_t x, uint32_t y, uint32_t direction) {
  return Mesh_direction_plane_const(mesh, direction)[Mesh_scalar_index(mesh, x, y)];
}

static inline void Mesh_set_value(Mesh* mesh, uint32_t x, uint32_t y, uint32_t direction, double value) {
  Mesh_direction_plane(mesh, direction)[Mesh_scalar_index(mesh, x, y)] = value;
}

static inline void Mesh_load_cell(const Mesh* mesh, uint32_t x, uint32_t y, double* out_cell) {
  const size_t idx = Mesh_scalar_index(mesh, x, y);
  for (size_t k = 0; k < DIRECTIONS; k++) {
    out_cell[k] = Mesh_direction_plane_const(mesh, k)[idx];
  }
}

static inline void Mesh_store_cell(Mesh* mesh, uint32_t x, uint32_t y, const double* in_cell) {
  const size_t idx = Mesh_scalar_index(mesh, x, y);
  for (size_t k = 0; k < DIRECTIONS; k++) {
    Mesh_direction_plane(mesh, k)[idx] = in_cell[k];
  }
}

/// @brief Retrieves a pointer on the cell type of a mesh given its coordinates.
static inline lbm_cell_type_t* lbm_cell_type_t_get_cell(const lbm_mesh_type_t* meshtype, uint32_t x, uint32_t y) {
  return &meshtype->types[x * meshtype->height + y];
}
