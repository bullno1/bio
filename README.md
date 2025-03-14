# bio
## What?

Async I/O framework with coroutines.

Still WIP.

## Why?

Why not?
Network programming with C does not have to be tedious.

C is a simple language with very little mental overhead.
It just lacks batteries.
This is a battery.

## How?

With [minicoro](https://github.com/edubart/minicoro), the thread of execution can be suspended on an I/O call.
One can just spawn a coroutine for every connection and treat every I/O operation as blocking.
The userspace scheduler will take care of waking up coroutines when an I/O request is finished.

On Windows, [IOCP](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports) makes it extremely simple to have a centralized place to check for I/O completion.
On Linux, [io_uring](https://unixism.net/loti/index.html) now delivers the same capabilities.

To communicate between coroutines, a [message passing facility](https://github.com/bullno1/bio/blob/master/include/bio/mailbox.h) is introduced.
This borrows an idea from [Erlang](https://www.erlang.org/).

## Goals

* Design for the common case first: The most common I/O code pattern is to make a blocking I/O call, process the result and make more calls.
  That should be the most straightforward to write.
  Fan in (waiting on multiple calls) or fan out (sending data to multiple destinations) are special cases.
  For the first version, all calls will block the calling coroutine.
* No dangling references: Handles instead of pointers for framework allocated objects (e.g: files, sockets, coroutines...).
  Not only they are [better pointers](https://floooh.github.io/2018/06/17/handles-vs-pointers.html), the safety feature shines in a concurrent environment.
  Coroutines may spawn and despawn, sockets open and closes while some other code is still expecting them...
  Instead of crashing, the operation just errors out and the caller can handle it accordingly.
* Type-safety: As much as possible, catch error at compile-time.
  Handles are strongly typed, e.g: a `bio_socket_t` is not a `bio_file_t` since on a certain platform (Windows), they don't support the same operations.

  Mailboxes (message passing primitive) are strongly typed as: `BIO_MAILBOX(type_name)`.
  Some macro trickery was used to ensure at compile-time that only the right type of message can be sent or received.
* C-first API: This is a language with:

  * Manual memory management
  * No garbage collector

  Instead of copying the API design of GC languages, the API is designed with C first.
  Some implications:

  * Functions like [read](https://man7.org/linux/man-pages/man2/read.2.html) must accept an user-provided buffer.
    It can be stack or heap/arena allocated.
    Rarely, will the framework returns a buffer to the user.
  * The mailbox API is designed for fixed-sized messages with a bounded queue.
    The message buffer is owned by the mailbox.
    Senders and receivers just read/write into that buffer.
  * Variable-length buffers would require considerations from the user but techniques such as [arena](https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator) could be used.
