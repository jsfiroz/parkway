
#ifndef _VCYCLE_CONTROLLER_HPP
#define _VCYCLE_CONTROLLER_HPP

// ### ParaVCycleController.hpp ###
//
// Copyright (C) 2004, Aleksandar Trifunovic, Imperial College London
//
// HISTORY:
//
// 4/1/2005: Last Modified
//
// ###

#include "ParaController.hpp"
#include "ParaRestrFCCoarsener.hpp"

typedef dynamic_array<int> IntArray;

using namespace std;

class ParaVCycleController : public ParaController {

protected:
  int limitOnCycles;
  int minInterVertIndex;

  double limAsPercentOfCut;

  stack<int> numLocCurrVerts;
  stack<int> minLocCurrVertId;
  stack<IntArray *> bestVCyclePartition;

  dynamic_array<int> mapToInterVerts;

  ParaRestrCoarsener &restrCoarsener;

public:
  ParaVCycleController(ParaRestrCoarsener &rc, ParaCoarsener &c, ParaRefiner &r,
                       SeqController &ref, int rank, int nP, int percentile,
                       int inc, int approxRef, int limit, double limitAsPercent,
                       ostream &out);

  virtual ~ParaVCycleController();
  virtual void runPartitioner(MPI_Comm comm) = 0;
  virtual void printVCycleType() const = 0;

  void setWeightConstraints(MPI_Comm comm);
  void dispParaControllerOptions() const;

  void recordVCyclePartition(const parallel_hypergraph &h, int numIteration);
  void gatherInVCyclePartition(parallel_hypergraph &h, int cut, MPI_Comm comm);

  void projectVCyclePartition(parallel_hypergraph &cG, parallel_hypergraph &fG,
                              MPI_Comm comm);
  void shuffleVCycleVertsByPartition(parallel_hypergraph &h, MPI_Comm comm);
  // void randomVCycleVertShuffle(ParaHypergraph &h, ParaHypergraph &fineH,
  // MPI_Comm comm);
  void shiftVCycleVertsToBalance(parallel_hypergraph &h, MPI_Comm comm);
  void updateMapToOrigVerts(MPI_Comm comm);
  void resetStructs();
};

#endif
