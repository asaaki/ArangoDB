////////////////////////////////////////////////////////////////////////////////
/// @brief JSUnity wrapper
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2012 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var internal = require("internal");
var fs = require("fs");
var console = require("console");

var TOTAL = 0;
var PASSED = 0;
var FAILED = 0;
var DURATION = 0;

var jsUnity = require("./jsunity/jsunity").jsUnity;

jsUnity.results.begin = function (total, suiteName) {
  print("Running " + (suiteName || "unnamed test suite"));
  print(" " + total + " test(s) found");
  print();
};

jsUnity.results.pass = function (index, testName) {
  print(internal.COLORS.COLOR_GREEN + " [PASSED] " + testName + internal.COLORS.COLOR_RESET);
};

jsUnity.results.fail = function (index, testName, message) {
  print(internal.COLORS.COLOR_RED + " [FAILED] " + testName + ": " + message + internal.COLORS.COLOR_RESET);
};

jsUnity.results.end = function (passed, failed, duration) {
  print(" " + passed + " test(s) passed");
  print(" " + ((failed > 0) ? internal.COLORS.COLOR_RED : internal.COLORS.COLOR_RESET) + failed + " test(s) failed" + internal.COLORS.COLOR_RESET);
  print(" " + duration + " millisecond(s) elapsed");
  print();
};

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup JSUnity
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief runs a test with context
////////////////////////////////////////////////////////////////////////////////

function Run (tests) {
  var suite = jsUnity.compile(tests);
  var definition = tests();

  var tests = [];
  var setUp = undefined;
  var tearDown = undefined;

  if (definition.hasOwnProperty("setUp")) {
    setUp = definition.setUp;
  }

  if (definition.hasOwnProperty("tearDown")) {
    tearDown = definition.tearDown;
  }
  
  var scope = {};
  scope.setUp = setUp;
  scope.tearDown = tearDown;

  for (var key in definition) {
    if (key.indexOf("test") == 0) {
      var test = { name : key, fn : definition[key] };

      tests.push(test);
    }
    else if (key != "tearDown" && key != "setUp") {
      console.error("unknown function: %s", key);
    }
  }

  suite = new jsUnity.TestSuite(suite.suiteName, scope);

  suite.tests = tests;
  suite.setUp = setUp;
  suite.tearDown = tearDown;
  
  var result = jsUnity.run(suite);

  TOTAL += result.total;
  PASSED += result.passed;
  FAILED += result.failed;
  DURATION += result.duration;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief done with all tests
////////////////////////////////////////////////////////////////////////////////

function Done () {
//  console.log("%d total, %d passed, %d failed, %d ms", TOTAL, PASSED, FAILED, DURATION);
  internal.printf("%d total, %d passed, %d failed, %d ms", TOTAL, PASSED, FAILED, DURATION);
  print();

  var ok = FAILED == 0;

  TOTAL = 0;
  PASSED = 0;
  FAILED = 0;
  DURATION = 0;

  return ok;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief runs a JSUnity test file
////////////////////////////////////////////////////////////////////////////////

function RunTest (path) {
  var content;
  var f;

  content = fs.read(path);

  content = "(function(){require('jsunity').jsUnity.attachAssertions();" + content + "})";
  f = internal.executeScript(content, undefined, path);
  
  if (f == undefined) {
    throw "cannot create context function";
  }

  return f();
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    MODULE EXPORTS
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup JSUnity
/// @{
////////////////////////////////////////////////////////////////////////////////

exports.jsUnity = jsUnity;
exports.run = Run;
exports.done = Done;
exports.runTest = RunTest;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\)"
// End:
