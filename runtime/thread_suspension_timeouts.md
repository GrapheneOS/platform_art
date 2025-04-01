Thread Suspension timeouts in ART
---------------------------------
ART occasionally needs to "suspend" threads for a variety of reasons. "Suspended" threads may
continue to run, but may not access data structures related to the Java heap. Threads that are
blocked indefinitely in ways understood by ART, e.g. because they are waiting on a monitor,
waiting for Java IO, or in normal (not `@CriticalNative` or `@FastNative`) JNI calls are
considered to be already suspended, and not subject to this process. Please see
`mutator_gc_coord.md` for details.

The suspension process usually involves setting a flag for the thread to be "suspended", possibly
causing the thread being "suspended" to generate a SIGSEGV at an opportune point, so that it
notices the flag, and then having it acknowledge that it is now "suspended".

This process is time-limited so that it does not hang a misbehaving process indefinitely. A
timeout crashes the process with an abort message indicating a timeout in one of `SuspendAll`,
`SuspendThreadByPeer`, or `SuspendThreadByThreadId`. It will normally occur after 4 seconds if the
thread requesting the suspension has high priority, and either 8 or 12 seconds otherwise.

Any such timeout has the inherent downside that it may occur on a sufficiently overcommitted
device even when there is no deadlock or similar bug involved. Clearly this should be
extremely rare.

Android 15 changed the handling of such timeouts in several ways:

1) The underlying suspension code was changed to improve correctness and better report timeouts.
This included reducing the timeout in some cases to avoid the danger of reporting such timeouts as
hard-to-analyze ANRs.

2) When such a timeout is encountered, we now aggressively try to abort the thread refusing to
suspend, so that the main reported stack trace gives a better indication of what went wrong. The
thread originating the suspension request will still abort if this failed or took too long.

3) The timeout abort message should contain a fair amount of information about the thread failing
to abort, including two prefixes of the `/proc/<pid>/task/<tid>/stat` for the offending thread,
taken a second or more apart. These snapshots contain several bits of useful information, such as
kernel process states, the thread priority, and the `utime` and `stime` fields indicating the
amount of time for which the thread was scheduled. See `man proc`, and look for `/proc/pid/stat`.
(Initial Android 15 versions reported `/proc/<tid>/stat` instead, which includes process rather
than thread cpu time.)

This has been known to fail for several reasons:

1) A deadlock involving thread suspension. The issues here are discussed in `mutator_gc_coord.md`.
A common cause of these appear to be "native" C++ locks that are both held while executing Java
code, and acquired in `@CriticalNative` or `@FastNative` JNI calls. These are clear bugs that,
once identified, usually have a fairly clear-cut fix.

2) Overcommitting the cores, so that the thread being "suspended" just does not get a chance to
run within the timeout of 4 or more seconds.

3) Either ART or `@CriticalNative`/`@FastNative` code that continues in Java `kRunnable` state for
too long without checking suspension requests.

4) The thread being suspended is either itself running at a low thread priority, or is waiting for
a thread at low thread priority. A Java priority 10 thread has Linux niceness -8, but a priority 1
thread has niceness 20. This means the former gets roughly 1.25^28. or more than 500, times the
cpu share of the latter when the device's cores are overcommitted. It is worth noting that
priority 5 (NORMAL) corresponds to niceness 0, while priority 4 corresponds to niceness 10, which
is already almost a factor of 10 difference.

When we do see such timeouts, they are often a combination of the last 3. The fixes in such a case
tend to be less clear. Cores may become significantly overcommitted due to attempts to avoid
unused cores, particularly during startup. There are currently times when ART needs to perform IO
or paging operations while the Java heap is not in a consistent state. Priority issues can be
difficult to address, since temporary priority changes may race with other priority changes.

Different suspension timeout failures will usually need to be addressed individually.
There is no single "silver bullet" fix for all of them. There is ongoing work
to improve the tools available for handling priority issues. Currently the possible fixes
include:

- Remove any newly discovered deadlocks, e.g. by removing an `@FastNative` annotation to prevent
  a lock from being acquired while the thread already has Java heap access. Or no longer
  hold native locks across calls to Java.
- Reduce the amount of time spent continuously in Java runnable state. For application code, that
  may again involve removing `@FastNative` or `@CriticalNative` annotations. For ART internal
  code, break up `ScopedObjectAccess` sections or the like, being careful to not hold native
  pointers to Java heap objects across such sections.
- Avoid calling problem code that is known to not be as responsive to suspension requests as we
  would like. The most common example of this is generation of Java stack traces. Usually this is
  fine, but if done very frequently, especially from low priority threads, you may see a
  non-negligible suspension timeout failure rate, or at least add appreciable jank.
- Avoid excessive parallelism that is causing some threads to starve.
- Reduce differences in thread priorities and, if necessary, avoid very low priority threads, for
  the same reason.
- On slow devices, if you are in a position to do so, consider setting `ro.hw_timeout_multiplier`
  to a value greater than one.
