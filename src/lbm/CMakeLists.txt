add_library(top::lbm-lib ALIAS top.lbm-lib)

target_sources(
  top.lbm-lib
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${PROJECT_SOURCE_DIR}/include
      FILES
        ${PROJECT_SOURCE_DIR}/include/lbm/lib.hpp
        ${PROJECT_SOURCE_DIR}/include/lbm/config.hpp
        ${PROJECT_SOURCE_DIR}/include/lbm/structures.hpp
        ${PROJECT_SOURCE_DIR}/include/lbm/communications.hpp
        ${PROJECT_SOURCE_DIR}/include/lbm/initialization.hpp
        ${PROJECT_SOURCE_DIR}/include/lbm/physics.hpp
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${CMAKE_BINARY_DIR}/include
      FILES ${CMAKE_BINARY_DIR}/include/lbm/tpl.hpp ${CMAKE_BINARY_DIR}/include/lbm/tpl_loader.hpp
  PRIVATE config.cpp structures.cpp communications.cpp initialization.cpp physics.cpp
)
target_link_libraries(top.lbm-lib PRIVATE MPI::MPI_CXX OpenMP::OpenMP_CXX)
target_compile_options(top.lbm-lib PRIVATE -march=native)
if(TOP_LBM_USE_OMP_TARGET)
  target_compile_definitions(top.lbm-lib PRIVATE TOP_LBM_USE_OMP_TARGET=1)
  target_compile_definitions(top.lbm-exe PRIVATE TOP_LBM_USE_OMP_TARGET=1)
endif()
set_target_properties(top.lbm-lib PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
