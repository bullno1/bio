# bio

@mainpage Asynchronous I/O framework with coroutines

This is the documentation for bio, an async I/O framework with coroutines.

## General idea

With [minicoro](https://github.com/edubart/minicoro), the thread of execution can be suspended on an I/O call.
One can just @ref coro "spawn a coroutine" for every connection and treat every I/O operation as blocking.
The userspace scheduler will take care of waking up coroutines when an I/O request is finished.

To communicate between coroutines, a @ref mailbox "message passing primitive" is introduced.
This borrows an idea from [Erlang](https://www.erlang.org/).

## Getting started

To learn how to use it, first checkout the following sections:

* @ref init "": How to initialize the framework
* @ref coro "": How to get any work done
* @ref handle "": A brief on resource handling and safety
* @ref bio_run_async "Async thread pool": Avoid blocking the main thread

[Other topics](./topics.html) can be discovered as needed.
