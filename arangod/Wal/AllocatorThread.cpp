////////////////////////////////////////////////////////////////////////////////
/// @brief Write-ahead log storage allocator thread
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
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "AllocatorThread.h"
#include "BasicsC/logging.h"
#include "Basics/ConditionLocker.h"
#include "Wal/LogfileManager.h"

using namespace triagens::wal;

// -----------------------------------------------------------------------------
// --SECTION--                                             class AllocatorThread
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief wait interval for the allocator thread when idle
////////////////////////////////////////////////////////////////////////////////

const uint64_t AllocatorThread::Interval = 500000;

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief create the allocator thread
////////////////////////////////////////////////////////////////////////////////

AllocatorThread::AllocatorThread (LogfileManager* logfileManager) 
  : Thread("WalAllocator"),
    _logfileManager(logfileManager),
    _condition(),
    _requestedSize(0),
    _stop(0) {
  
  allowAsynchronousCancelation();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy the allocator thread
////////////////////////////////////////////////////////////////////////////////

AllocatorThread::~AllocatorThread () {
}

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief stops the allocator thread
////////////////////////////////////////////////////////////////////////////////

void AllocatorThread::stop () {
  if (_stop > 0) {
    return;
  }

  _stop = 1;
  _condition.signal();

  while (_stop != 2) {
    usleep(1000);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief signal the creation of a new logfile
////////////////////////////////////////////////////////////////////////////////

void AllocatorThread::signal (uint32_t size) {
  assert(size > 0);
  
  CONDITION_LOCKER(guard, _condition);

  if (_requestedSize == 0 || size > _requestedSize) {
    _requestedSize = size;
  }

  guard.signal();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new reserve logfile
////////////////////////////////////////////////////////////////////////////////

bool AllocatorThread::createReserveLogfile (uint32_t size) {
  int res = _logfileManager->createReserveLogfile(size);

  return (res == TRI_ERROR_NO_ERROR);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                    Thread methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief main loop
////////////////////////////////////////////////////////////////////////////////

void AllocatorThread::run () {
  while (_stop == 0) {
    uint32_t requestedSize = 0;

    {
      CONDITION_LOCKER(guard, _condition);
      requestedSize = _requestedSize;
      _requestedSize = 0;
    }

    if (requestedSize == 0 && ! _logfileManager->hasReserveLogfiles()) {
      if (createReserveLogfile(0)) {
        continue;
      }

      LOG_ERROR("unable to create new wal reserve logfile");
    }
    else if (requestedSize > 0 && _logfileManager->logfileCreationAllowed(requestedSize)) {
      if (createReserveLogfile(requestedSize)) {
        continue;
      }
      
      LOG_ERROR("unable to create new wal reserve logfile");
    }
    
    {  
      CONDITION_LOCKER(guard, _condition);
      guard.wait(Interval);
    }
  }

  _stop = 2;
}

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End: