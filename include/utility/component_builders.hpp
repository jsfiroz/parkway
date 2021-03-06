#ifndef INCLUDE_UTILITY_COMPONENT_BUILDERS_HPP_
#define INCLUDE_UTILITY_COMPONENT_BUILDERS_HPP_

#include <math.h>
#include <time.h>

#include "mpi.h"
#include "hypergraph/parallel/hypergraph.hpp"
#include "controllers/serial/recursive_bisection_contoller.hpp"
#include "controllers/parallel/basic_contoller.hpp"
#include "controllers/parallel/v_cycle_final.hpp"
#include "controllers/parallel/v_cycle_all.hpp"
#include "KHMetisController.hpp"
#include "PaToHController.hpp"
#include "options.hpp"
#include "utility/logging.hpp"

namespace parallel = parkway::parallel;
namespace serial = parkway::serial;

namespace parkway {

parallel::coarsener *build_parallel_coarsener(
    int rank, const parkway::options &options, parallel::hypergraph *h,
    MPI_Comm comm);

parallel::restrictive_coarsening *build_parallel_restrictive_coarsener(
    int rank, const parkway::options &options, parallel::hypergraph *h,
    MPI_Comm comm);

parallel::refiner *build_parallel_refiner(
    int rank, const parkway::options &options, parallel::hypergraph *h,
    MPI_Comm comm);

serial::controller *build_sequential_controller(
    int rank, const parkway::options &options);

parallel::controller *build_parallel_controller(
    int rank, int num_tot_verts, parallel::coarsener *c,
    parallel::restrictive_coarsening *rc, parallel::refiner *r,
    serial::controller *s, const parkway::options &options, MPI_Comm comm);

void check_parts_and_processors(const parkway::options &options, MPI_Comm comm);

inline int get_vertex_visit_order(const int rank, const std::string &order) {
  if (order == "random") {
    return 3;
  } else if (order == "increasing") {
    return 1;
  } else if (order == "decreasing") {
    return 2;
  } else if (order == "increasing-weight") {
    return 4;
  } else if (order == "decreasing-weight") {
    return 5;
  } else {
    error_on_processor("p[%i] has invalid vertex visit order '%s'\n", rank,
                       order.c_str());
    return 0;
  }
}


inline void get_connectivity_values(int metric, int &div_by_clu_weight,
                                    int &div_by_hyperedge_len) {
  switch (metric) {
    case 0:
      div_by_clu_weight = 0;
      div_by_hyperedge_len = 0;
      return;
    case 1:
      div_by_clu_weight = 1;
      div_by_hyperedge_len = 0;
      return;
    case 2:
      div_by_clu_weight = 0;
      div_by_hyperedge_len = 1;
      return;
    case 3:
    default:
      div_by_clu_weight = 1;
      div_by_hyperedge_len = 1;
      return;
  }
}

}

#endif  // INCLUDE_UTILITY_COMPONENT_BUILDERS_HPP_
