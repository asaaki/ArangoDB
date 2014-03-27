////////////////////////////////////////////////////////////////////////////////
/// @brief prime numbers
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2013 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Jan Steemann
/// @author Copyright 2006-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "prime-numbers.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief pre-calculated prime numbers
////////////////////////////////////////////////////////////////////////////////

static const uint64_t Primes[251] = {
  7, 11, 13, 17, 19, 23, 29, 31,
  37, 41, 47, 53, 59, 67, 73, 79,
  89, 97, 107, 127, 137, 149, 163, 179,
  193, 211, 227, 251, 271, 293, 317, 347,
  373, 401, 431, 467, 503, 541, 587, 641,
  691, 751, 809, 877, 947, 1019, 1097, 1181,
  1277, 1381, 1487, 1601, 1733, 1867, 2011, 2179,
  2347, 2531, 2729, 2939, 3167, 3413, 3677, 3967,
  4273, 4603, 4957, 5347, 5779, 6229, 6709, 7229,
  7789, 8389, 9041, 9739, 10499, 11311, 12197, 13147,
  14159, 15259, 16433, 17707, 19069, 20543, 22123, 23827,
  25667, 27647, 29789, 32083, 34583, 37243, 40111, 43201,
  46549, 50129, 53987, 58147, 62627, 67447, 72643, 78233,
  84263, 90749, 97729, 105251, 113357, 122081, 131477, 141601,
  152501, 164231, 176887, 190507, 205171, 220973, 237971, 256279,
  275999, 297233, 320101, 344749, 371281, 399851, 430649, 463781,
  499459, 537883, 579259, 623839, 671831, 723529, 779189, 839131,
  903691, 973213, 1048123, 1128761, 1215623, 1309163, 1409869, 1518329,
  1635133, 1760917, 1896407, 2042297, 2199401, 2368589, 2550791, 2747021,
  2958331, 3185899, 3431009, 3694937, 3979163, 4285313, 4614959, 4969961,
  5352271, 5763991, 6207389, 6684907, 7199147, 7752929, 8349311, 8991599,
  9683263, 10428137, 11230309, 12094183, 13024507, 14026393, 15105359, 16267313,
  17518661, 18866291, 20317559, 21880459, 23563571, 25376179, 27328211, 29430391,
  31694281, 34132321, 36757921, 39585457, 42630499, 45909769, 49441289, 53244481,
  57340211, 61750999, 66501077, 71616547, 77125553, 83058289, 89447429, 96328003,
  103737857, 111717757, 120311453, 129566201, 139532831, 150266159, 161825107, 174273193,
  187678831, 202115701, 217663079, 234406397, 252437677, 271855963, 292767983, 315288607,
  339541597, 365660189, 393787907, 424079291, 456700789, 491831621, 529664827, 570408281,
  614285843, 661538611, 712426213, 767228233, 826245839, 889803241, 958249679, 1031961197,
  1111342867, 1196830801, 1288894709, 1388040461, 1494812807, 1609798417, 1733629067, 1866985157,
  2010599411, 2165260961, 2331819499, 2511190229, 2704358747, 2912386343, 3136416067, 3377678861,
  3637500323, 3917308049, 4218639443
};

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief return a prime number not lower than value
////////////////////////////////////////////////////////////////////////////////

uint64_t TRI_NextPrime (uint64_t value) {
  unsigned int i;

  for (i = 0; i < sizeof(Primes); ++i) {
    if (Primes[i] >= value) {
      return Primes[i];
    }
  }
  return value;
}

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End: