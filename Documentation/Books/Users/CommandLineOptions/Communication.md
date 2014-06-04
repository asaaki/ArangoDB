!CHAPTER Command-Line Options for Communication 

`--scheduler.threads arg`
An integer argument which sets the number of threads to use in the IO scheduler. The default is 1.

`--scheduler.maximal-queue-size size`

Specifies the maximum size of the dispatcher queue for asynchronous task execution. If the queue already contains size tasks, new tasks will be rejected until other tasks are popped from the queue. Setting this value may help preventing from running out of memory if the queue is filled up faster than the server can process requests.


`--scheduler.backend arg`

The I/O method used by the event handler. The default (if this option is not specified) is to try all recommended backends. This is platform specific. See libev for further details and the meaning of select, poll and epoll.


`--show-io-backends`

<!--
@anchor CommandLineSchedulerThreads
@copydetails triagens::rest::ApplicationScheduler::_nrSchedulerThreads

@CLEARPAGE
@anchor CommandLineSchedulerMaximalQueueSize
@copydetails triagens::arango::ArangoServer::_dispatcherQueueSize

@CLEARPAGE
@anchor CommandLineSchedulerBackend
@copydetails triagens::rest::ApplicationScheduler::_backend
-->

If this option is specified, then the server will list available backends and
exit. This option is useful only when used in conjunction with the option
scheduler.backend. An integer is returned (which is platform dependent) which
indicates available backends on your platform. See libev for further details and
for the meaning of the integer returned. This describes the allowed integers for
`scheduler.backend`, see [here](#command-line-options-for-communication) for details.