////////////////////////////////////////////////////////////////////////////////
/// @brief infrastructure for query optimizer
///
/// @file arangod/Aql/Optimizer.cpp
///
/// DISCLAIMER
///
/// Copyright 2010-2014 triagens GmbH, Cologne, Germany
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
/// @author Max Neunhoeffer
/// @author Copyright 2014, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "Aql/Optimizer.h"
#include "Aql/OptimizerRules.h"

using namespace triagens::aql;

// -----------------------------------------------------------------------------
// --SECTION--                                               the optimizer class
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// @brief constructor, this will initialize the rules database
////////////////////////////////////////////////////////////////////////////////

Optimizer::Optimizer () {
  // List all the rules in the system here:
  registerRule (relaxRule, 1000);

  // Now sort them by pass:
  std::stable_sort(_rules.begin(), _rules.end());
}

////////////////////////////////////////////////////////////////////////////////
// @brief the actual optimization
////////////////////////////////////////////////////////////////////////////////

int Optimizer::createPlans (ExecutionPlan* plan) {
  // This vector holds the plans we have created in this pass:
  PlanList newPlans;
  // This vector holds the plans we have created in the previous pass:
  PlanList oldPlans(plan);

  bool keep;  // used as a return value for rules
  int res;

  // _plans contains the final result
  for (auto p : _plans) {
    delete p;
  }
  _plans.clear();

  for (int pass = 1; pass <= numberOfPasses; pass++) {
    for (auto r : _rules) {
      PlanList nextNewPlans;
      PlanList nextOldPlans;
      while (oldPlans.size() > 0) {
        auto p = oldPlans.pop_front();
        try {
          res = r.func(this, p, nextNewPlans, keep);
          if (keep) {
            nextOldPlans.push_back(p);
          }
        }
        catch (...) {
          delete p;
          throw;
        }
        if (res != TRI_ERROR_NO_ERROR) {
          return res;
        }
      }
      while (newPlans.size() > 0) {
        auto p = newPlans.pop_front();
        try {
          res = r.func(this, p, nextNewPlans, keep);
          if (keep) {
            nextNewPlans.push_back(p);
          }
        }
        catch (...) {
          delete p;
          throw;
        }
        if (res != TRI_ERROR_NO_ERROR) {
          return res;
        }
      }
      oldPlans.steal(nextOldPlans);
      newPlans.steal(nextNewPlans);
    }
    // Now move the surviving old plans to the result:
    oldPlans.appendTo(_plans);
    // Now move all the new plans to old:
    oldPlans.steal(newPlans);

    // A shortcut if nothing new was produced:
    if (oldPlans.size() == 0) {
      break;
    }

    // Stop if the result gets out of hand:
    if (_plans.size() + oldPlans.size() >= maxNumberOfPlans) {
      break;
    }
  }
  // Append the surviving plans to the result:
  oldPlans.appendTo(_plans);

  estimatePlans();
  sortPlans();

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief estimatePlans
////////////////////////////////////////////////////////////////////////////////

void Optimizer::estimatePlans () {
  for (auto p : _plans) {
    p->getCost();
    // this value is cached in the plan, so formally this step is
    // unnecessary, but for the sake of cleanliness...
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sortPlans
////////////////////////////////////////////////////////////////////////////////

bool sortByEstimate (ExecutionPlan* const& a, ExecutionPlan* const& b) {
  return a->getCost() < b->getCost();
}

void Optimizer::sortPlans () {
  std::sort(_plans.begin(), _plans.end(), sortByEstimate);
}

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:

