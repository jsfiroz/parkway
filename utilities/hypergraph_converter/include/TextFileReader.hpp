
#ifndef _TEXTFILE_READER_HPP
#define _TEXTFILE_READER_HPP

// ### TextFileReader.hpp ###
//
// Copyright (C) 2004, Aleksandar Trifunovic, Imperial College London
//
// HISTORY:
//
// 01/12/2004: Last Modified
//
// ###

#include <fstream>
#include <cstring>
#include <cstdio>
#include "data_structures/dynamic_array.hpp"

using parkway::data_structures::dynamic_array;

class TextFileReader {
 private:
  static int maxLength;
  static int maxPinsInChunk;

 protected:
  int length;
  dynamic_array<char> buffer;

 public:
  TextFileReader();
  ~TextFileReader();

  void getLine(std::ifstream &input_stream);

  static inline int getMaxLength() { return maxLength; }
  static inline int getMaxPinsInChunk() { return maxPinsInChunk; }
  static inline void setMaxLength(int max) { maxLength = max; }
  static inline void setMaxPinsInChunk(int max) {
    maxPinsInChunk = max;
  }
};

#endif
