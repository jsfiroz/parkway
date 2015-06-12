#ifndef _PARA_2DMODEL_COARSENER_CPP
#define _PARA_2DMODEL_COARSENER_CPP

// ### Para2DModelCoarsener.cpp ###
//
// Copyright (C) 2004, Aleksandar Trifunovic, Imperial College London
//
// HISTORY:
//
// 19/04/2005: Last Modified
//
// ###

#include "Para2DModelCoarsener.hpp"
#include "data_structures/map_to_pos_int.hpp"
#include "data_structures/internal/table_utils.hpp"

Para2DModelCoarsener::Para2DModelCoarsener(int rank, int nProcs, int nParts,
                                           int vertVisOrder, int matchReqOrder,
                                           int divByWt, int divByLen,
                                           std::ostream &out)
    : ParaCoarsener(rank, nProcs, nParts, out) {
  vertexVisitOrder = vertVisOrder;
  matchRequestVisitOrder = matchReqOrder;
  divByCluWt = divByWt;
  divByHedgeLen = divByLen;
  limitOnIndexDuringCoarsening = 0;

  table = nullptr;
}

Para2DModelCoarsener::~Para2DModelCoarsener() {
  DynaMem::deletePtr<ds::match_request_table>(table);
}

void Para2DModelCoarsener::dispCoarseningOptions() const {
  switch (dispOption) {
  case SILENT:

    break;

  default:

    out_stream << "|--- PARA_C:" << std::endl
               << "|- 2DModel:"
               << " r = " << reductionRatio << " min = " << minNodes
               << " vvo = ";
    printVisitOrder(vertexVisitOrder);
    out_stream << " mvo = ";
    printVisitOrder(matchRequestVisitOrder);
    out_stream << " divWt = " << divByCluWt << " divLen = " << divByHedgeLen
               << " threshold = " << 1e6 << std::endl
               << "|" << std::endl;
    break;
  }
}

void Para2DModelCoarsener::buildAuxiliaryStructs(int numTotPins,
                                                 double aveVertDeg,
                                                 double aveHedgeSize) {
  // ###
  // build the ds::match_request_table
  // ###

  int i =
      static_cast<int>(ceil(static_cast<double>(numTotPins) / aveVertDeg));

  table = new ds::match_request_table(
      ds::internal::table_utils::table_size(i / processors_));
}

void Para2DModelCoarsener::releaseMemory() {
  hEdgeWeight.reserve(0);
  hEdgeOffset.reserve(0);
  locPinList.reserve(0);

  vToHedgesOffset.reserve(0);
  vToHedgesList.reserve(0);
  allocHedges.reserve(0);
  clusterWeights.reserve(0);

    free_memory();
}

parallel_hypergraph *Para2DModelCoarsener::coarsen(parallel_hypergraph &h,
                                              MPI_Comm comm) {
  loadHyperGraph(h, comm);

  // if(rank_ == 0)
  // std::cout << "loaded hypergraph" << std::endl;
  // MPI_Barrier(comm);

  if (totalVertices < minNodes || h.dontCoarsen())
    return nullptr;

  if (totalVertices < 3000000) {
    // if(rank_ == 0) std::cout << "ParaFCC " << std::endl;
    return (ParaFCCoarsen(h, comm));
  } else {
    // if(rank_ == 0) std::cout << "ParaHedgeC " << std::endl;
    return (ParaHedgeCoarsen(h, comm));
  }
}

parallel_hypergraph *Para2DModelCoarsener::ParaFCCoarsen(parallel_hypergraph &h,
                                                    MPI_Comm comm) {
#ifdef MEM_CHECK
  MPI_Barrier(comm);
  write_log(rank_, "[begin PFCC]: usage: %f", MemoryTracker::usage());
  Funct::printMemUse(rank_, "[begin PFCC]");
#endif

  int i;
  int j;

  int index = 0;
  int numNotMatched = numLocalVertices;
  int maxLocWt = 0;
  int maxWt;
  int aveVertexWt = static_cast<int>(
      ceil(static_cast<double>(totalHypergraphWt) / totalVertices));
  int bestMatch;
  int bestMatchWt = -1;
  int numVisited;
  int vPerProc = totalVertices / processors_;
  int endOffset1;
  int endOffset2;
  int cluWeight;
  int globalVertexIndex;
  int candVwt;
  int nonLocV;
  int candidatV;
  int hEdgeLen;
  int pairWt;
  int neighbourLoc;

  int hEdge;
  int vertex;

  double metric;
  double maxMatchMetric;
  double reducedBy;

  ds::map_to_pos_int matchInfoLoc;

  if (numLocPins < totalVertices / 2)
    matchInfoLoc.create(numLocPins, 1);
  else
    matchInfoLoc.create(totalVertices, 0);

  dynamic_array<int> neighVerts;
  dynamic_array<int> neighCluWts;
  dynamic_array<double> connectVals;
  dynamic_array<int> vertices(numLocalVertices);

  permuteVerticesArray(vertices.data(), numLocalVertices);

  if (dispOption > 1) {
    for (i = 0; i < numLocalVertices; ++i) {
      if (vWeight[i] > maxLocWt)
        maxLocWt = vWeight[i];
    }

    MPI_Reduce(&maxLocWt, &maxWt, 1, MPI_INT, MPI_MAX, 0, comm);

    if (rank_ == 0) {
      out_stream << " " << maxVertexWt << " " << maxWt << " " << aveVertexWt
                 << " ";
      out_stream.flush();
    }
  }

  metric = static_cast<double>(numLocalVertices) / reductionRatio;
  limitOnIndexDuringCoarsening =
      numLocalVertices - static_cast<int>(floor(metric - 1.0));
  clusterIndex = 0;
  stopCoarsening = 0;

  // write_log(rank_, "numLocalVertices = %d, numLocalPins = %d, numHedges =
  // %d", numLocalVertices, numLocPins, numHedges);

  for (; index < numLocalVertices; ++index) {
    if (index % 50000 == 0 && rank_ == 0)
      write_log(rank_, "considering local vertex index %d", index);

    if (matchVector[vertices[index]] == -1) {
      vertex = vertices[index];
#ifdef DEBUG_COARSENER
      assert(vertex >= 0 && vertex < numLocalVertices);
#endif
      globalVertexIndex = vertex + minVertexIndex;
      endOffset1 = vToHedgesOffset[vertex + 1];
      bestMatch = -1;
      maxMatchMetric = 0.0;
      numVisited = 0;

      for (i = vToHedgesOffset[vertex]; i < endOffset1; ++i) {
        hEdge = vToHedgesList[i];
        endOffset2 = hEdgeOffset[hEdge + 1];
        hEdgeLen = endOffset2 - hEdgeOffset[hEdge];

        for (j = hEdgeOffset[hEdge]; j < endOffset2; ++j) {

          candidatV = locPinList[j];
#ifdef DEBUG_COARSENER
          assert(candidatV >= 0 && candidatV < totalVertices);
#endif
          if (candidatV != globalVertexIndex) {
            /* now try not to check the weight before checking the vertex */

            neighbourLoc = matchInfoLoc.get_careful(candidatV);

            if (neighbourLoc >= 0) {
              if (divByHedgeLen)
                connectVals[neighbourLoc] +=
                    (static_cast<double>(hEdgeWeight[hEdge]) / (hEdgeLen - 1));
              else
                connectVals[neighbourLoc] +=
                    (static_cast<double>(hEdgeWeight[hEdge]));
            } else {
              // ###
              // here compute the cluster weight
              // ###

              if (candidatV >= minVertexIndex && candidatV < maxVertexIndex) {
                // ###
                // candidatV is a local vertex
                // ###

                if (matchVector[candidatV - minVertexIndex] == -1)
                  cluWeight =
                      vWeight[vertex] + vWeight[candidatV - minVertexIndex];
                else if (matchVector[candidatV - minVertexIndex] >=
                         NON_LOCAL_MATCH) {
                  nonLocV =
                      matchVector[candidatV - minVertexIndex] - NON_LOCAL_MATCH;
                  cluWeight = vWeight[vertex] +
                              table->cluster_weight(nonLocV) + aveVertexWt;
                } else
                  cluWeight =
                      vWeight[vertex] +
                      clusterWeights[matchVector[candidatV - minVertexIndex]];
              } else {
                // ###
                // candidatV is not a local vertex or it is a local
                // vertex matched with a non-local one
                // ###

                candVwt = table->cluster_weight(candidatV);

                if (candVwt != -1)
                  cluWeight = vWeight[vertex] + candVwt + aveVertexWt;
                else
                  cluWeight = vWeight[vertex] + aveVertexWt;
              }

#ifdef DEBUG_COARSENER
              assert(numVisited >= 0);
#endif
              if (matchInfoLoc.insert(candidatV, numVisited)) {
                write_log(rank_, "numEntries %d",
                          matchInfoLoc.size());
                write_log(rank_, "using hash %d",
                          matchInfoLoc.use_hash());
                write_log(rank_, "numSlots %d", matchInfoLoc.capacity());
                write_log(rank_, "candidatV %d", candidatV);
                write_log(rank_, "neighbourLoc %d", neighbourLoc);
                assert(0);
              }

              neighVerts.assign(numVisited, candidatV);
              neighCluWts.assign(numVisited, cluWeight);

              if (divByHedgeLen)
                connectVals.assign(numVisited++,
                                   static_cast<double>(hEdgeWeight[hEdge]) /
                                       (hEdgeLen - 1));
              else
                connectVals.assign(numVisited++,
                                   static_cast<double>(hEdgeWeight[hEdge]));
            }
          }
        }
      }

      // ###
      // now choose best match from adjacent vertices
      // visited above
      // ###

      for (i = 0; i < numVisited; ++i) {
        pairWt = neighCluWts[i];
        candidatV = neighVerts[i];

        if (pairWt <= maxVertexWt) {
          metric = connectVals[i];

          if (divByCluWt)
            metric /= pairWt;

          if (metric > maxMatchMetric) {
            maxMatchMetric = metric;
            bestMatch = candidatV;
            bestMatchWt = pairWt;

#ifdef DEBUG_COARSENER
            assert(bestMatch >= 0);
#endif
          }
        }
      }

      matchInfoLoc.clear();
      numVisited = 0;

      if (bestMatch == -1) {
        // ###
        // match as singleton
        // ###

        matchVector[vertex] = clusterIndex;
        clusterWeights.assign(clusterIndex++, vWeight[vertex]);
        --numNotMatched;
      } else {
#ifdef DEBUG_COARSENER
        assert(bestMatch >= 0);
#endif

        if (bestMatch >= minVertexIndex && bestMatch < maxVertexIndex) {
          // ###
          // best match is a local vertex
          // ###

          if (matchVector[bestMatch - minVertexIndex] == -1) {
            matchVector[bestMatch - minVertexIndex] = clusterIndex;
            matchVector[vertex] = clusterIndex;
            clusterWeights.assign(clusterIndex++, bestMatchWt);
            numNotMatched -= 2;
          } else {
            if (matchVector[bestMatch - minVertexIndex] >= NON_LOCAL_MATCH) {
              nonLocV =
                  matchVector[bestMatch - minVertexIndex] - NON_LOCAL_MATCH;
              table->add_local(nonLocV, vertex + minVertexIndex,
                               vWeight[vertex],
                               std::min(nonLocV / vPerProc, processors_ - 1));
#ifdef DEBUG_COARSENER
              assert(std::min(nonLocV / vPerProc, processors_ - 1) != rank_);
#endif
              matchVector[vertex] = NON_LOCAL_MATCH + nonLocV;
              --numNotMatched;
            } else {
              matchVector[vertex] = matchVector[bestMatch - minVertexIndex];
              clusterWeights[matchVector[vertex]] +=
                  vWeight[vertex]; // bestMatchWt;
              --numNotMatched;
            }
          }
        } else {
          // ###
          // best match is not a local vertex
          // ###

          table->add_local(bestMatch, vertex + minVertexIndex, vWeight[vertex],
                           std::min(bestMatch / vPerProc, processors_ - 1));
#ifdef DEBUG_COARSENER
          assert(std::min(bestMatch / vPerProc, processors_ - 1) != rank_);
#endif
          matchVector[vertex] = NON_LOCAL_MATCH + bestMatch;
          --numNotMatched;
        }
      }

      // ###
      // check the amount that the hypergraph has shrunk by
      // ###

      if (index > limitOnIndexDuringCoarsening) {
        reducedBy = static_cast<double>(numLocalVertices) /
                    (numNotMatched + clusterIndex + table->size());
        break;
      }
    }
  }

  // if(rank_ == 0)
  //  std::cout << "done local match" << std::endl;
  // MPI_Barrier(comm);

  matchInfoLoc.destroy();

  // ###
  // now carry over all the unmatched vertices as singletons
  // ###

  for (; index < numLocalVertices; ++index) {
    vertex = vertices[index];

    if (matchVector[vertex] == -1) {
      matchVector[vertex] = clusterIndex;
      clusterWeights.assign(clusterIndex++, vWeight[vertex]);
    }
  }

  // ###
  // now need to prepare and send out the matching requests
  // ###

  for (i = 0; i < 2; ++i) {
    setRequestArrays(i);
      send_from_data_out(comm); // actually sending requests
    setReplyArrays(i, maxVertexWt);
      send_from_data_out(comm); // actually sending replies
    processReqReplies();
  }

  setClusterIndices(comm);

  if (static_cast<double>(totalVertices) / totalClusters <
      MIN_ALLOWED_REDUCTION_RATIO) {
    stopCoarsening = 1;
  }

  table->clear();

  // ###
  // now construct the coarse hypergraph using the matching vector
  // ###

  // if(rank_ == 0)
  // std::cout << "about to contract hyperedges" << std::endl;
  // MPI_Barrier(comm);
  return (contractHyperedges(h, comm));
}

parallel_hypergraph *Para2DModelCoarsener::ParaHedgeCoarsen(parallel_hypergraph &h,
                                                       MPI_Comm comm) {
#ifdef MEM_CHECK
  MPI_Barrier(comm);
  write_log(rank_, "[begin PFCC]: usage: %f", MemoryTracker::usage());
  Funct::printMemUse(rank_, "[begin PFCC]");
#endif

  int i;

  int index;
  int numNotMatched = numLocalVertices;
  int maxLocWt = 0;
  int maxWt;
  int aveVertexWt = static_cast<int>(
      ceil(static_cast<double>(totalHypergraphWt) / totalVertices));

  int vPerProc = totalVertices / processors_;

  int hEdge;
  int vertex;

  double reducedBy;

  if (dispOption > 1) {
    for (i = 0; i < numLocalVertices; ++i) {
      if (vWeight[i] > maxLocWt)
        maxLocWt = vWeight[i];
    }

    MPI_Reduce(&maxLocWt, &maxWt, 1, MPI_INT, MPI_MAX, 0, comm);

    if (rank_ == 0) {
      out_stream << " [PHEDGE] " << maxVertexWt << " " << maxWt << " "
                 << aveVertexWt << " ";
      out_stream.flush();
    }
  }

  bit_field matchedVertices(totalVertices);
  matchedVertices.unset();

  dynamic_array<int> hEdges(numHedges);
  dynamic_array<int> hEdgeLens(numHedges);
  dynamic_array<int> unmatchedLocals;
  dynamic_array<int> unmatchedNonLocals;

  for (i = 0; i < numHedges; ++i) {
    hEdges[i] = i;
    hEdgeLens[i] = hEdgeOffset[i + 1] - hEdgeOffset[i];
  }

  /* sort hedges in increasing order of capacity_ */

  Funct::qsortByAnotherArray(0, numHedges - 1, hEdges.data(),
                             hEdgeLens.data(), INC);

  /*
  */

  /* now sort the hyperedges of same capacity_ in
     decreasing order of weight */

  int leftBound = 0;
  int rightBound;
  int length;

  for (; leftBound < numHedges;) {
    length = hEdgeWeight[leftBound];
    rightBound = leftBound + 1;

    for (; rightBound < numHedges && hEdgeLens[rightBound] == length;
         ++rightBound)
      ;
    /*
    if(rank_ == 0) {
      std::cout << "leftBound = " << leftBound
           << ", rightBound = " << rightBound << std::endl;
    }
    */
    Funct::qsortByAnotherArray(leftBound, rightBound - 1, hEdges.data(),
                               hEdgeWeight.data(), DEC);
    leftBound = rightBound;
  }

  /*
  if(rank_ == 0) {
    for (i=0;i<numHedges;++i)
      if(i % 10 == 0)
        std::cout << hEdgeLens[hEdges[i]] << " ";
    std::cout << std::endl;
  }
  MPI_Barrier(comm);
  */

  clusterIndex = 0;
  stopCoarsening = 0;

  for (index = 0; index < numHedges; ++index) {

    hEdge = hEdges[index];
    /*
    if(index % 1000 == 0 && rank_ == 0) {
      std::cout << "hEdge = " << hEdge << std::endl;
      std::cout << "hEdgeLen[" << hEdge << "] = " << hEdgeLens[hEdge] << std::endl;
      std::cout << "hEdgeOffset[" << hEdge+1 << "]-hEdgeOffset[" << hEdge << "] = "
    << hEdgeOffset[hEdge+1]-hEdgeOffset[hEdge] << std::endl;
    }
    */
    int numUnmatchedLocals = 0;
    int numUnmatchedNonLocals = 0;

    for (i = hEdgeOffset[hEdge]; i < hEdgeOffset[hEdge + 1]; ++i) {
      vertex = locPinList[i];
#ifdef DEBUG_COARSENER
      assert(vertex >= 0 && vertex < totalVertices);
#endif
      if (vertex >= minVertexIndex && vertex < maxVertexIndex) {
        if (matchVector[vertex - minVertexIndex] == -1) {
          unmatchedLocals.assign(numUnmatchedLocals++, vertex - minVertexIndex);
        }
      } else {
        if (matchedVertices(vertex) == 0) {
          unmatchedNonLocals.assign(numUnmatchedNonLocals++, vertex);
        }
      }
    }
    /*
    if(index % 1000 == 0 && rank_ == 0) {
      std::cout << "numUnmatchedLocals = " << numUnmatchedLocals << std::endl
           << ", numUnmatchedNonLocals = " << numUnmatchedNonLocals << std::endl;
    }
    */
    // if(numUnmatchedLocals+numUnmatchedNonLocals == hEdgeLens[hEdge]) {
    if (numUnmatchedLocals > 1 ||
        ((numUnmatchedLocals + numUnmatchedNonLocals) == hEdgeLens[hEdge])) {

      int totMatchWt = 0;

      for (i = 0; i < numUnmatchedLocals; ++i)
        totMatchWt += vWeight[unmatchedLocals[i]];

      if (numUnmatchedLocals > 1) {
        if (totMatchWt < maxVertexWt) {
          for (i = 0; i < numUnmatchedLocals; ++i) {
            vertex = unmatchedLocals[i];
            matchVector[vertex] = clusterIndex;
            clusterWeights.assign(clusterIndex, totMatchWt);
          }

          ++clusterIndex;
          numNotMatched -= numUnmatchedLocals;
          reducedBy = static_cast<double>(numLocalVertices) /
                      (numNotMatched + clusterIndex + table->size());

          // if(rank_ == 0)
          // std::cout << "[local] reducedBy = " << reducedBy << std::endl;

          if (reducedBy > reductionRatio)
            break;
        }
      } else {
        if (totMatchWt <
            (maxVertexWt - (aveVertexWt * numUnmatchedNonLocals))) {
          /*
          if(numUnmatchedNonLocals > 0) {
          */
          int nonLocalVert = unmatchedNonLocals[0];

          for (i = 0; i < numUnmatchedLocals; ++i) {
            vertex = unmatchedLocals[i];
            table->add_local(nonLocalVert, vertex + minVertexIndex,
                             vWeight[vertex],
                             std::min(nonLocalVert / vPerProc, processors_ - 1));
#ifdef DEBUG_COARSENER
            assert(std::min(nonLocalVert / vPerProc, processors_ - 1) != rank_);
#endif
            matchVector[vertex] = NON_LOCAL_MATCH + nonLocalVert;
          }

          // for (i=0;i<numUnmatchedNonLocals;++i) {
          matchedVertices.set(nonLocalVert);
          //}

          numNotMatched -= numUnmatchedLocals;
          reducedBy = static_cast<double>(numLocalVertices) /
                      (numNotMatched + clusterIndex + table->size());

          // if(rank_ == 0)
          //  std::cout << "[remote] reducedBy = " << reducedBy << std::endl;

          if (reducedBy > reductionRatio)
            break;
        }
      }
    }
  }

  // if(rank_ == 0)
  // std::cout << "reducedBy = " << reducedBy << std::endl
  // << ", index = " << index << std::endl;
  // MPI_Barrier(comm);

  for (index = 0; index < numLocalVertices; ++index) {
    if (matchVector[index] == -1) {
      matchVector[index] = clusterIndex;
      clusterWeights.assign(clusterIndex++, vWeight[index]);
    }
  }

  // ###
  // now need to prepare and send out the matching requests
  // ###

  for (i = 0; i < 2; ++i) {
    setRequestArrays(i);
      send_from_data_out(comm); // actually sending requests
    setReplyArrays(i, maxVertexWt);
      send_from_data_out(comm); // actually sending replies
    processReqReplies();
  }

  setClusterIndices(comm);

  if (static_cast<double>(totalVertices) / totalClusters <
      MIN_ALLOWED_REDUCTION_RATIO) {
    stopCoarsening = 1;
  }

  table->clear();

  // ###
  // now construct the coarse hypergraph using the matching vector
  // ###

  //  if(rank_ == 0)
  // std::cout << "about to contract hyperedges" << std::endl;
  // MPI_Barrier(comm);

  return (contractHyperedges(h, comm));
}

void Para2DModelCoarsener::setRequestArrays(int highToLow) {
  int numRequests = table->size();
  int nonLocVertex;
  int cluWt;

  int i;
  int procRank;

  ds::match_request_table::entry *entry;
  ds::match_request_table::entry **entryArray = table->get_entries();

  for (i = 0; i < processors_; ++i)
    send_lens_[i] = 0;

  for (i = 0; i < numRequests; ++i) {
    entry = entryArray[i];

#ifdef DEBUG_COARSENER
    assert(entry);
#endif

    nonLocVertex = entry->non_local_vertex();
    cluWt = entry->cluster_weight();
    procRank = entry->non_local_process();

#ifdef DEBUG_COARSENER
    assert(procRank < processors_);
#endif

    if ((cluWt >= 0) && ((highToLow && procRank < rank_) ||
                         (!highToLow && procRank > rank_))) {
      data_out_sets_[procRank]->assign(send_lens_[procRank]++, nonLocVertex);
      data_out_sets_[procRank]->assign(send_lens_[procRank]++, cluWt);
    }
  }
}

void Para2DModelCoarsener::setReplyArrays(int highToLow, int maxVWt) {
  int j;
  int i;
  int l;

  dynamic_array<int> visitOrder;

  for (i = 0; i < processors_; ++i)
    send_lens_[i] = 0;

  int startOffset = 0;
  int vLocReq;
  int reqCluWt;
  int matchIndex;
  int visitOrderLen;

  for (i = 0; i < processors_; ++i) {
#ifdef DEBUG_COARSENER
    assert(And(receive_lens_[i], 0x1) == 0);
#endif

    if (matchRequestVisitOrder == RANDOM_ORDER) {
      visitOrderLen = Shiftr(receive_lens_[i], 1);
      visitOrder.reserve(visitOrderLen);

      for (j = 0; j < visitOrderLen; ++j)
        visitOrder[j] = j;

      // ###
      // processing match requests in random order
      // ###

      Funct::randomPermutation(visitOrder.data(), visitOrderLen);

      for (l = 0; l < visitOrderLen; ++l) {

        j = Shiftl(visitOrder[l], 1);

        vLocReq = receive_array_[startOffset + j];
        reqCluWt = receive_array_[startOffset + j + 1];

        if (accept(vLocReq, reqCluWt, highToLow, maxVWt)) {
          // ###
          // cross-processor match accepted. inform vertices
          // of their cluster index and cluster weight
          // ###

          matchIndex = matchVector[vLocReq - minVertexIndex];
          data_out_sets_[i]->assign(send_lens_[i]++, vLocReq);
          data_out_sets_[i]->assign(send_lens_[i]++, matchIndex);
          data_out_sets_[i]->assign(send_lens_[i]++, clusterWeights[matchIndex]);
        } else {
          // ###
          // cross-processor match rejected, inform vertices
          // that match rejected
          // ###

          data_out_sets_[i]->assign(send_lens_[i]++, vLocReq);
          data_out_sets_[i]->assign(send_lens_[i]++, NO_MATCH);
        }
      }
    } else {
      // ###
      // processing match requests as they arrive
      // ###

      visitOrderLen = receive_lens_[i];

      for (l = 0; l < visitOrderLen;) {
        vLocReq = receive_array_[startOffset + (l++)];
        reqCluWt = receive_array_[startOffset + (l++)];

        if (accept(vLocReq, reqCluWt, highToLow, maxVWt)) {
          // ###
          // cross-processor match accepted. inform vertices
          // of their cluster index and cluster weight
          // ###

          matchIndex = matchVector[vLocReq - minVertexIndex];
          data_out_sets_[i]->assign(send_lens_[i]++, vLocReq);
          data_out_sets_[i]->assign(send_lens_[i]++, matchIndex);
          data_out_sets_[i]->assign(send_lens_[i]++, clusterWeights[matchIndex]);
        } else {
          // ###
          // cross-processor match rejected, inform vertices
          // that match rejected
          // ###

          data_out_sets_[i]->assign(send_lens_[i]++, vLocReq);
          data_out_sets_[i]->assign(send_lens_[i]++, NO_MATCH);
        }
      }
    }
    startOffset += receive_lens_[i];
  }
}

void Para2DModelCoarsener::processReqReplies() {
  int i;
  int j;
  int index;

  int startOffset = 0;
  int vNonLocReq;
  int cluWt;
  int matchIndex;
  int numLocals;
  int *locals;

  ds::match_request_table::entry *entry;

  for (i = 0; i < processors_; ++i) {
    j = 0;
    while (j < receive_lens_[i]) {
      vNonLocReq = receive_array_[startOffset + (j++)];
      matchIndex = receive_array_[startOffset + (j++)];

      if (matchIndex != NO_MATCH) {
        // ###
        // match successful - set the clusterIndex
        // ###

        cluWt = receive_array_[startOffset + (j++)];
        entry = table->get_entry(vNonLocReq);

#ifdef DEBUG_COARSENER
        assert(entry);
#endif

        entry->set_cluster_index(matchIndex);
        entry->set_cluster_weight(cluWt);
      } else {
        // ###
        // match not successful - match requesting
        // vertices into a cluster
        // ###

        entry = table->get_entry(vNonLocReq);
        locals = entry->local_vertices_array();
        numLocals = entry->number_local();
        entry->set_cluster_index(MATCHED_LOCALLY);

        for (index = 0; index < numLocals; ++index)
          matchVector[locals[index] - minVertexIndex] = clusterIndex;

        clusterWeights.assign(clusterIndex++, entry->cluster_weight());
      }
    }
    startOffset += receive_lens_[i];
  }
}

void Para2DModelCoarsener::setClusterIndices(MPI_Comm comm) {
  dynamic_array<int> numClusters(processors_);
  dynamic_array<int> startIndex(processors_);

  MPI_Allgather(&clusterIndex, 1, MPI_INT, numClusters.data(), 1, MPI_INT,
                comm);

  int index = 0;
  int i;

  ds::match_request_table::entry *entry;
  ds::match_request_table::entry **entryArray;

  int numLocals;
  int cluIndex;
  int numEntries = table->size();
  int *locals;

  i = 0;
  for (; index < processors_; ++index) {
    startIndex[index] = i;
    i += numClusters[index];
  }
  totalClusters = i;

  myMinCluIndex = startIndex[rank_];

  for (index = 0; index < numLocalVertices; ++index) {
#ifdef DEBUG_COARSENER
    assert(matchVector[index] != -1);
#endif
    if (matchVector[index] < NON_LOCAL_MATCH)
      matchVector[index] += myMinCluIndex;
  }

  // ###
  // now set the non-local matches' cluster indices
  // ###

  entryArray = table->get_entries();

  for (index = 0; index < numEntries; ++index) {
    entry = entryArray[index];

#ifdef DEBUG_COARSENER
    assert(entry);
#endif

    cluIndex = entry->cluster_index();

#ifdef DEBUG_COARSENER
    assert(cluIndex >= 0);
#endif

    if (cluIndex != MATCHED_LOCALLY) {
      numLocals = entry->number_local();
      locals = entry->local_vertices_array();

#ifdef DEBUG_COARSENER
      assert(locals);
#endif
      cluIndex += startIndex[entry->non_local_process()];

      // indicate - there is no reason now why a vertex may not
      // match non-locally with a vertex that was matched non-locally
      // with a vertex from another processor during a high-to-low
      // communication for example...otherwise the cluster weight
      // information is redundant?

      for (i = 0; i < numLocals; ++i)
        matchVector[locals[i] - minVertexIndex] = cluIndex;
    }
  }

#ifdef DEBUG_COARSENER
  for (index = 0; index < numLocalVertices; ++index)
    if (matchVector[index] < 0 || matchVector[index] >= totalVertices) {
      std::cout << "matchVector[" << index << "]  = " << matchVector[index] << std::endl;
      assert(0);
    }
#endif
}

int Para2DModelCoarsener::accept(int locVertex, int nonLocCluWt, int highToLow,
                                 int maxWt) {
  int locVertexIndex = locVertex - minVertexIndex;
  int matchValue = matchVector[locVertexIndex];

  if (matchValue < NON_LOCAL_MATCH) {
    if (clusterWeights[matchValue] + nonLocCluWt >= maxWt)
      return 0;
    else {
      // ###
      // the non-local requesting vertices will have the same
      // cluster index as locVertex
      // ###

      clusterWeights[matchValue] += nonLocCluWt;
      return 1;
    }
  } else {

    int nonLocReq = matchValue - NON_LOCAL_MATCH;
    int proc = nonLocReq / (totalVertices / processors_);

    if ((highToLow && proc < rank_) || (!highToLow && proc > rank_))
      return 0;

    // ###
    // here think about allowing more than one cross-processor match
    // ###

    if (table->cluster_index(nonLocReq) != -1)
      return 0;

    int cluWt = vWeight[locVertexIndex] + nonLocCluWt;

    if (cluWt >= maxWt)
      return 0;

    else {
      // ###
      // the non-local requesting vertices will have the
      // same cluster index as locVertex
      // remove locVertex from the request table
      // ###

      table->remove_local(nonLocReq, locVertex, vWeight[locVertexIndex]);
      matchVector[locVertexIndex] = clusterIndex;
      clusterWeights.assign(clusterIndex++, cluWt);

      return 1;
    }
  }
}

void Para2DModelCoarsener::permuteVerticesArray(int *verts, int nLocVerts) {
  int i;

  switch (vertexVisitOrder) {
  case INCREASING_ORDER:
    for (i = 0; i < nLocVerts; ++i) {
      verts[i] = i;
    }
    break;

  case DECREASING_ORDER:
    for (i = 0; i < nLocVerts; ++i) {
      verts[i] = nLocVerts - i - 1;
    }
    break;

  case RANDOM_ORDER:
    for (i = 0; i < nLocVerts; ++i) {
      verts[i] = i;
    }
    Funct::randomPermutation(verts, nLocVerts);
    break;

  case INCREASING_WEIGHT_ORDER:
    for (i = 0; i < nLocVerts; ++i) {
      verts[i] = i;
    }
    Funct::qsortByAnotherArray(0, nLocVerts - 1, verts, vWeight, INC);
    break;

  case DECREASING_WEIGHT_ORDER:
    for (i = 0; i < nLocVerts; ++i) {
      verts[i] = i;
    }
    Funct::qsortByAnotherArray(0, nLocVerts - 1, verts, vWeight, DEC);
    break;

  default:
    for (i = 0; i < nLocVerts; ++i) {
      verts[i] = i;
    }
    Funct::randomPermutation(verts, nLocVerts);
    break;
  }
}

void Para2DModelCoarsener::printVisitOrder(int variable) const {
  switch (variable) {
  case INCREASING_ORDER:
    out_stream << "inc-idx";
    break;

  case DECREASING_ORDER:
    out_stream << "dec-idx";
    break;

  case INCREASING_WEIGHT_ORDER:
    out_stream << "inc_wt";
    break;

  case DECREASING_WEIGHT_ORDER:
    out_stream << "dec-wt";
    break;

  default:
    out_stream << "rand";
    break;
  }
}

#endif
