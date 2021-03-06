/*jslint indent: 2, nomen: true, maxlen: 120, sloppy: true, vars: true, white: true, plusplus: true */
/*global require, ENABLE_STATISTICS */

////////////////////////////////////////////////////////////////////////////////
/// @brief JavaScript server functions
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
/// @author Dr. Frank Celler
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

// extend prototypes for internally defined classes
require("org/arangodb");

// -----------------------------------------------------------------------------
// --SECTION--                                                  global functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief global 'Buffer'
////////////////////////////////////////////////////////////////////////////////

var Buffer = require("buffer").Buffer;

// -----------------------------------------------------------------------------
// --SECTION--                                                        statistics
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief installs a period statistics handler
////////////////////////////////////////////////////////////////////////////////

(function () {
  if (! ENABLE_STATISTICS) {
    // statistics are turned off
    return;
  }

  var internal = require("internal");
  var interval = require('org/arangodb/statistics').STATISTICS_INTERVAL;
  var interval15 = require('org/arangodb/statistics').STATISTICS_HISTORY_INTERVAL;

  if (internal.threadNumber === 0 && typeof internal.registerTask === "function") {
    internal.registerTask({ 
      id: "statistics-collector", 
      name: "statistics-collector",
      offset: interval / 10, 
      period: interval, 
      command: "require('org/arangodb/statistics').historian();"
    });

    internal.registerTask({ 
      id: "statistics-average-collector", 
      name: "statistics-average-collector",
      offset: 2 * interval, 
      period: interval15, 
      command: "require('org/arangodb/statistics').historianAverage();"
    });

    internal.registerTask({ 
      id: "statistics-gc", 
      name: "statistics-gc",
      offset: Math.random() * interval15 / 2, 
      period: interval15 / 2, 
      command: "require('org/arangodb/statistics').garbageCollector();"
    });
  }
}());


// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @\\}\\)"
// End:
