Trading off Garbage Collector Speed vs Memory Use
-------------------------------------------------

Garbage collection inherently involves a space vs. time trade-off: The less frequently we collect,
the more memory we will use. This is true both for average heap memory use, and also maximum heap
memory use, measured just before GC completion. (We're assuming that the application's behavior
doesn't change depending on GC frequency, which isn't always true, but ideally should be.)

Android provides primarily one knob to control this trade-off: the `HeapTargetUtilization` option.
This is a fraction between 0 and 1, normally between 0.3 and 0.8 or so. The collector aims to
ensure that just before a collection completes and memory is reclaimed, the ratio of live data in
the heap to the allocated size of the heap is `HeapTargetUtilization`.  With a non-concurrent GC,
we would trigger a GC when the heap size reaches the estimated amount of live data in the heap
divided by `HeapTargetUtilization`. In the concurrent GC case, this is a bit more complicated, but
the basic idea remains the same.

Note that *lower* values of `HeapTargetUtilization` correspond to more memory use and less
frequent GC. A high memory device (relative to memory requirements, which will usually depend on
screen size, etc.) might use a value of 0.5, where a low memory device might use 0.75.

This scheme is designed to keep GC cost per byte allocated roughly constant, independent of the
heap size required by the application:

GC cost is usually dominated by the cost of visiting live data in the heap.  Assume that the GC
cost is *cL*, where *L* is the number of live bytes in the heap, which we assume remains roughly
fixed, and *c* is the cost of processing a byte of live data. If we abbreviate
`HeapTargetUtilization` as *u*, we will collect whenever the heap has grown to *L/u* bytes. Thus
between collections, we can allocate *L/u - L* or *L(1/u - 1)* bytes.  This means the cost per
byte allocated is *cL / L(1/u - 1)* or just *c / (1/u - 1)*, and thus independent of *L*, the
amount of live data.

(It's not clear whether we should really try to keep per-byte GC cost constant on a system running
many ART instances. For a counter-argument, see [Marisa Kirisame et al, "Optimal heap limits for
reducing browser memory use"](https://dl.acm.org/doi/10.1145/3563323). The current ART approach is
more traditional among garbage collectors, and reasonably defensible, especially if we only have
process-local information.)

In fact GCs have some constant cost independent of heap size. With very small heaps, the above
rule would constantly trigger GCs, and this constant cost would dominate. With a zero-sized heap,
we would GC on every allocation. This would be slow, so a minimimum number of allocations between
GCs, a.k.a. `HeapMinFree`, makes sense. Ideally it should be computed based on the
heap-size-invariant GC cost, which is basically the cost of scanning GC roots, including the cost
of "suspending" threads to capture references on thread stacks. Currently we approximate that with
a constant, which is suboptimal. It would be better to have the GC compute this based on actual
data, like the number of threads, and the current size of the remaining root set.

`HeapMinFree` should really reflect the characteristics of the Java implementation, given data
about the application known to ART. It is currently configurable. But we would like to move away
from that, and have ART use more sophisticated heuristics in setting it.

There used to be some argument that once application heaps get too large, we may want to limit
their heap growth even at the expense of additional GC cost. That could be used to justify
`HeapMaxFree`, which limits the amount of allocation between collections in applications with a
very large heap.  Given that in most cases device memory has grown faster than our support for
huge Java heaps, it is unclear this still has a purpose at all. We recommend it be set to
something like the maximum heap size, so that it no longer has an effect. We may remove it, or at
least make it ineffective, in the future. However, we still need to confirm that this is
acceptable for very low memory devices.

Favoring Latency Sensitive Processes
------------------------------------

For particularly sensitive processes, such as the foreground application or Android's system
server, the bytes we allocate between consecutive GCs, as computed via the above controls, are
multiplied by `ForegroundHeapGrowthMultiplier` + 1. Such applications thus use more memory
in order to reduce GC time.

When an application moves into the background, we normally trigger a GC to reduce its memory
footprint before doing so.

Interaction with Compacting Garbage Collectors
----------------------------------------------
ART normally uses one of two compacting garbage collectors. These also have compile-time-specified
heap density targets. These are in effect for collections other than those that prepare the
application for moving into the background:

'kEvacuateLivePercentThreshold', currently 75% is used for the CC collector,
`kBlackDenseRegionThreshold`, currently 95%, is used for the CMC collector. Roughly speaking, we
don't try to compact pages or regions that already exceed these occupancy thresholds, and we
cannot reallocate the "holes" in uncompacted memory.  This effectively prevents us from reclaiming
some memory that remains fragmented after a GC.

Some heap areas, e.g. those used for non-movable objects, are also subject to (usually very small
amounts of) fragmentation, and are treated similarly.

`HeapTargetUtilization` should thus be appreciably less than the relevant density target above, to
ensure that, even in the presence of fragmentation, we generate enough compacted heap space to not
instantly trigger another GC.  (In the long run, we will probably compute these density thresholds
from `HeapTargetUtilization` instead.)
