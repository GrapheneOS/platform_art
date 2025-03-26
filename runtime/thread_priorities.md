Thread Priorities in Android
----------------------------

[TODO(b/389104950) Some of the following is currently more aspirational than fact. Implementation
is in progress. ]

Unfortunately, this topic relies on several different kinds of thread "priority", which we will
try to distinguish as follows:

1. The priority argument passed to Posix `setpriority()`, which affects Linux threads using
   `SCHED_OTHER` scheduling. These range in value from -20 to 19, with larger values correspond to
   *lower* scheduling priority. We will refer to these as "niceness". A difference of 1 in
   niceness corresponds to something like a factor of 1.25 in processor share when cores are
   overcommitted. Details may change as the Linux scheduler changes.

2. Java thread priorities. In practice, these range from 1 to 10, with higher values corresponding
   to higher scheduling priority. We refer to these as "java-priorities".

3. The value of the `sched_priority` field, for example in the `sched_param` struct passed to
   `sched_setparam()`. This affects only `SCHED_FIFO` and `SCHED_RR` scheduling policies. The
   latter is not normally used in Android. The former has some limited uses as described below.
   These threads always have higher scheduling priority than normal `SCHED_OTHER` threads, and
   they can effectively preclude even essential system functions from running. Larger values
   correspond to higher scheduling priority. We will refer to these as "rt-priorities".

ART initially assumed that Java threads always used `SCHED_OTHER` Linux thread scheduling. Under
this discipline all threads get a chance to run, even when cores are overcommitted. However the
share of cpu time allocated to a particular thread varies greatly, by up to a factor of thousands,
depending on its "niceness", as set by `setpriority()` (or the swiss-army-knife `sched_setattr()`).

In general java-priorities are mapped, via the platform supplied `libartpalette` to corresponding
niceness values for `SCHED_OTHER` threads, These niceness values are then installed with
`setpriority()`.

The above assumptions are still generally true, but there are two important exceptions:

1. ART occasionally assigns niceness values for internal daemon threads that are obtained by
   interpolating between java-priorities.  These niceness values do not map perfectly back onto
   java-priorities. Thread.getPriority() rounds them to the nearest java-priority.

2. Frameworks application management code may temporarily promote certain threads to `SCHED_FIFO`
   scheduling with an associated rt-priority. As mentioned above, such threads cannot be preempted
   by `SCHED_OTHER` threads. There are currently no clear rules about which threads may run under
   `SCHED_FIFO` scheduling. In order to prevent deadlocks, all ART, libcore, and frameworks code
   should be written so that it can safely run under `SCHED_FIFO`. This means that wait loops that
   either spin consuming CPU time, or attempt to release it by calling `sched_yield()` or
   `Thread.yield()` are *incorrect*. At a minimum they should eventually sleep for short periods.
   Preferably, they should call an OS waiting primitive. Mutex acquisition, or Java's
   `Object.wait()` do so internally.

How to set priorities
---------------------

Java code is arguably allowed to expect that `Thread.getPriority()` returns the last value passed
to `Thread.setPriority()`, at least unless the application itself calls Android-specific code or
native code documented to alter thread priorities. This argues that `Thread.getPriority()` should
cache the current priority. Historically, this has also been done for performance reasons. We
continue to do so, though we now cache niceness rather than java-priority.

In order to keep this cache accurate, applications should try to set priorities via
`Thread.setPriority(int)`. `android.os.Process.setThreadPriority(int)` may be used to set any
niceness value, and interacts correctly with the `Thread.getPriority()`. (This treatment
of `android.os.Process.setThreadPriority(int)` is currently still future work.)

All other mechanisms, including `android.os.Process.setThreadPriority(int, int)` (with a thread id
parameter) bypass this cache. This has two effects:

1. The change of priority is invisible to `Thread.getPriority()`.

2. If the effect is to set a positive (> 0, i.e. low priority) niceness value, that effect may be
   temporary, since ART, or the Java appliction may undo the effect.

Avoiding races in setting priorities
------------------------------------

Priorities may be set either from within the process itself, or by external frameworks components.
If multiple threads or external components try to set the priority of a thread at the same time,
this is prone to races. For example:

Consider a thread *A* that temporarily wants to raise its own priority from *x* to *y* and then
restore it to the initial priority *x*. If another thread happens to also raise its priority to
*y* while it is already temporarily raised to *y*, resulting in no immediate change, *A* will have
no way to tell that it should not lower the priority back to *x*. The update by the second thread
can thus be lost.

In order to minimize such issues, all components should avoid racing priority updates and play by
the following rules:

1. The application is entitled to adjust its own thread priorities. Everything else should
   interfere with that as little as possible. The application should adjust priorities using
   either `Thread.setPriority(int)` or `android.os.Process.setThreadPriority(int)`. JNI code that
   updates the thread priority only for the duration of the call, may be able to adjust its
   own priorities using OS calls, using something like the ART protocol outlined below,
   so long as the JNI code does not call back into Java code sensitive to thread priorities.
   The application should generally restore its priority to an expected value, such as a saved
   value previously read via `Thread.getPriority()`, so that the value it restores is unaffected by
   by priority adjustment from another process.

2. ART may temporarily change the priority of an application thread with positive niceness to a
   lower non-negative niceness value (usually zero), and then restore it it to the cached value as
   detailed below.  This happens only from within that thread itself.  This is necessary, since
   threads with large positive niceness may become esentially unresponsive under heavy load, and
   may thus block essential runtime operations.

3. External process priority updates, usually by another process, should not use mechanisms that
   affect the cached value, specifically `android.os.Process.setThreadPriority(int)` or
   `Thread.setPriority(int)`, so that such temporary updates do not result in the application
   restoring the wrong value after a temprary priority adjustment. This is normally automatic,
   since a cross-process niceness change doesn't affect the cached value.

4. Priority updates using means other than `android.os.Process.setThreadPriority(int)` or
   `Thread.setPriority(int)` should set a positive niceness value only when it is OK for ART (or
   the application) to overwrite that with the value cached by Java at any point. ART will not
   overwrite negative niceness values, unless the negative value is installed exactly between ART's
   priority check and priority update. (There is no atomic priority update in Linux.) Hence,
   ideally:

5. In order to minimize interference with applications directly using OS primitive to
   adjust priorities, external processes should avoid updating priorities of threads
   executing managed code, or that may therwise be concurrently adjusting its own priorities.
   If they do, ART can at any moment overwrite their updates and restore its cached value.
   That is good, because it is not always clear how to enforce such timing constraints.

ART may use the following to temporarily increase priority by lowering niceness to zero:

```
  current_niceness = getpriority(<me>);
  if (current_niceness <= 0 || current_niceness != <cached niceness>)
    <do not change niceness>;
  else
    setpriority(<me>, 0);
  <do high priority stuff that does not alter priority / niceness>
  if (<we changed niceness> && getpriority(<me>) == 0) {
    // Either there were no external intervening priority/niceness changes, or an
    // external process restored our 0 niceness in the interim. Thread.setPriority(0)
    // may have been called by another thread in the interim. In any of those cases,
    // it is safe to restore priority with:
    setpriority(<me>, <niceness cached by Java>);
    // setpriority(<me>, current_niceness) would be incorrect with an intervening
    // Thread.setPriority(0).
  }
```

The above requires sufficient concurrency control to ensure that no `Thread.setPriority()`
calls overlap with the priority restoration at the end.

Java priority to niceness mapping
---------------------------------

The priority mapping is mostly determined by libartpalette, which is a platform component
outside ART. Traditionally that provided `PaletteGetSchedPriority` and `PaletteSetSchedPriority`,
which returned and took Java priority arguments, thus attempting to entirely hide the mapping.
This turned out to be impractical, because some ART components, e.g. ART's system daemons,
were tuned to use niceness values that did not correspond to Java priorities, and thus used
Posix/Linux `setpriority` calls directly. This scheme also prevents us from easily caching
priorities set by `android.os.Process.setThreadPriority(int)`, which traffics in Linux niceness
values.

Thus libartpalette now also provides `PaletteMapPriority()`, which is used in connection
with Linux `setpriority(), and is preferred when available.

Kernel-supported priority inheritance
------------------------------------

ART often suffers from priority inversions: A high priority thread *A* waits for a lower
priority thread *B*. This may happen because *B* holds a lock that *A* needs, because *A* is
waiting for a condition variable that *B* will eventually signal, or because *A* is waiting
for *B* to pass a barrier, etc. The reason ART needs to explicitly adjust priorities is
mostly to work around this kind of situation.

In the case of locks, there is a well-established technique for addressing this, which is
partially supported by the Linux kernel: If *A* blocks on a lock held by *B*, we can temporarily
raise *B*'s priority to *A*'s until the lock is released. The Linux kernel provides
`FUTEX_LOCK_PI` and pthreads provides `PTHREAD_PRIO_INHERIT` to support this. The implementation
avoids the race issues we discussed above, along with some others. It is thus quite attractive.

We plan to make use of this facility in the future. There are ongoing discussions about expanding
it to cover some non-lock cases, which are equally important to us. However, there are currently
some obstacles in the way of such use:

1. The kernel supports priority inheritance for non-real-time-threads only in 6.12 and later. Pure
   real-time priority inheritance does not help us much, since it is, for good reason, rarely used.

2. User-level support currently only exists for `pthread_mutex`, and not for various kinds of locks
   used internally by ART, nor those by Java synchronized blocks, nor the Java-implemented locks
   used by java.util.concurrent. For the former two, this would require at least a major redesign,
   possibly with some negative performance consequences. For the latter, it's currently impossible
   because the implementation builds directly on `park()` / `unpark()`, which do not expose the
   thread being waited for to the kernel.

3. Even for `pthread_mutex_lock()` we would have to be consistent about using
  `PTHREAD_PRIO_INHERIT`everywhere. This may have negative performance consequences, and the Posix
  standard appears to prohibit defaulting to it.

Thus, at the moment, we basically do not use kernel-supported priority inheritance. But stay
tuned.
