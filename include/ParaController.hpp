
#ifndef _PARA_CONTROLLER_HPP
#define _PARA_CONTROLLER_HPP

// ### ParaController.hpp ###
//
// Copyright (C) 2004, Aleksandar Trifunovic, Imperial College London
//
// HISTORY:
//
// 4/1/2005: Last Modified
//
// ###

#include <iostream>
#include "data_structures/stack.hpp"
#include "ParaFCCoarsener.hpp"
#include "Para2DModelCoarsener.hpp"
#include "ParaApproxFCCoarsener.hpp"
#include "ParaRestrFCCoarsener.hpp"
#include "ParaGreedyKwayRefiner.hpp"
#include "SeqController.hpp"

class ParaController : public global_communicator {
 protected:
  std::ostream &out_stream;

  /* Parallel partitioning options */

  int randShuffBefRef;
  int shuffled;
  int numParaRuns;
  int numTotalParts;
  int maxPartWt;
  int numOrigLocVerts;

  double keepPartitionsWithin;
  double reductionInKeepThreshold;

  /* partition used to assign vertices to processors */

  dynamic_array<int> shufflePartition;

  /* Approx coarsening and refinement options */

  int startPercentile;
  int percentileIncrement;
  int approxRefine;

  /* Other options */

  int writePartitionToFile;
  int dispOption;

  double balConstraint;

  /* auxiliary variables */

  int bestCutsize;
  int worstCutsize;
  int totCutsizes;

  double aveCutSize;
  double startTime;
  double totCoaTime;
  double totSeqTime;
  double totRefTime;
  double totalTime;
  double accumulator;

  dynamic_array<int> bestPartition;
  dynamic_array<int> mapToOrigVerts;

  parallel_hypergraph *hgraph;

  ParaCoarsener &coarsener;
  ParaRefiner &refiner;
  SeqController &seqController;
  parkway::data_structures::stack<parallel_hypergraph *> hgraphs;

 public:
  ParaController(ParaCoarsener &c, ParaRefiner &r, SeqController &ref, int rank,
                 int nP, int percentile, int inc, int approxRefine,
                 std::ostream &out);

  virtual ~ParaController();
  virtual void dispParaControllerOptions() const = 0;
  virtual void resetStructs() = 0;
  virtual void runPartitioner(MPI_Comm comm) = 0;
  virtual void setWeightConstraints(MPI_Comm comm);

  inline int getNumParaRuns() const { return numParaRuns; }
  inline int getNumParts() const { return numTotalParts; }
  inline int getMaxPartWt() const { return maxPartWt; }
  inline int getBestCutsize() const { return bestCutsize; }

  inline double getBalConstraint() const { return balConstraint; }

  inline void setNumParaRuns(int n) { numParaRuns = n; }
  inline void setNumParts(int n) { numTotalParts = n; }
  inline void setBalConstraint(double b) { balConstraint = b; }
  inline void setDispOpt(int dO) { dispOption = dO; }
  inline void setReduction(double r) { reductionInKeepThreshold = r; }
  inline void setKTFactor(double kT) { keepPartitionsWithin = kT; }
  inline void setShuffleVertices(int s) { shuffled = s; }
  inline void setRandShuffBefRef(int s) { randShuffBefRef = s; }
  inline void setGraph(parallel_hypergraph *graph) {
    hgraph = graph;
    numOrigLocVerts = hgraph->getNumLocalVertices();
  }

  // void setShuffleFile(const char *filename);
  void initMapToOrigVerts();
  void setPrescribedPartition(const char *filename, MPI_Comm comm);
  void storeBestPartition(int numV, const int *array, MPI_Comm comm);
  void partitionToFile(const char *filename, MPI_Comm comm) const;
  void copyOutPartition(int numVertices, int *pVector) const;
  void displayPartitionInfo(MPI_Comm comm) const;
#ifdef DEBUG_TABLES
  void printHashMemUse();
#endif
};

#endif
