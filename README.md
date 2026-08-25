*This project has been created as part of the 42 curriculum by mide-fre.*

# Codexion

## Description

Codexion simulates a shared co-working hub where `N` coders compete for `N` USB
dongles. Each coder cycles through **compiling**, **debugging** and
**refactoring**. Compiling requires *two* dongles held simultaneously, and a
coder who does not start a new compile within `time_to_burnout` milliseconds of
their last one burns out, ending the simulation.

On top of the classic resource-contention problem, this project adds three
layers:

- **Dongle cooldown** — a released dongle stays unavailable for
  `dongle_cooldown` ms, so "put it back and someone takes it" no longer holds.
- **Explicit arbitration** — every dongle keeps a request queue ordered by a
  selectable policy: `fifo` (arrival order) or `edf` (earliest deadline first,
  where deadline = `last_compile_start + time_to_burnout`).
- **A hand-rolled binary heap** — no standard library priority queue is used.

Each coder is a POSIX thread. A separate monitor thread detects burnout and
stops the simulation, and all logging is serialised so that two messages never
interleave.

## Instructions

Build:

```
make          # build ./codexion
make clean    # remove object files
make fclean   # remove object files and the binary
make re       # fclean + all
```

Compiled with `cc -Wall -Wextra -Werror -pthread`.

Run:

```
./codexion number_of_coders time_to_burnout time_to_compile time_to_debug \
           time_to_refactor number_of_compiles_required dongle_cooldown scheduler
```

All eight arguments are mandatory. Times are in milliseconds. `scheduler` must
be exactly `fifo` or `edf`. Non-integer, negative and out-of-range values are
rejected with a usage message on `stderr`.

### Usage examples

```
./codexion 5 800 200 200 200 5 0 fifo      # 5 coders, no cooldown, runs to completion
./codexion 5 800 200 200 200 5 50 edf      # EDF arbitration with a 50 ms cooldown
./codexion 1 800 200 200 200 5 0 fifo      # single coder: takes one dongle, burns out at 801
./codexion 4 1500 200 200 200 3 300 fifo   # high cooldown, feasible thanks to the wider deadline
```

The simulation stops either when a coder burns out or when every coder has
compiled at least `number_of_compiles_required` times.

### Source layout

| File | Contents |
|---|---|
| `codexion.h` | All structures and prototypes |
| `codexion.c` | `main` |
| `parse.c` | Argument validation |
| `init.c` | Allocation, mutex/cond initialisation, dongle assignment |
| `run.c` | Thread creation and joining |
| `cleanup.c` | Destruction and freeing |
| `heap.c`, `heap_ops.c` | Binary heap (init, comparator, sift, push/pop/remove) |
| `arb.c` | Eligibility-based arbitration predicates |
| `dongle.c`, `dongle_utils.c` | Request lifecycle, pair acquisition, release |
| `coder.c` | Coder thread routine |
| `monitor.c` | Monitor thread routine |
| `time.c`, `log.c` | Clock helpers and serialised logging |

## Feature list and technical choices

**No global variables.** A single `t_sim` structure is stack-allocated in `main`
and passed by pointer to every thread. Each `t_coder` carries a back-pointer to
it.

**Zero allocation per request.** Each `t_request` lives on the stack of the
thread inside `dongle_acquire`, and the heap stores *pointers* to those
structures. The only allocations in the whole program are the coder array, the
dongle array, and one backing array per heap — 12 allocations, 12 frees.
Because the heap holds pointers, checking "am I the request at the front?" is a
pointer identity comparison rather than a field-by-field match.

**One comparator, two policies.** `heap_less` is the only place where the
scheduling policy lives:

- `fifo` orders by a globally increasing sequence number.
- `edf` orders by deadline, breaking ties first by sequence number and then by
  coder id, which makes the policy fully deterministic even when two deadlines
  are numerically equal.

**Arbitrary removal from the heap.** Each `t_request` stores its own index into
the heap array, updated on every swap, push, pop and removal. This allows a
request to be pulled from the middle of the queue — needed both when a waiting
coder is granted its pair and when the simulation stops while it is still
queued. Missing a single index update silently corrupts removals, so the
discipline is enforced in `heap_swap` rather than at the call sites.

## Blocking cases handled

### Deadlock — breaking circular wait

The classic failure mode of this topology is every coder holding one dongle and
waiting for the next, forming a cycle. Two independent mechanisms address it.

First, **dongles are always requested in ascending id order**. At
initialisation each coder's left and right dongles are sorted into `first` and
`second`, so exactly one coder in the ring ends up with the reversed order. That
breaks the cycle in the wait-for graph and eliminates Coffman's circular-wait
condition.

Second, and more importantly, **hold-and-wait is eliminated entirely**. A coder
never holds one dongle while waiting for the other: it enqueues a request on
both queues and only marks either dongle as taken once *both* are simultaneously
available and it has won arbitration on both. Until that moment it holds
nothing.

This second mechanism was added after testing revealed the first one was not
sufficient. With ordered acquisition alone the program never deadlocked, but
runs serialised into a chain — coder 3 waiting on coder 2 waiting on coder 1,
each holding a dongle — and coders further along the ring burned out waiting for
a resource that was held but idle. Deadlock freedom and liveness are not the
same property.

### Starvation — arbitration by eligibility

A subtler problem appeared once pair acquisition was atomic. Requiring a coder
to be at the *head* of both queues means a coder that arrived first but whose
pair is not currently free blocks everyone behind it, even though it cannot
proceed itself. With `N=200` this reduced genuine parallelism from roughly a
hundred concurrent compiles to nine.

The rule is therefore not "am I first in the queue?" but **"am I the most
prioritary among the requests that can actually start right now?"**. `is_best`
scans a queue and yields only to competitors that both outrank the caller *and*
have their own pair free. A queue never contains more than two requests, since
each dongle is adjacent to exactly two coders, so this scan is linear over two
elements.

Liveness follows from the priority order being total: among all requests whose
pair is free, the global minimum is necessarily best in both of its own queues,
so whenever any pair is free at least one coder makes progress. No coder is
starved under `edf` given feasible parameters.

### Cooldown handling

A released dongle records `available_at = now + cooldown` and is not grantable
until that instant passes. The end of a cooldown is a purely *temporal* event
with no thread to signal it, which is why waiters use `pthread_cond_timedwait`
rather than `pthread_cond_wait` — a plain wait would sleep until some unrelated
release happened to wake it, which may never occur. The wake-up deadline is the
earlier of the two dongles' cooldown expiries.

### Feasibility

Cooldown can make otherwise reasonable parameters infeasible, and it is worth
being precise about where the boundary lies. For an even ring the coders split
into two alternating groups and the period of a full cycle is:

```
period = max(time_to_compile + time_to_debug + time_to_refactor,
             2 * time_to_compile + 2 * dongle_cooldown)
```

Measured against `4 800 200 200 200 3 <cd> fifo`:

| cooldown | predicted period | observed |
|---|---|---|
| 0 | 600 ms | compiles at 31, 636, 1236 |
| 100 | 600 ms | compiles at 0, 600, 1200 |
| 200 | 800 ms | burnout at 801 — period equals the deadline exactly |
| 300 | 1000 ms | compiles at 0, 1003, 2003 (with `time_to_burnout 1500`) |

Odd rings are worse, since the ring is not 2-colourable and needs three phases.
Burnout in these cases is the correct outcome, not a scheduling failure.

### Precise burnout detection

The monitor polls every coder's `last_compile_start` on a **1 ms absolute
deadline** rather than a relative sleep, so a late wake-up does not accumulate
drift into subsequent iterations. The burnout message is printed with the
timestamp captured *before* the write, so the logged time reflects the moment of
detection rather than the moment the terminal accepted the line.

Measured on 42 cluster hardware, `5 800 200 200 200 3 300 fifo` reports burnout
at **801 ms in 20 runs out of 20** — a deadline of 800 ms detected within a
single millisecond, well inside the 10 ms requirement.

The same binary on a VirtualBox guest occasionally reported 850–1000 ms.
Instrumenting the monitor showed gaps of 130–240 ms in which *no thread ran at
all* — the coders' own log timestamps skipped by the same amount over the same
intervals, and `CLOCK_REALTIME` and `CLOCK_MONOTONIC` agreed on the gap to the
millisecond. These are whole-VM stalls by the hypervisor, not detection latency.

### Log serialisation

All output passes through `log_state`, which holds `log_lock` for the duration
of the write, so two messages can never interleave on a line. The stop flag is
also tested *inside* that critical section: without it a coder that had already
decided to print "is compiling" could emit it after "burned out" had been
written.

### Clean shutdown

Setting the stop flag is not enough on its own — threads parked in
`pthread_cond_timedwait` would only notice at their next timeout. `stop_sim`
therefore broadcasts on the arbitration condition variable after setting the
flag, so every waiter re-evaluates immediately, removes its request from the
queues and returns. This is what keeps `pthread_join` from hanging.

## Thread synchronization mechanisms

### Primitives and what each protects

| Primitive | Protects |
|---|---|
| `arb_lock` / `arb_cond` | All dongle availability state and all request queues |
| `state_lock` (one per coder) | `last_compile_start` and `compiles_done` |
| `log_lock` | Standard output |
| `stop_lock` | The simulation stop flag |
| `seq_lock` | The global request sequence counter |

### Why arbitration uses a single lock

Deciding whether a coder may proceed requires reading, atomically, the
availability of two dongles *plus* the pair-availability of every competing
requester. Those competitors' dongles are not the caller's own, so a per-dongle
mutex design would force either an unsynchronised read or an acquisition that
violates the ascending-id ordering — reintroducing the deadlock the ordering was
meant to prevent. Centralising the decision under one mutex makes the whole
arbitration snapshot consistent. Per-dongle state remains distinct; only the
decision is serialised.

### Lock ordering invariant

Nested acquisitions always follow:

```
arb_lock  →  stop_lock
log_lock  →  stop_lock
```

and never the reverse. `stop_lock` is a leaf: nothing is acquired while holding
it. This is why `stop_sim` releases `stop_lock` before taking `arb_lock` to
broadcast, and why `check_burnout` finishes printing under `log_lock` before
calling `stop_sim`. A single violation of this ordering between the monitor and
a coder thread is sufficient to deadlock the program.

### Preventing race conditions — worked examples

**`last_compile_start`.** The owning coder writes it the instant it begins
compiling; the monitor reads it to compute elapsed time; the arbitration code
reads it to compute an EDF deadline. All three accesses take that coder's
`state_lock`, and the value is copied out before use so the lock is held for a
few instructions only.

**`compiles_done`.** Incremented *after* the dongles are released, so the
monitor can never observe a coder as "finished a compile" while it still holds
resources.

**The heap.** Every push, pop, removal and comparison happens under `arb_lock`.
Since the requests themselves live on waiting threads' stacks, a request is
guaranteed to be dequeued before its owner leaves the function that declared it
— on both the success path and the shutdown path.

### Coder ↔ monitor communication

The two directions are deliberately asymmetric.

*Coders to monitor*: purely through per-coder state under `state_lock`. Coders
never signal the monitor; the monitor polls. This keeps the coders' hot path
free of any dependency on monitor liveness.

*Monitor to coders*: through the stop flag plus a broadcast on `arb_cond`. The
flag is the source of truth and the broadcast is only a wake-up hint, so a
missed or spurious wake-up cannot cause incorrect behaviour — every waiter
re-tests the flag and its own predicate in a loop.

### Broadcast, not signal

Every wake-up uses `pthread_cond_broadcast`. Under `edf` the winner of a queue
can change whenever a new request is enqueued, so a `signal` could wake an
arbitrary waiter — possibly one that still cannot proceed — while the coder that
should now win stays asleep. Broadcast forces every waiter to re-evaluate.

### The single-coder case

With `N == 1` there is one dongle, and `first` and `second` point at the same
object. A second acquisition would wait forever on a resource the caller itself
holds. This case is handled separately in `lone_coder`: the coder takes the one
dongle, logs it, never compiles, and burns out at `time_to_burnout` — which is
the behaviour the subject specifies.

## Resources

- [POSIX Threads Programming](https://hpc-tutorials.llnl.gov/posix/) — LLNL
  tutorial covering `pthread_create`, mutexes and condition variables.
- `man` pages for `pthread_cond_timedwait`, `pthread_mutex_lock`,
  `gettimeofday` and `clock_gettime`.
- Coffman, Elphick and Shoshani, *System Deadlocks* (1971) — the four necessary
  conditions for deadlock, and which one this project breaks.
- Dijkstra's original dining philosophers formulation, for the resource-ordering
  approach to circular wait.
- Valgrind's [Helgrind manual](https://valgrind.org/docs/manual/hg-manual.html) —
  used to check for data races and lock-order inversions.

### Testing notes

`helgrind` reports no data races and no lock-order inversions. On the cluster's
Valgrind 3.18 it emits six `pthread_cond_{signal,broadcast}: dubious` warnings,
all originating inside glibc's own `pthread_cond_timedwait` implementation
rather than in project code; these do not appear under Valgrind 3.25, which has
updated glibc suppressions. `memcheck` reports 12 allocations, 12 frees, no
leaks and no errors.

### Use of AI

Consulting, discussion and clarification of concepts and and implementeions.