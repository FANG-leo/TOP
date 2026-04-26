#include <lbm/structures.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr size_t kCacheLineBytes = 64;
}

void Mesh_init(Mesh* mesh, uint32_t width, uint32_t height) {
  // Setup parameters
  mesh->width  = width;
  mesh->height = height;

  // Allocate memory for cells
  const size_t bytes = static_cast<size_t>(width) * height * DIRECTIONS * sizeof(double);
  if (posix_memalign(reinterpret_cast<void**>(&mesh->cells), kCacheLineBytes, bytes) != 0) {
    perror("posix_memalign");
    abort();
  }
}

void Mesh_release(Mesh* mesh) {
  mesh->width  = 0;
  mesh->height = 0;
  free(mesh->cells);
}

void lbm_mesh_type_t_init(lbm_mesh_type_t* meshtype, uint32_t width, uint32_t height) {
  // Setup parameters
  meshtype->width  = width;
  meshtype->height = height;

  // Allocate memory for cells
  const size_t bytes = static_cast<size_t>(width + 2) * height * sizeof(lbm_cell_type_t);
  if (posix_memalign(reinterpret_cast<void**>(&meshtype->types), kCacheLineBytes, bytes) != 0) {
    perror("posix_memalign");
    abort();
  }
}

void lbm_mesh_type_t_release(lbm_mesh_type_t* mesh) {
  mesh->width  = 0;
  mesh->height = 0;
  free(mesh->types);
}

void fatal(const char* message) {
  fprintf(stderr, "FATAL ERROR : %s\n", message);
  abort();
}
