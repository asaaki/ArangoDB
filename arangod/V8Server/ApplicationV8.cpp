////////////////////////////////////////////////////////////////////////////////
/// @brief V8 engine configuration
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
/// @author Dr. Frank Celler
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2011-2014, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "ApplicationV8.h"

#include "Actions/actions.h"
#include "ApplicationServer/ApplicationServer.h"
#include "Basics/ConditionLocker.h"
#include "Basics/FileUtils.h"
#include "Basics/Mutex.h"
#include "Basics/MutexLocker.h"
#include "Basics/ReadLocker.h"
#include "Basics/StringUtils.h"
#include "Basics/Thread.h"
#include "Basics/WriteLocker.h"
#include "BasicsC/logging.h"
#include "BasicsC/tri-strings.h"
#include "Rest/HttpRequest.h"
#include "Scheduler/ApplicationScheduler.h"
#include "Scheduler/Scheduler.h"
#include "V8/v8-buffer.h"
#include "V8/v8-conv.h"
#include "V8/v8-shell.h"
#include "V8/v8-utils.h"
#include "V8Server/v8-actions.h"
#include "V8Server/v8-query.h"
#include "V8Server/v8-vocbase.h"
#include "VocBase/server.h"
#include "Cluster/ServerState.h"
#include "Cluster/v8-cluster.h"

using namespace triagens;
using namespace triagens::basics;
using namespace triagens::arango;
using namespace triagens::rest;
using namespace std;

// -----------------------------------------------------------------------------
// --SECTION--                                        class GlobalContextMethods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise code for pre-defined actions
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief reload the routing cache
////////////////////////////////////////////////////////////////////////////////

std::string const GlobalContextMethods::CodeReloadRouting    = "require(\"org/arangodb/actions\").reloadRouting()";

////////////////////////////////////////////////////////////////////////////////
/// @brief flush the modules cache
////////////////////////////////////////////////////////////////////////////////

std::string const GlobalContextMethods::CodeFlushModuleCache = "require(\"internal\").flushModuleCache()";

////////////////////////////////////////////////////////////////////////////////
/// @brief reload AQL functions
////////////////////////////////////////////////////////////////////////////////

std::string const GlobalContextMethods::CodeReloadAql        = "try { require(\"org/arangodb/ahuacatl\").reload(); } catch (err) { }";

////////////////////////////////////////////////////////////////////////////////
/// @brief we'll store deprecated config option values in here
////////////////////////////////////////////////////////////////////////////////

static std::string DeprecatedPath;

// -----------------------------------------------------------------------------
// --SECTION--                                                  class V8GcThread
// -----------------------------------------------------------------------------

namespace {

////////////////////////////////////////////////////////////////////////////////
/// @brief garbage collector
////////////////////////////////////////////////////////////////////////////////

  class V8GcThread : public Thread {
    public:
      V8GcThread (ApplicationV8* applicationV8)
        : Thread("v8-gc"),
          _applicationV8(applicationV8),
          _lock(),
          _lastGcStamp(TRI_microtime()) {
      }

    public:

////////////////////////////////////////////////////////////////////////////////
/// @brief collect garbage in an endless loop (main functon of GC thread)
////////////////////////////////////////////////////////////////////////////////

      void run () {
        _applicationV8->collectGarbage();
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief get the timestamp of the last GC
////////////////////////////////////////////////////////////////////////////////

      double getLastGcStamp () {
        READ_LOCKER(_lock);
        return _lastGcStamp;
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief set the global GC timestamp
////////////////////////////////////////////////////////////////////////////////

      void updateGcStamp (double value) {
        WRITE_LOCKER(_lock);
        _lastGcStamp = value;
      }

    private:
      ApplicationV8* _applicationV8;
      ReadWriteLock _lock;
      double _lastGcStamp;
  };
}

// -----------------------------------------------------------------------------
// --SECTION--                                                   class V8Context
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a global method
////////////////////////////////////////////////////////////////////////////////

bool ApplicationV8::V8Context::addGlobalContextMethod (string const& method) {
  GlobalContextMethods::MethodType type = GlobalContextMethods::getType(method);

  if (type == GlobalContextMethods::TYPE_UNKNOWN) {
    return false;
  }

  MUTEX_LOCKER(_globalMethodsLock);

  for (size_t i = 0; i < _globalMethods.size(); ++i) {
    if (_globalMethods[i] == type) {
      // action is already registered. no need to register it again
      return true;
    }
  }

  // insert action into vector
  _globalMethods.push_back(type);
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes all global methods
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::V8Context::handleGlobalContextMethods () {
  v8::HandleScope scope;

  vector<GlobalContextMethods::MethodType> copy;

  {
    // we need to copy the vector of functions so we do not need to hold the
    // lock while we execute them
    // this avoids potential deadlocks when one of the executed functions itself
    // registers a context method
    MUTEX_LOCKER(_globalMethodsLock);
    copy = _globalMethods;
    _globalMethods.clear();
  }

  for (vector<GlobalContextMethods::MethodType>::const_iterator i = copy.begin();  i != copy.end();  ++i) {
    GlobalContextMethods::MethodType const type = *i;
    string const func = GlobalContextMethods::getCode(type);

    LOG_DEBUG("executing global context methods '%s' for context %d", func.c_str(), (int) _id);

    TRI_ExecuteJavaScriptString(_context,
                                v8::String::New(func.c_str(), (int) func.size()),
                                v8::String::New("global context method"),
                                false);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the cancelation cleanup
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::V8Context::handleCancelationCleanup () {
  v8::HandleScope scope;

  LOG_DEBUG("executing cancelation cleanup context %d", (int) _id);

  TRI_ExecuteJavaScriptString(_context,
                              v8::String::New("require('internal').cleanupCancelation();"),
                              v8::String::New("context cleanup method"),
                              false);
}

// -----------------------------------------------------------------------------
// --SECTION--                                               class ApplicationV8
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

ApplicationV8::ApplicationV8 (TRI_server_t* server,
                              ApplicationScheduler* scheduler,
                              ApplicationDispatcher* dispatcher)
  : ApplicationFeature("V8"),
    _server(server),
    _startupPath(),
    _modulesPath(),
    _actionPath(),
    _appPath(),
    _devAppPath(),
    _useActions(true),
    _developmentMode(false),
    _frontendDevelopmentMode(false),
    _gcInterval(1000),
    _gcFrequency(10.0),
    _v8Options(""),
    _startupLoader(),
    _actionLoader(),
    _vocbase(0),
    _nrInstances(0),
    _contexts(0),
    _contextCondition(),
    _freeContexts(),
    _dirtyContexts(),
    _busyContexts(),
    _stopping(0),
    _gcThread(nullptr),
    _scheduler(scheduler),
    _dispatcher(dispatcher),
    _definedBooleans(),
    _startupFile("server/server.js"),
    _gcFinished(false) {

  TRI_ASSERT(_server != nullptr);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destructor
////////////////////////////////////////////////////////////////////////////////

ApplicationV8::~ApplicationV8 () {
}

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief sets the concurrency
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::setConcurrency (size_t n) {
  _nrInstances = n;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets the database
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::setVocbase (TRI_vocbase_t* vocbase) {
  _vocbase = vocbase;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enters a context
////////////////////////////////////////////////////////////////////////////////

ApplicationV8::V8Context* ApplicationV8::enterContext (TRI_vocbase_s* vocbase,
                                                       triagens::rest::HttpRequest* request,
                                                       bool initialise,
                                                       bool allowUseDatabase) {
  CONDITION_LOCKER(guard, _contextCondition);

  while (_freeContexts.empty() && ! _stopping) {
    LOG_DEBUG("waiting for unused V8 context");
    guard.wait();
  }

  // in case we are in the shutdown phase, do not enter a context!
  // the context might have been deleted by the shutdown
  if (_stopping) {
    return 0;
  }

  LOG_TRACE("found unused V8 context");

  V8Context* context = _freeContexts.back();

  TRI_ASSERT(context != nullptr);
  TRI_ASSERT(context->_isolate != nullptr);

  _freeContexts.pop_back();
  _busyContexts.insert(context);

  context->_locker = new v8::Locker(context->_isolate);
  context->_isolate->Enter();
  context->_context->Enter();

  // set the current database
  v8::HandleScope scope;
  TRI_v8_global_t* v8g = static_cast<TRI_v8_global_t*>(context->_isolate->GetData());
  v8g->_vocbase = vocbase;
  v8g->_allowUseDatabase = allowUseDatabase;

  LOG_TRACE("entering V8 context %d", (int) context->_id);
  context->handleGlobalContextMethods();

  if (_developmentMode && ! initialise) {
    v8::HandleScope scope;

    TRI_ExecuteJavaScriptString(context->_context,
                                v8::String::New("require(\"internal\").resetEngine()"),
                                v8::String::New("global context method"),
                                false);
  }

  return context;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief exists an context
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::exitContext (V8Context* context) {
  V8GcThread* gc = dynamic_cast<V8GcThread*>(_gcThread);
  TRI_ASSERT(gc != nullptr);

  LOG_TRACE("leaving V8 context %d", (int) context->_id);
  double lastGc = gc->getLastGcStamp();

  CONDITION_LOCKER(guard, _contextCondition);

  // update data for later garbage collection
  TRI_v8_global_t* v8g = static_cast<TRI_v8_global_t*>(context->_isolate->GetData());
  context->_hasDeadObjects = v8g->_hasDeadObjects;
  ++context->_numExecutions;

  // HasOutOfMemoryException must be called while there is still an isolate!
  bool const hasOutOfMemoryException = context->_context->HasOutOfMemoryException();

  // check for cancelation requests
  bool const canceled = v8g->_canceled;
  v8g->_canceled = false;

  // exit the context
  context->_context->Exit();
  context->_isolate->Exit();

  // if the execution was canceled, we need to cleanup
  if (canceled) {
    context->_isolate->Enter();
    context->_context->Enter();

    context->handleCancelationCleanup();

    context->_context->Exit();
    context->_isolate->Exit();
  }

  // try to execute new global context methods
  bool runGlobal = false;
  {
    MUTEX_LOCKER(context->_globalMethodsLock);
    runGlobal = ! context->_globalMethods.empty();
  }

  if (runGlobal) {
    context->_isolate->Enter();
    context->_context->Enter();

    context->handleGlobalContextMethods();

    context->_context->Exit();
    context->_isolate->Exit();
  }

  delete context->_locker;


  // default is false
  bool performGarbageCollection = false;

  if (context->_lastGcStamp + _gcFrequency < lastGc) {
    LOG_TRACE("V8 context has reached GC timeout threshold and will be scheduled for GC");
    performGarbageCollection = true;
  }
  else if (context->_numExecutions >= _gcInterval) {
    LOG_TRACE("V8 context has reached maximum number of requests and will be scheduled for GC");
    performGarbageCollection = true;
  }
  else if (hasOutOfMemoryException) {
    LOG_INFO("V8 context has encountered out of memory and will be scheduled for GC");
    performGarbageCollection = true;
  }

  if (performGarbageCollection) {
    _dirtyContexts.push_back(context);
  }
  else {
    _freeContexts.push_back(context);
  }

  _busyContexts.erase(context);

  guard.broadcast();

  LOG_TRACE("returned dirty V8 context");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a global context functions to be executed asap
////////////////////////////////////////////////////////////////////////////////

bool ApplicationV8::addGlobalContextMethod (string const& method) {
  bool result = true;

  for (size_t i = 0; i < _nrInstances; ++i) {
    if (! _contexts[i]->addGlobalContextMethod(method)) {
      result = false;
    }
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief determine which of the free contexts should be picked for the GC
////////////////////////////////////////////////////////////////////////////////

ApplicationV8::V8Context* ApplicationV8::pickFreeContextForGc () {
  int const n = (int) _freeContexts.size();

  if (n == 0) {
    // this is easy...
    return 0;
  }

  V8GcThread* gc = dynamic_cast<V8GcThread*>(_gcThread);
  TRI_ASSERT(gc != nullptr);

  V8Context* context = nullptr;

  // we got more than 1 context to clean up, pick the one with the "oldest" GC stamp
  int pickedContextNr = -1; // index of context with lowest GC stamp, -1 means "none"

  for (int i = 0; i < n; ++i) {
    // check if there's actually anything to clean up in the context
    if (_freeContexts[i]->_numExecutions == 0 && ! _freeContexts[i]->_hasDeadObjects) {
      continue;
    }

    // compare last GC stamp
    if (pickedContextNr == -1 ||
        _freeContexts[i]->_lastGcStamp <= _freeContexts[pickedContextNr]->_lastGcStamp) {
      pickedContextNr = i;
    }
  }
  // we now have the context to clean up in pickedContextNr

  if (pickedContextNr == -1) {
    // no context found
    return 0;
  }

  // this is the context to clean up
  context = _freeContexts[pickedContextNr];
  TRI_ASSERT(context != 0);

  // now compare its last GC timestamp with the last global GC stamp
  if (context->_lastGcStamp + _gcFrequency >= gc->getLastGcStamp()) {
    // no need yet to clean up the context
    return 0;
  }

  // we'll pop the context from the vector. the context might be at any position in the vector
  // so we need to move the other elements around
  if (n > 1) {
    for (int i = pickedContextNr; i < n - 1; ++i) {
      _freeContexts[i] = _freeContexts[i + 1];
    }
  }
  _freeContexts.pop_back();

  return context;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief runs the garbage collection
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::collectGarbage () {
  V8GcThread* gc = dynamic_cast<V8GcThread*>(_gcThread);
  TRI_ASSERT(gc != nullptr);

  // this flag will be set to true if we timed out waiting for a GC signal
  // if set to true, the next cycle will use a reduced wait time so the GC
  // can be performed more early for all dirty contexts. The flag is set
  // to false again once all contexts have been cleaned up and there is nothing
  // more to do
  bool useReducedWait = false;

  // the time we'll wait for a signal
  uint64_t const regularWaitTime = (uint64_t) (_gcFrequency * 1000.0 * 1000.0);

  // the time we'll wait for a signal when the previous wait timed out
  uint64_t const reducedWaitTime = (uint64_t) (_gcFrequency * 1000.0 * 100.0);

  while (_stopping == 0) {
    V8Context* context = nullptr;

    {
      bool gotSignal = false;
      CONDITION_LOCKER(guard, _contextCondition);

      if (_dirtyContexts.empty()) {
        uint64_t waitTime = useReducedWait ? reducedWaitTime : regularWaitTime;

        // we'll wait for a signal or a timeout
        gotSignal = guard.wait(waitTime);

        // use a reduced wait time in the next round because we seem to be idle
        // the reduced wait time will allow use to perfom GC for more contexts
        useReducedWait = ! gotSignal;
      }

      if (! _dirtyContexts.empty()) {
        context = _dirtyContexts.back();
        _dirtyContexts.pop_back();
        useReducedWait = false;
      }
      else if (! gotSignal && ! _freeContexts.empty()) {
        // we timed out waiting for a signal, so we have idle time that we can
        // spend on running the GC pro-actively
        // We'll pick one of the free contexts and clean it up
        context = pickFreeContextForGc();

        // there is no context to clean up, probably they all have been cleaned up
        // already. increase the wait time so we don't cycle too much in the GC loop
        // and waste CPU unnecessary
        useReducedWait = (context != 0);
      }
    }

    // update last gc time
    double lastGc = TRI_microtime();
    gc->updateGcStamp(lastGc);

    if (context != nullptr) {
      LOG_TRACE("collecting V8 garbage");

      context->_locker = new v8::Locker(context->_isolate);
      context->_isolate->Enter();
      context->_context->Enter();

      v8::V8::LowMemoryNotification();
      while (! v8::V8::IdleNotification()) {
      }

      context->_context->Exit();
      context->_isolate->Exit();
      delete context->_locker;

      // update garbage collection statistics
      context->_hasDeadObjects = false;
      context->_numExecutions  = 0;
      context->_lastGcStamp    = lastGc;

      {
        CONDITION_LOCKER(guard, _contextCondition);

        _freeContexts.push_back(context);
        guard.broadcast();
      }
    }
  }

  _gcFinished = true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief disables actions
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::disableActions () {
  _useActions = false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enables development mode
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::enableDevelopmentMode () {
  _developmentMode = true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief runs the version check
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::runVersionCheck (bool skip, bool perform) {
  LOG_TRACE("starting version check");

  // enter context and isolate
  V8Context* context = _contexts[0];

  context->_locker = new v8::Locker(context->_isolate);
  context->_isolate->Enter();
  context->_context->Enter();

  // run upgrade script
  if (! skip) {
    LOG_DEBUG("running database version check");

    // can do this without a lock as this is the startup
    for (size_t j = 0; j < _server->_databases._nrAlloc; ++j) {
      TRI_vocbase_t* vocbase = static_cast<TRI_vocbase_t*>(_server->_databases._table[j]);

      if (vocbase != nullptr) {
        // special check script to be run just once in first thread (not in all)
        // but for all databases
        v8::HandleScope scope;

        v8::Handle<v8::Object> args = v8::Object::New();
        args->Set(v8::String::New("upgrade"), v8::Boolean::New(perform));

        context->_context->Global()->Set(v8::String::New("UPGRADE_ARGS"), args);

        bool ok = TRI_V8RunVersionCheck(vocbase, &_startupLoader, context->_context);

        if (! ok) {
          if (context->_context->Global()->Has(v8::String::New("UPGRADE_STARTED"))) {
            if (perform) {
              LOG_FATAL_AND_EXIT(
                "Database upgrade failed for '%s'. Please inspect the logs from the upgrade procedure",
                vocbase->_name);
            }
            else {
              LOG_FATAL_AND_EXIT(
                "Database version check failed for '%s'. Please start the server with the --upgrade option",
                vocbase->_name);
            }
          }
          else {
            LOG_FATAL_AND_EXIT("JavaScript error during server start");
          }
        }

        LOG_DEBUG("database version check passed for '%s'", vocbase->_name);
      }
    }
  }

  if (perform) {

    // issue #391: when invoked with --upgrade, the server will not always shut down
    LOG_INFO("database upgrade passed");
    context->_context->Exit();
    context->_isolate->Exit();
    delete context->_locker;

    // regular shutdown... wait for all threads to finish

    // again, can do this without the lock
    for (size_t j = 0; j < _server->_databases._nrAlloc; ++j) {
      TRI_vocbase_t* vocbase = static_cast<TRI_vocbase_t*>(_server->_databases._table[j]);

      if (vocbase != nullptr) {
        vocbase->_state = 2;

        int res = TRI_ERROR_NO_ERROR;

        res |= TRI_StopCompactorVocBase(vocbase);
        vocbase->_state = 3;
        res |= TRI_JoinThread(&vocbase->_cleanup);

        if (res != TRI_ERROR_NO_ERROR) {
          LOG_ERROR("unable to join database threads for database '%s'", vocbase->_name);
        }
      }
    }

    LOG_INFO("finished");
    TRI_EXIT_FUNCTION(EXIT_SUCCESS, NULL);
  }

  // and return from the context
  context->_context->Exit();
  context->_isolate->Exit();
  delete context->_locker;

  LOG_TRACE("finished version check");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief runs the upgrade check
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::runUpgradeCheck () {
  LOG_TRACE("starting upgrade check");

  // enter context and isolate
  V8Context* context = _contexts[0];

  context->_locker = new v8::Locker(context->_isolate);
  context->_isolate->Enter();
  context->_context->Enter();

  // run upgrade script
  LOG_DEBUG("running database upgrade check");

  // can do this without a lock as this is the startup
  int result = 1;

  for (size_t j = 0; j < _server->_databases._nrAlloc; ++j) {
    TRI_vocbase_t* vocbase = static_cast<TRI_vocbase_t*>(_server->_databases._table[j]);

    if (vocbase != nullptr) {
      // special check script to be run just once in first thread (not in all)
      // but for all databases
      v8::HandleScope scope;

      int status = TRI_V8RunUpgradeCheck(vocbase, &_startupLoader, context->_context);

      if (status < 0) {
        LOG_FATAL_AND_EXIT(
          "Database upgrade check failed for '%s'. Please inspect the logs from any errors",
          vocbase->_name);
      }
      else if (status == 3) {
        result = 3;
      }
      else if (status == 2 && result == 1) {
        result = 2;
      }
    }
  }

  // issue #391: when invoked with --upgrade, the server will not always shut down
  context->_context->Exit();
  context->_isolate->Exit();
  delete context->_locker;

  // regular shutdown... wait for all threads to finish

  // again, can do this without the lock
  for (size_t j = 0; j < _server->_databases._nrAlloc; ++j) {
    TRI_vocbase_t* vocbase = (TRI_vocbase_t*) _server->_databases._table[j];

    if (vocbase != nullptr) {
      vocbase->_state = 2;

      int res = TRI_ERROR_NO_ERROR;

      res |= TRI_StopCompactorVocBase(vocbase);
      vocbase->_state = 3;
      res |= TRI_JoinThread(&vocbase->_cleanup);

      if (res != TRI_ERROR_NO_ERROR) {
        LOG_ERROR("unable to join database threads for database '%s'", vocbase->_name);
      }
    }
  }

  if (result == 1) {
    TRI_EXIT_FUNCTION(EXIT_SUCCESS, NULL);
  }
  else {
    TRI_EXIT_FUNCTION(result, NULL);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief prepares the actions
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::prepareActions () {
  for (size_t i = 0;  i < _nrInstances;  ++i) {
    prepareV8Actions(i);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets an alternate init file
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::setStartupFile (const string& file) {
  _startupFile = file;
}

// -----------------------------------------------------------------------------
// --SECTION--                                        ApplicationFeature methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::setupOptions (map<string, basics::ProgramOptionsDescription>& options) {
  options["JAVASCRIPT Options:help-admin"]
    ("javascript.gc-interval", &_gcInterval, "JavaScript request-based garbage collection interval (each x requests)")
    ("javascript.gc-frequency", &_gcFrequency, "JavaScript time-based garbage collection frequency (each x seconds)")
    ("javascript.app-path", &_appPath, "directory for Foxx applications (normal mode)")
    ("javascript.dev-app-path", &_devAppPath, "directory for Foxx applications (development mode)")
    ("javascript.startup-directory", &_startupPath, "path to the directory containing JavaScript startup scripts")
    ("javascript.v8-options", &_v8Options, "options to pass to v8")

    // deprecated options
    ("javascript.action-directory", &DeprecatedPath, "path to the JavaScript action directory (deprecated)")
    ("javascript.modules-path", &DeprecatedPath, "one or more directories separated by semi-colons (deprecated)")
    ("javascript.package-path", &DeprecatedPath, "one or more directories separated by semi-colons (deprecated)")
  ;

  options[ApplicationServer::OPTIONS_HIDDEN]
    ("javascript.frontend-development", &_frontendDevelopmentMode, "allows rebuild frontend assets")
  ;
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

bool ApplicationV8::prepare () {
  // check the startup path
  if (_startupPath.empty()) {
    LOG_FATAL_AND_EXIT("no 'javascript.startup-directory' has been supplied, giving up");
  }

  // remove trailing / from path
  _startupPath = StringUtils::rTrim(_startupPath, TRI_DIR_SEPARATOR_STR);

  // derive all other options from --javascript.startup-directory
  _actionPath  = _startupPath + TRI_DIR_SEPARATOR_STR + "actions";

  _modulesPath = _startupPath + TRI_DIR_SEPARATOR_STR + "server" + TRI_DIR_SEPARATOR_STR + "modules;" +
                 _startupPath + TRI_DIR_SEPARATOR_STR + "common" + TRI_DIR_SEPARATOR_STR + "modules;" +
                 _startupPath + TRI_DIR_SEPARATOR_STR + "node";

  // dump paths
  {
    vector<string> paths;

    paths.push_back(string("startup '" + _startupPath + "'"));
    paths.push_back(string("modules '" + _modulesPath + "'"));

    if (_useActions) {
      paths.push_back(string("actions '" + _actionPath + "'"));
    }

    if (! _appPath.empty()) {
      paths.push_back(string("application '" + _appPath + "'"));
    }

    if (! _devAppPath.empty()) {
      paths.push_back(string("dev application '" + _devAppPath + "'"));
    }

    LOG_INFO("JavaScript using %s", StringUtils::join(paths, ", ").c_str());
  }

  // check whether app-path was specified
  if (_appPath.empty()) {
    LOG_FATAL_AND_EXIT("no value has been specified for --javascript.app-path.");
  }

  _startupLoader.setDirectory(_startupPath);
  ServerState::instance()->setJavaScriptPath(_startupPath);

  // check for development mode
  if (! _devAppPath.empty()) {
    _developmentMode = true;
  }

  // set up action loader
  if (_useActions) {
    _actionLoader.setDirectory(_actionPath);
  }

  // add v8 options
  if (_v8Options.size() > 0) {
    LOG_INFO("using V8 options '%s'", _v8Options.c_str());
    v8::V8::SetFlagsFromString(_v8Options.c_str(), (int) _v8Options.size());
  }

  // use a minimum of 1 second for GC
  if (_gcFrequency < 1) {
    _gcFrequency = 1;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

bool ApplicationV8::prepare2 () {
  // setup instances
  _contexts = new V8Context*[_nrInstances];

  for (size_t i = 0;  i < _nrInstances;  ++i) {
    bool ok = prepareV8Instance(i);

    if (! ok) {
      return false;
    }
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

bool ApplicationV8::start () {
  _gcThread = new V8GcThread(this);
  _gcThread->start();

  _gcFinished = false;
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::close () {
  _stopping = 1;
  _contextCondition.broadcast();

  // unregister all tasks
  if (_scheduler != nullptr && _scheduler->scheduler() != nullptr) {
    _scheduler->scheduler()->unregisterUserTasks();
  }

  // wait for all contexts to finish
  for (size_t n = 0;  n < 10 * 5;  ++n) {
    CONDITION_LOCKER(guard, _contextCondition);

    if (_busyContexts.empty()) {
      LOG_DEBUG("no busy V8 contexts");
      break;
    }

    LOG_DEBUG("waiting for %d busy V8 contexts to finish", (int) _busyContexts.size());

    guard.wait(100000);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::stop () {

  //send all busy contexts a termate signal
  {
    CONDITION_LOCKER(guard, _contextCondition);

    for (auto it = _busyContexts.begin(); it != _busyContexts.end(); ++it) {
      LOG_WARNING("sending termination signal to V8 context");
      v8::V8::TerminateExecution((*it)->_isolate);
    }
  }

  // wait for one minute
  for (size_t n = 0;  n < 10 * 60;  ++n) {
    CONDITION_LOCKER(guard, _contextCondition);

    if (_busyContexts.empty()) {
      break;
    }

    guard.wait(100000);
  }


  // wait until garbage collector thread is done
  while (! _gcFinished) {
    usleep(10000);
  }

  // stop GC thread
  _gcThread->shutdown();

  // shutdown all action threads
  for (size_t i = 0;  i < _nrInstances;  ++i) {
    shutdownV8Instance(i);
  }

  delete[] _contexts;

  // delete GC thread after all action threads have been stopped
  delete _gcThread;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                   private methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief prepares a V8 instance
////////////////////////////////////////////////////////////////////////////////

bool ApplicationV8::prepareV8Instance (const size_t i) {
  vector<string> files;

  files.push_back("common/bootstrap/modules.js");
  files.push_back("common/bootstrap/module-internal.js");
  files.push_back("common/bootstrap/module-fs.js");
  files.push_back("common/bootstrap/module-console.js"); // needs internal
  files.push_back("common/bootstrap/errors.js");
  files.push_back("common/bootstrap/monkeypatches.js");

  files.push_back("server/bootstrap/module-internal.js");

  if (! _startupFile.empty()) {
    files.push_back(_startupFile); // needs internal
  }

  LOG_TRACE("initialising V8 context #%d", (int) i);

  V8Context* context = _contexts[i] = new V8Context();

  if (context == 0) {
    LOG_FATAL_AND_EXIT("cannot initialize V8 engine");
  }

  // enter a new isolate
  context->_id = i;
  context->_isolate = v8::Isolate::New();
  context->_locker = new v8::Locker(context->_isolate);
  context->_isolate->Enter();

  // create the context
  context->_context = v8::Context::New();

  if (context->_context.IsEmpty()) {
    LOG_FATAL_AND_EXIT("cannot initialize V8 engine");
  }

  context->_context->Enter();

  TRI_InitV8VocBridge(context->_context, _server, _vocbase, &_startupLoader, i);
  TRI_InitV8Queries(context->_context);

  TRI_InitV8Cluster(context->_context);

  if (_useActions) {
    TRI_InitV8Actions(context->_context, _vocbase, _scheduler, _dispatcher, this);
  }

  TRI_InitV8Buffer(context->_context);
  TRI_InitV8Conversions(context->_context);
  TRI_InitV8Utils(context->_context, _startupPath, _modulesPath);
  TRI_InitV8Shell(context->_context);

  {
    v8::HandleScope scope;

    char const* logfile = TRI_GetFilenameLogging();
    TRI_AddGlobalVariableVocbase(context->_context, "LOGFILE_PATH", logfile != 0 ? v8::String::New(logfile) : v8::Null());
    TRI_AddGlobalVariableVocbase(context->_context, "APP_PATH", v8::String::New(_appPath.c_str(), (int) _appPath.size()));
    TRI_AddGlobalVariableVocbase(context->_context, "DEV_APP_PATH", v8::String::New(_devAppPath.c_str(), (int) _devAppPath.size()));
    TRI_AddGlobalVariableVocbase(context->_context, "DEVELOPMENT_MODE", v8::Boolean::New(_developmentMode));
    TRI_AddGlobalVariableVocbase(context->_context, "FE_DEVELOPMENT_MODE", v8::Boolean::New(_frontendDevelopmentMode));

    for (map<string, bool>::iterator i = _definedBooleans.begin();  i != _definedBooleans.end(); ++i) {
      TRI_AddGlobalVariableVocbase(context->_context, i->first.c_str(), v8::Boolean::New(i->second));
    }
  }

  // load all init files
  for (size_t j = 0;  j < files.size();  ++j) {
    bool ok = _startupLoader.loadScript(context->_context, files[j]);

    if (! ok) {
      LOG_FATAL_AND_EXIT("cannot load JavaScript utilities from file '%s'", files[j].c_str());
    }
  }

  // and return from the context
  context->_context->Exit();
  context->_isolate->Exit();
  delete context->_locker;

  // initialise garbage collection for context
  context->_numExecutions  = 0;
  context->_hasDeadObjects = true;
  context->_lastGcStamp    = TRI_microtime();

  LOG_TRACE("initialised V8 context #%d", (int) i);

  _freeContexts.push_back(context);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief prepares the V8 actions
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::prepareV8Actions (const size_t i) {
  LOG_TRACE("initialising V8 actions #%d", (int) i);

  // enter context and isolate
  V8Context* context = _contexts[i];

  context->_locker = new v8::Locker(context->_isolate);
  context->_isolate->Enter();
  context->_context->Enter();

  // scan for foxx applications
  if (i == 0) {

    // once again, we don't need the lock as this is the startup
    for (size_t j = 0; j < _server->_databases._nrAlloc; ++j) {
      TRI_vocbase_t* vocbase = (TRI_vocbase_t*) _server->_databases._table[j];

      if (vocbase != 0) {
        TRI_V8InitialiseFoxx(vocbase, context->_context);
      }
    }
  }

  // load all actions
  if (_useActions) {
    v8::HandleScope scope;

    bool ok = _actionLoader.executeAllScripts(context->_context);

    if (! ok) {
      LOG_FATAL_AND_EXIT("cannot load JavaScript actions from directory '%s'", _actionLoader.getDirectory().c_str());
    }

    {
      v8::HandleScope scope;
      TRI_ExecuteJavaScriptString(context->_context,
                                  v8::String::New("require(\"internal\").actionLoaded()"),
                                  v8::String::New("action loaded"),
                                  false);
    }

  }

  // and return from the context
  context->_context->Exit();
  context->_isolate->Exit();
  delete context->_locker;

  // initialise garbage collection for context
  LOG_TRACE("initialised V8 actions #%d", (int) i);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief shut downs a V8 instances
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::shutdownV8Instance (size_t i) {
  LOG_TRACE("shutting down V8 context #%d", (int) i);

  V8Context* context = _contexts[i];

  context->_locker = new v8::Locker(context->_isolate);
  context->_isolate->Enter();
  context->_context->Enter();

  v8::V8::LowMemoryNotification();
  while (! v8::V8::IdleNotification()) {
  }

  TRI_v8_global_t* v8g = static_cast<TRI_v8_global_t*>(context->_isolate->GetData());

  if (v8g) {
    delete v8g;
  }

  context->_context->Exit();
  context->_context.Dispose(context->_isolate);

  context->_isolate->Exit();
  delete context->_locker;

  context->_isolate->Dispose();

  delete context;

  LOG_TRACE("closed V8 context #%d", (int) i);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
