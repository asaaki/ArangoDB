////////////////////////////////////////////////////////////////////////////////
/// @brief skiplist index
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dr. Oreste Costa-Panaia
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "skiplistIndex.h"

#include "BasicsC/utf8-helper.h"
#include "ShapedJson/json-shaper.h"
#include "ShapedJson/shaped-json.h"
#include "VocBase/document-collection.h"
#include "VocBase/voc-shaper.h"

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Common private methods
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

// .............................................................................
// recall for all of the following comparison functions:
//
// left < right  return -1
// left > right  return  1
// left == right return  0
//
// furthermore:
//
// the following order is currently defined for placing an order on documents
// undef < null < boolean < number < strings < lists < hash arrays
// note: undefined will be treated as NULL pointer not NULL JSON OBJECT
// within each type class we have the following order
// boolean: false < true
// number: natural order
// strings: lexicographical
// lists: lexicographically and within each slot according to these rules.
// ...........................................................................

////////////////////////////////////////////////////////////////////////////////
/// @brief compares a key with an element, version with proper types
////////////////////////////////////////////////////////////////////////////////

static int CompareKeyElement (TRI_shaped_json_t const* left,
                              TRI_skiplist_index_element_t* right,
                              size_t rightPosition,
                              TRI_shaper_t* shaper) {
  int result;

  TRI_ASSERT(nullptr != left);
  TRI_ASSERT(nullptr != right);
  result = TRI_CompareShapeTypes(nullptr,
                                 nullptr,
                                 left,
                                 right->_document,
                                 &right->_subObjects[rightPosition],
                                 nullptr,
                                 shaper);

  // ...........................................................................
  // In the above function CompareShapeTypes we use strcmp which may
  // return an integer greater than 1 or less than -1. From this
  // function we only need to know whether we have equality (0), less
  // than (-1) or greater than (1)
  // ...........................................................................

  if (result < 0) {
    result = -1;
  }
  else if (result > 0) {
    result = 1;
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares elements, version with proper types
////////////////////////////////////////////////////////////////////////////////

static int CompareElementElement (TRI_skiplist_index_element_t* left,
                                  size_t leftPosition,
                                  TRI_skiplist_index_element_t* right,
                                  size_t rightPosition,
                                  TRI_shaper_t* shaper) {
  TRI_ASSERT(nullptr != left);
  TRI_ASSERT(nullptr != right);

  int result = TRI_CompareShapeTypes(left->_document,
                                     &left->_subObjects[leftPosition],
                                     nullptr,
                                     right->_document,
                                     &right->_subObjects[rightPosition],
                                     nullptr,
                                     shaper);

  // ...........................................................................
  // In the above function CompareShapeTypes we use strcmp which may
  // return an integer greater than 1 or less than -1. From this
  // function we only need to know whether we have equality (0), less
  // than (-1) or greater than (1)
  // ...........................................................................

  if (result < 0) {
    return -1;
  }
  else if (result > 0) {
    return 1;
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares two elements in a skip list, this is the generic callback
////////////////////////////////////////////////////////////////////////////////

static int CmpElmElm (void* sli,
                      void* left,
                      void* right,
                      TRI_cmp_type_e cmptype) {

  TRI_skiplist_index_element_t* leftElement = static_cast<TRI_skiplist_index_element_t*>(left);
  TRI_skiplist_index_element_t* rightElement = static_cast<TRI_skiplist_index_element_t*>(right);
  TRI_shaper_t* shaper;

  TRI_ASSERT(nullptr != left);
  TRI_ASSERT(nullptr != right);

  // ..........................................................................
  // The document could be the same -- so no further comparison is required.
  // ..........................................................................

  if (leftElement == rightElement ||
      leftElement->_document == rightElement->_document) {
    return 0;
  }

  SkiplistIndex* skiplistindex = static_cast<SkiplistIndex*>(sli);
  shaper = skiplistindex->_collection->getShaper();  // ONLY IN INDEX, PROTECTED by RUNTIME
  int compareResult;

  for (size_t j = 0;  j < skiplistindex->_numFields;  j++) {
    compareResult = CompareElementElement(leftElement,
                                          j,
                                          rightElement,
                                          j,
                                          shaper);

    if (compareResult != 0) {
      return compareResult;
    }
  }

  // ...........................................................................
  // This is where the difference between the preorder and the proper total
  // order comes into play. Here if the 'keys' are the same,
  // but the doc ptr is different (which it is since we are here), then
  // we return 0 if we use the preorder and look at the _key attribute
  // otherwise.
  // ...........................................................................

  if (TRI_CMP_PREORDER == cmptype) {
    return 0;
  }

  // We break this tie in the key comparison by looking at the key:
  compareResult = strcmp(TRI_EXTRACT_MARKER_KEY(leftElement->_document),    // ONLY IN INDEX, PROTECTED by RUNTIME
                         TRI_EXTRACT_MARKER_KEY(rightElement->_document));  // ONLY IN INDEX, PROTECTED by RUNTIME

  if (compareResult < 0) {
    return -1;
  }
  else if (compareResult > 0) {
    return 1;
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares a key with an element in a skip list, generic callback
////////////////////////////////////////////////////////////////////////////////

static int CmpKeyElm (void* sli,
                      void* left,
                      void* right) {
  TRI_skiplist_index_key_t* leftKey = static_cast<TRI_skiplist_index_key_t*>(left);
  TRI_skiplist_index_element_t* rightElement = static_cast<TRI_skiplist_index_element_t*>(right);
  TRI_shaper_t* shaper;

  TRI_ASSERT(nullptr != left);
  TRI_ASSERT(nullptr != right);

  SkiplistIndex* skiplistindex = static_cast<SkiplistIndex*>(sli);
  shaper = skiplistindex->_collection->getShaper();  // ONLY IN INDEX, PROTECTED by RUNTIME

  // Note that the key might contain fewer fields than there are indexed
  // attributes, therefore we only run the following loop to
  // leftKey->_numFields.
  for (size_t j = 0;  j < leftKey->_numFields;  j++) {
    int compareResult = CompareKeyElement(&leftKey->_fields[j], rightElement, j, shaper);

    if (compareResult != 0) {
      return compareResult;
    }
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees an element in the skiplist
////////////////////////////////////////////////////////////////////////////////

static void FreeElm (void* e) {
  TRI_skiplist_index_element_t* element = static_cast<TRI_skiplist_index_element_t*>(e);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, element->_subObjects);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, element);
}

static int CopyElement (SkiplistIndex* skiplistindex,
                        TRI_skiplist_index_element_t* leftElement,
                        TRI_skiplist_index_element_t* rightElement) {
  TRI_ASSERT(nullptr != leftElement && nullptr != rightElement);

  leftElement->_document   = rightElement->_document;
  leftElement->_subObjects = static_cast<TRI_shaped_sub_t*>(TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_sub_t) * skiplistindex->_numFields, false));

  if (leftElement->_subObjects == nullptr) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  memcpy(leftElement->_subObjects, rightElement->_subObjects,
         sizeof(TRI_shaped_sub_t) * skiplistindex->_numFields);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
// Some static helper functions:
// These are assigned to some callback hooks but do not seem to be used,
// which is good because otherwise the TRI_ASSERT(false) statements would
// bite!
////////////////////////////////////////////////////////////////////////////////

static int SkiplistIndex_queryMethodCall (void* theIndex,
                                          TRI_index_operator_t* indexOperator,
                                          TRI_index_challenge_t* challenge,
                                          void* data) {
  SkiplistIndex* slIndex = (SkiplistIndex*)(theIndex);
  if (slIndex == nullptr || indexOperator == nullptr) {
    return TRI_ERROR_INTERNAL;
  }
  TRI_ASSERT(false);
  return TRI_ERROR_NO_ERROR;
}

static TRI_index_iterator_t* SkiplistIndex_resultMethodCall (
                   void* theIndex,
                   TRI_index_operator_t* indexOperator,
                   void* data,
                   bool (*filter) (TRI_index_iterator_t*)) {
  SkiplistIndex* slIndex = (SkiplistIndex*)(theIndex);
  if (slIndex == nullptr || indexOperator == nullptr) {
    return nullptr;
  }
  TRI_ASSERT(false);
  return nullptr;
}

static int SkiplistIndex_freeMethodCall (void* theIndex,
                                         void* data) {
  SkiplistIndex* slIndex = (SkiplistIndex*)(theIndex);
  if (slIndex == nullptr) {
    return TRI_ERROR_INTERNAL;
  }
  TRI_ASSERT(false);
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Attempts to determine if there is a next document within an
/// interval - without advancing the iterator.
////////////////////////////////////////////////////////////////////////////////

static bool SkiplistHasNextIterationCallback(TRI_skiplist_iterator_t* iterator) {
  TRI_skiplist_iterator_interval_t* interval;
  void* leftNode;

  // ...........................................................................
  // Some simple checks.
  // ...........................................................................

  TRI_ASSERT(nullptr != iterator);

  if (nullptr == iterator->_cursor) {
    return false;
  }

  // ...........................................................................
  // if we have more intervals than the one we are currently working
  // on then of course we have a next doc
  // ...........................................................................
  if (iterator->_intervals._length - 1 > iterator->_currentInterval) {
    return true;
  }

  // ...........................................................................
  // Obtain the current interval -- in case we ever use more than one interval
  // ...........................................................................

  interval =  (TRI_skiplist_iterator_interval_t*)
        ( TRI_AtVector(&(iterator->_intervals), iterator->_currentInterval) );

  // ...........................................................................
  // Obtain the left end point we are currently at
  // ...........................................................................

  leftNode = TRI_SkipListNextNode(iterator->_cursor);
  // Note that leftNode can be NULL here!
  // ...........................................................................
  // If the left == right end point AND there are no more intervals then we have
  // no next.
  // ...........................................................................
  if (leftNode == interval->_rightEndPoint) {
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Jumps forwards by jumpSize and returns the document
////////////////////////////////////////////////////////////////////////////////

static TRI_skiplist_index_element_t* SkiplistIteration (
                               TRI_skiplist_iterator_t* iterator,
                               int64_t jumpSize) {
  TRI_skiplist_iterator_interval_t* interval;
  int64_t j;

  // ...........................................................................
  // Some simple checks.
  // ...........................................................................

  TRI_ASSERT(nullptr != iterator);

  if (nullptr == iterator->_cursor) {
    // In this case the iterator is exhausted or does not even have intervals.
    return nullptr;
  }

  TRI_ASSERT(jumpSize > 0);

  // ...........................................................................
  // Obtain the current interval we are at.
  // ...........................................................................

  interval = (TRI_skiplist_iterator_interval_t*)
       ( TRI_AtVector(&(iterator->_intervals), iterator->_currentInterval) );

  // ...........................................................................
  // use the current cursor and move jumpSize forward.
  // ...........................................................................
  for (j = 0; j < jumpSize; ++j) {
    while (true) {   // will be left by break
      iterator->_cursor = TRI_SkipListNextNode(iterator->_cursor);
      if (iterator->_cursor != interval->_rightEndPoint) {
        // Note that _cursor can be NULL here!
        break;   // we found a next one
      }
      if (iterator->_currentInterval == (iterator->_intervals._length - 1)) {
        iterator->_cursor = nullptr;  // exhausted
        return nullptr;
      }
      ++iterator->_currentInterval;
      interval = (TRI_skiplist_iterator_interval_t*)
          ( TRI_AtVector(&(iterator->_intervals), iterator->_currentInterval) );
      iterator->_cursor = interval->_leftEndPoint;
    }
  }

  return (TRI_skiplist_index_element_t*) (iterator->_cursor->doc);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief default callback for jumping forward by 1
////////////////////////////////////////////////////////////////////////////////

static TRI_skiplist_index_element_t* SkiplistNextIterationCallback(
                        TRI_skiplist_iterator_t* iterator) {
  return SkiplistIteration(iterator, 1);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief default callback for jumping forward by jumpSize docs
////////////////////////////////////////////////////////////////////////////////

static TRI_skiplist_index_element_t* SkiplistNextsIterationCallback(
                                            TRI_skiplist_iterator_t* iterator,
                                            int64_t jumpSize) {
  return SkiplistIteration(iterator,jumpSize);
}

// -----------------------------------------------------------------------------
// --SECTION--                           skiplistIndex     common public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief Assigns a static function call to a function pointer used by
/// the Query Engine, seems not to be used at this stage...
////////////////////////////////////////////////////////////////////////////////

int SkiplistIndex_assignMethod (void* methodHandle,
                                TRI_index_method_assignment_type_e methodType) {
  switch (methodType) {

    case TRI_INDEX_METHOD_ASSIGNMENT_FREE : {
      TRI_index_query_free_method_call_t* call =
             (TRI_index_query_free_method_call_t*)(methodHandle);
      *call = SkiplistIndex_freeMethodCall;
      break;
    }

    case TRI_INDEX_METHOD_ASSIGNMENT_QUERY : {
      TRI_index_query_method_call_t* call =
             (TRI_index_query_method_call_t*)(methodHandle);
      *call = SkiplistIndex_queryMethodCall;
      break;
    }

    case TRI_INDEX_METHOD_ASSIGNMENT_RESULT : {
      TRI_index_query_result_method_call_t* call =
             (TRI_index_query_result_method_call_t*)(methodHandle);
      *call = SkiplistIndex_resultMethodCall;
      break;
    }

    default : {
      TRI_ASSERT(false);
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Free a skiplist iterator
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeSkiplistIterator (TRI_skiplist_iterator_t* iterator) {
  TRI_ASSERT(nullptr != iterator);

  TRI_DestroyVector(&iterator->_intervals);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, iterator);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a skip list index , but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void SkiplistIndex_destroy (SkiplistIndex* slIndex) {
  if (slIndex == nullptr) {
    return;
  }

  TRI_FreeSkipList(slIndex->skiplist);
  slIndex->skiplist = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a skip list index and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void SkiplistIndex_free (SkiplistIndex* slIndex) {
  if (slIndex == nullptr) {
    return;
  }
  SkiplistIndex_destroy(slIndex);
  TRI_Free(TRI_CORE_MEM_ZONE, slIndex);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Skiplist indices
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Public Methods Skiplist Indices
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new skiplist index
////////////////////////////////////////////////////////////////////////////////

SkiplistIndex* SkiplistIndex_new (TRI_document_collection_t* document,
                                  size_t numFields,
                                  bool unique,
                                  bool sparse) {
  SkiplistIndex* skiplistIndex = static_cast<SkiplistIndex*>(TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(SkiplistIndex), true));

  if (skiplistIndex == nullptr) {
    return nullptr;
  }

  skiplistIndex->_collection = document;
  skiplistIndex->_numFields = numFields;
  skiplistIndex->unique = unique;
  skiplistIndex->sparse = sparse;
  skiplistIndex->skiplist = TRI_InitSkipList(CmpElmElm, CmpKeyElm, skiplistIndex,
                                             FreeElm, unique);
  if (skiplistIndex->skiplist == nullptr) {
    TRI_Free(TRI_CORE_MEM_ZONE, skiplistIndex);
    return nullptr;
  }

  return skiplistIndex;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Locates one or more ranges within the skiplist and returns iterator
////////////////////////////////////////////////////////////////////////////////

// .............................................................................
// Tests whether the LeftEndPoint is < than RightEndPoint (-1)
// Tests whether the LeftEndPoint is == to RightEndPoint (0)    [empty]
// Tests whether the LeftEndPoint is > than RightEndPoint (1)   [undefined]
// .............................................................................

static bool skiplistIndex_findHelperIntervalValid(
                        SkiplistIndex* skiplistIndex,
                        TRI_skiplist_iterator_interval_t* interval) {
  int compareResult;
  TRI_skiplist_node_t* lNode;
  TRI_skiplist_node_t* rNode;

  lNode = interval->_leftEndPoint;

  if (lNode == nullptr) {
    return false;
  }
  // Note that the right end point can be NULL to indicate the end of the index.

  rNode = interval->_rightEndPoint;

  if (lNode == rNode) {
    return false;
  }

  if (TRI_SkipListNextNode(lNode) == rNode) {
    // Interval empty, nothing to do with it.
    return false;
  }

  if (nullptr != rNode && TRI_SkipListNextNode(rNode) == lNode) {
    // Interval empty, nothing to do with it.
    return false;
  }

  if (TRI_SkipListGetNrUsed(skiplistIndex->skiplist) == 0) {
    return false;
  }

  if ( lNode == TRI_SkipListStartNode(skiplistIndex->skiplist) ||
       nullptr == rNode ) {
    // The index is not empty, the nodes are not neighbours, one of them
    // is at the boundary, so the interval is valid and not empty.
    return true;
  }

  compareResult = CmpElmElm( skiplistIndex,
                             lNode->doc, rNode->doc, TRI_CMP_TOTORDER );
  return (compareResult == -1);
  // Since we know that the nodes are not neighbours, we can guarantee
  // at least one document in the interval.
}

static bool skiplistIndex_findHelperIntervalIntersectionValid (
                    SkiplistIndex* skiplistIndex,
                    TRI_skiplist_iterator_interval_t* lInterval,
                    TRI_skiplist_iterator_interval_t* rInterval,
                    TRI_skiplist_iterator_interval_t* interval) {
  TRI_skiplist_node_t* lNode;
  TRI_skiplist_node_t* rNode;

  lNode = lInterval->_leftEndPoint;
  rNode = rInterval->_leftEndPoint;

  if (nullptr == lNode || nullptr == rNode) {
    // At least one left boundary is the end, intersection is empty.
    return false;
  }

  int compareResult;
  // Now find the larger of the two start nodes:
  if (lNode == TRI_SkipListStartNode(skiplistIndex->skiplist)) {
    // We take rNode, even if it is the start node as well.
    compareResult = -1;
  }
  else if (rNode == TRI_SkipListStartNode(skiplistIndex->skiplist)) {
    // We take lNode
    compareResult = 1;
  }
  else {
    compareResult = CmpElmElm(skiplistIndex, lNode->doc, rNode->doc,
                              TRI_CMP_TOTORDER);
  }

  if (compareResult < 1) {
    interval->_leftEndPoint = rNode;
  }
  else {
    interval->_leftEndPoint = lNode;
  }

  lNode = lInterval->_rightEndPoint;
  rNode = rInterval->_rightEndPoint;

  // Now find the smaller of the two end nodes:
  if (nullptr == lNode) {
    // We take rNode, even is this also the end node.
    compareResult = 1;
  }
  else if (nullptr == rNode) {
    // We take lNode.
    compareResult = -1;
  }
  else {
    compareResult = CmpElmElm(skiplistIndex, lNode->doc, rNode->doc,
                              TRI_CMP_TOTORDER);
  }

  if (compareResult < 1) {
    interval->_rightEndPoint = lNode;
  }
  else {
    interval->_rightEndPoint = rNode;
  }

  return skiplistIndex_findHelperIntervalValid(skiplistIndex, interval);
}

static void SkiplistIndex_findHelper (SkiplistIndex* skiplistIndex,
                                      TRI_vector_t* shapeList,
                                      TRI_index_operator_t* indexOperator,
                                      TRI_vector_t* resultIntervalList) {
  TRI_skiplist_index_key_t          values;
  TRI_vector_t                      leftResult;
  TRI_vector_t                      rightResult;
  TRI_relation_index_operator_t*    relationOperator;
  TRI_logical_index_operator_t*     logicalOperator;
  TRI_skiplist_iterator_interval_t  interval;
  TRI_skiplist_node_t*              temp;

  TRI_InitVector(&(leftResult), TRI_UNKNOWN_MEM_ZONE,
                 sizeof(TRI_skiplist_iterator_interval_t));
  TRI_InitVector(&(rightResult), TRI_UNKNOWN_MEM_ZONE,
                 sizeof(TRI_skiplist_iterator_interval_t));

  relationOperator  = (TRI_relation_index_operator_t*)(indexOperator);
  logicalOperator   = (TRI_logical_index_operator_t*)(indexOperator);

  switch (indexOperator->_type) {
    case TRI_EQ_INDEX_OPERATOR:
    case TRI_LE_INDEX_OPERATOR:
    case TRI_LT_INDEX_OPERATOR:
    case TRI_GE_INDEX_OPERATOR:
    case TRI_GT_INDEX_OPERATOR:

      values._fields     = relationOperator->_fields;
      values._numFields  = relationOperator->_numFields;

      break;   // this is to silence a compiler warning
    default: {
      // must not access relationOperator->xxx if the operator is not a
      // relational one otherwise we'll get invalid reads and the prog
      // might crash
    }
  }

  switch (indexOperator->_type) {

    /*
    case TRI_SL_OR_OPERATOR: {
      SkiplistIndex_findHelper(skiplistIndex,shapeList,logicalOperator->_left,
                               &leftResult);
      SkiplistIndex_findHelper(skiplistIndex,shapeList,logicalOperator->_right,
                               &leftResult);
      i = 0;
      while (i < leftResult._length - 1) {
        tempLeftInterval  =  (TRI_skiplist_iterator_interval_t*)
                             (TRI_AtVector(&leftResult, i));
        tempRightInterval =  (TRI_skiplist_iterator_interval_t*)
                             (TRI_AtVector(&leftResult, i + 1));
        // if intervals intersect, optimise and start again
      }
      TRI_ASSERT(0);
    }
    */

    case TRI_AND_INDEX_OPERATOR: {
      SkiplistIndex_findHelper(skiplistIndex,shapeList,
                               logicalOperator->_left,&leftResult);
      SkiplistIndex_findHelper(skiplistIndex,shapeList,
                               logicalOperator->_right,&rightResult);

      for (size_t i = 0; i < leftResult._length; ++i) {
        for (size_t j = 0; j < rightResult._length; ++j) {
          TRI_skiplist_iterator_interval_t* tempLeftInterval;
          TRI_skiplist_iterator_interval_t* tempRightInterval;

          tempLeftInterval  =  (TRI_skiplist_iterator_interval_t*)
                               (TRI_AtVector(&leftResult, i));
          tempRightInterval =  (TRI_skiplist_iterator_interval_t*)
                               (TRI_AtVector(&rightResult, j));

          if (skiplistIndex_findHelperIntervalIntersectionValid(
                            skiplistIndex,
                            tempLeftInterval,
                            tempRightInterval,
                            &interval)) {
            TRI_PushBackVector(resultIntervalList, &interval);
          }
        }
      }
      TRI_DestroyVector(&leftResult);
      TRI_DestroyVector(&rightResult);
      return;
    }


    case TRI_EQ_INDEX_OPERATOR: {
      temp = TRI_SkipListLeftKeyLookup(skiplistIndex->skiplist, &values);
      if (nullptr != temp) {
        interval._leftEndPoint = temp;
        if (skiplistIndex->unique) {
          // At most one hit:
          temp = TRI_SkipListNextNode(temp);
          if (nullptr != temp) {
            if (0 == CmpKeyElm(skiplistIndex, &values, temp->doc)) {
              interval._rightEndPoint = TRI_SkipListNextNode(temp);
              if (skiplistIndex_findHelperIntervalValid(skiplistIndex,
                                                        &interval)) {
                TRI_PushBackVector(resultIntervalList, &interval);
              }
            }
          }
        }
        else {
          temp = TRI_SkipListRightKeyLookup(skiplistIndex->skiplist, &values);
          interval._rightEndPoint = TRI_SkipListNextNode(temp);
          if (skiplistIndex_findHelperIntervalValid(skiplistIndex,
                                                    &interval)) {
            TRI_PushBackVector(resultIntervalList, &interval);
          }
        }
      }
      return;
    }


    case TRI_LE_INDEX_OPERATOR: {
      interval._leftEndPoint  = TRI_SkipListStartNode(skiplistIndex->skiplist);
      temp = TRI_SkipListRightKeyLookup(skiplistIndex->skiplist, &values);
      interval._rightEndPoint = TRI_SkipListNextNode(temp);

      if (skiplistIndex_findHelperIntervalValid(skiplistIndex,&interval)) {
        TRI_PushBackVector(resultIntervalList, &interval);
      }

      return;
    }


    case TRI_LT_INDEX_OPERATOR: {
      interval._leftEndPoint  = TRI_SkipListStartNode(skiplistIndex->skiplist);
      temp = TRI_SkipListLeftKeyLookup(skiplistIndex->skiplist, &values);
      interval._rightEndPoint = TRI_SkipListNextNode(temp);

      if (skiplistIndex_findHelperIntervalValid(skiplistIndex,&interval)) {
        TRI_PushBackVector(resultIntervalList, &interval);
      }

      return;
    }


    case TRI_GE_INDEX_OPERATOR: {
      temp = TRI_SkipListLeftKeyLookup(skiplistIndex->skiplist, &values);
      interval._leftEndPoint = temp;
      interval._rightEndPoint = nullptr;

      if (skiplistIndex_findHelperIntervalValid(skiplistIndex,&interval)) {
        TRI_PushBackVector(resultIntervalList, &interval);
      }

      return;
    }


    case TRI_GT_INDEX_OPERATOR: {
      temp = TRI_SkipListRightKeyLookup(skiplistIndex->skiplist, &values);
      interval._leftEndPoint = temp;
      interval._rightEndPoint = nullptr;

      if (skiplistIndex_findHelperIntervalValid(skiplistIndex,&interval)) {
        TRI_PushBackVector(resultIntervalList, &interval);
      }

      return;
    }

    default: {
      TRI_ASSERT(false);
    }

  } // end of switch statement
}

TRI_skiplist_iterator_t* SkiplistIndex_find (
                            SkiplistIndex* skiplistIndex,
                            TRI_vector_t* shapeList,
                            TRI_index_operator_t* indexOperator) {
  TRI_skiplist_iterator_t* results = static_cast<TRI_skiplist_iterator_t*>(TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_skiplist_iterator_t), true));

  if (results == nullptr) {
    return nullptr; // calling procedure needs to care when the iterator is null
  }
  results->_index = skiplistIndex;
  TRI_InitVector(&(results->_intervals), TRI_UNKNOWN_MEM_ZONE,
                 sizeof(TRI_skiplist_iterator_interval_t));
  results->_currentInterval = 0;
  results->_cursor          = nullptr;
  results->_hasNext         = SkiplistHasNextIterationCallback;
  results->_next            = SkiplistNextIterationCallback;
  results->_nexts           = SkiplistNextsIterationCallback;

  SkiplistIndex_findHelper(skiplistIndex, shapeList, indexOperator,
                           &(results->_intervals));

  // Finally initialise _cursor if the result is not empty:
  if (0 < TRI_LengthVector(&(results->_intervals))) {
    TRI_skiplist_iterator_interval_t* tmp = static_cast<TRI_skiplist_iterator_interval_t*>(TRI_AtVector(&(results->_intervals), 0));
    results->_cursor = tmp->_leftEndPoint;
  }

  return results;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a data element into a unique skip list
////////////////////////////////////////////////////////////////////////////////

int SkiplistIndex_insert (SkiplistIndex* skiplistIndex,
                          TRI_skiplist_index_element_t* element) {
  TRI_skiplist_index_element_t* copy = static_cast<TRI_skiplist_index_element_t*>(TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_skiplist_index_element_t), false));

  if (nullptr == copy) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  int res = CopyElement(skiplistIndex, copy, element);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, copy);
    return res;
  }

  res = TRI_SkipListInsert(skiplistIndex->skiplist, copy);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, copy->_subObjects);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, copy);
    return res;
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes an entry from the skip list
////////////////////////////////////////////////////////////////////////////////

int SkiplistIndex_remove (SkiplistIndex* skiplistIndex,
                          TRI_skiplist_index_element_t* element) {
  int result;

  result = TRI_SkipListRemove(skiplistIndex->skiplist, element);

  if (result == TRI_ERROR_ARANGO_DOCUMENT_NOT_FOUND) {
    // This is for the case of a rollback in an aborted transaction.
    // We silently ignore the fact that the document was not there.
    // This could also be useful for the case of a sparse index.
    return TRI_ERROR_NO_ERROR;
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of elements in the skip list index
////////////////////////////////////////////////////////////////////////////////

uint64_t SkiplistIndex_getNrUsed (SkiplistIndex* skiplistIndex) {
  return TRI_SkipListGetNrUsed(skiplistIndex->skiplist);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the memory used by the index
////////////////////////////////////////////////////////////////////////////////

size_t SkiplistIndex_memoryUsage (SkiplistIndex const* skiplistIndex) {
  return sizeof(SkiplistIndex) + TRI_SkipListMemoryUsage(skiplistIndex->skiplist);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
