odexion — The Incremental Build Guide

> **How this guide works.** Nothing is shown finished. Every file — the
> header included — grows one piece at a time, and each piece appears at
> the moment something needs it. For each increment you see what the file
> looked like before, the piece added and the reason for it, and the file
> after.
>
> Concurrency has a vocabulary. Terms like *deadlock*, *starvation*,
> *liveness* and *race condition* have precise meanings, and using them
> loosely is how people fail defences. So each term is defined in a
> **CONCEPT** box the first time it is needed, before any code uses it.
>
> Codexion has one property a sequential project does not: **a wrong
> design still compiles, still passes Norminette, and still appears to
> work.** Three modules are therefore shown in successive versions — v1,
> v2, v3 — each with the log output that disproved it. The dead ends are
> the content, not filler.

---

## Contents

- [Step 0 · Skeleton and toolchain](#step-0)
- [Module A · The priority queue](#module-a)
- [Module B · Time and serialised output](#module-b)
- [Module C · The entities and the ring](#module-c)
- [Module D · Acquisition v1 — one dongle at a time *(fails)*](#module-d)
- [Module E · Acquisition v2 — atomic pairs *(fails differently)*](#module-e)
- [Module F · Acquisition v3 — arbitration by eligibility](#module-f)
- [Module G · The coder thread](#module-g)
- [Module H · The monitor thread](#module-h)
- [Module I · Parse, init, run, cleanup](#module-i)
- [Step Z · Validation](#step-z)
- [Appendix · Glossary, principles, defence notes, pitfalls](#appendix)

Legend:

```
  ┌ CONCEPT ┐   a term defined before it is used
  ┌ SO FAR  ┐   the file before this increment
  ┌  ADD    ┐   the new piece, in isolation, with the reason
  ┌ RESULT  ┐   the file after the piece is folded in
  ┌ BREAKS  ┐   the log output that disproved this version
```

**The problem in one paragraph.** `N` coders sit in a ring with `N`
dongles between them. Each coder cycles compile → debug → refactor.
Compiling needs the two dongles adjacent to that coder, held at the same
time. A coder that does not *start* a compile within `time_to_burnout` ms
of its last one burns out and the simulation ends. Released dongles are
unavailable for `dongle_cooldown` ms. When several coders want the same
dongle, a scheduler (`fifo` or `edf`) decides who gets it. A monitor
thread must report a burnout within 10 ms of it happening.

---

## Step 0

### Skeleton and toolchain

Goal: a program that builds, so every increment after this can be
compiled and Norminette-checked in seconds.

**ADD** — three files.

```c
/* codexion.h */
#ifndef CODEXION_H
# define CODEXION_H

# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <pthread.h>
# include <sys/time.h>

#endif
```

```c
/* codexion.c */
#include "codexion.h"

int	main(int ac, char **av)
{
	(void)ac;
	(void)av;
	return (0);
}
```

```make
NAME = codexion
SRCS = codexion.c
HDRS = codexion.h
OBJS = $(SRCS:.c=.o)

CC = cc
CFLAGS = -Wall -Wextra -Werror -pthread

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
```

**RESULT** — `make` produces a program that does nothing, correctly.

```
┌ CONCEPT ┐  Translation unit
```

A **translation unit** is one `.c` file plus everything its `#include`
directives pull in. The compiler processes each one in complete isolation
and emits one `.o`. It has no knowledge of any other `.c` file.

Three consequences that cause most early errors in this project:

1. **Every `.c` must `#include "codexion.h"` itself.** Including it in
   one file does not make its declarations visible in another.
2. **A function *body* in a header is copied into every translation unit
   that includes it**, producing one definition of the symbol per unit
   and a `multiple definition` error at link time. Rule: *braces go in
   `.c`, semicolons go in `.h`.*
3. **The compiler trusts declarations.** If the header says a function
   exists, the `.c` compiles. Whether the body exists anywhere is only
   discovered by the linker, which reports `undefined reference`.

```
┌ CONCEPT ┐  Compiling vs linking
```

**Compiling** turns one translation unit into machine code with
unresolved references. **Linking** joins the `.o` files and resolves
those references. Errors from the two stages read differently:

| stage | example message | meaning |
|---|---|---|
| compile | `unknown type name 't_heap'` | this file never saw the type |
| compile | `implicit declaration of 'heap_init'` | this file never saw the prototype |
| link | `undefined reference to 'pair_wake'` | prototype exists, body does not |
| link | `multiple definition of 'now_ms'` | body exists in more than one unit |

Two details in the Makefile are load-bearing:

**`$(CFLAGS)` on both rules.** `-pthread` acts in *both* stages. At
compile time it defines `_REENTRANT`, which changes libc headers — most
importantly making `errno` thread-local rather than a shared global. At
link time it pulls in threading support. On glibc ≥ 2.34 `libpthread` was
merged into `libc`, so omitting it from the link line still links on your
machine and fails on someone else's.

**`%.o: %.c $(HDRS)`.** `make` decides what to rebuild by comparing
timestamps against the listed dependencies. Without the header listed,
editing a struct rebuilds nothing. Half your `.o` files then use the old
memory layout: one writes `compiles_done` at offset 24, another reads it
at offset 28. This compiles without a warning and behaves exactly like a
race condition. You will hunt a synchronisation bug that does not exist.

---

## Module A

### The priority queue

```
┌ CONCEPT ┐  Priority queue
```

A **priority queue** is a container with three operations: insert an
element, read the element of highest priority, and remove it. Unlike a
FIFO queue, insertion order does not determine removal order — the
comparison function does.

Codexion needs one per dongle. When several coders want the same dongle,
the queue answers "who should get it". Changing the comparison function
changes the scheduling policy without touching anything else.

```
┌ CONCEPT ┐  Binary heap
```

A **binary heap** is a priority queue stored as an array that represents
a complete binary tree. For the element at index `i`:

```
    parent(i) = (i - 1) / 2
      left(i) = 2i + 1
     right(i) = 2i + 2
```

The **heap property** is the invariant that every parent has higher
priority than both of its children. This is weaker than full sorting —
siblings are unordered — which is why insertion and removal cost
`O(log n)` rather than `O(n log n)`, while the highest-priority element
is always at index 0.

Restoring the property after a change is done by **sifting**: moving one
element up or down until it sits between its parent and its children.

The subject forbids using a standard library priority queue, so this is
hand-rolled. Ten functions, and Norminette allows five per file, so two
files: `heap.c` and `heap_ops.c`.

> **Build this first, and test it with no threads running.** It is pure,
> deterministic and conventionally debuggable. Leave it until later and
> you will be trying to distinguish "my heap is wrong" from "I have a
> race" inside a concurrent program.

### A.1 — the header learns about requests

**SO FAR** — the header has includes only.

**ADD** — the element the queue will hold. Start with the minimum: who
made the request, and the two values the two policies order by.

```c
typedef struct s_request
{
	int		coder_id;
	long	deadline;
	long	seq;
}	t_request;
```

**RESULT**

```c
#ifndef CODEXION_H
# define CODEXION_H

# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <pthread.h>
# include <sys/time.h>

typedef struct s_request
{
	int		coder_id;
	long	deadline;
	long	seq;
}	t_request;

#endif
```

- `seq` — a counter incremented once per request, globally. Ordering by
  it *is* first-in-first-out.
- `deadline` — `last_compile_start + time_to_burnout`, the instant this
  coder burns out if it does not start compiling. Ordering by it *is*
  earliest-deadline-first.
- `coder_id` — the final tie-breaker.

Two more fields will be added to this struct later, each when a specific
problem demands it. Adding them now would mean explaining them now,
before the problem exists.

### A.2 — the header learns about the queue

**SO FAR** — the header has `t_request`.

**ADD** — the container.

```c
typedef struct s_heap
{
	t_request	**items;
	int			size;
	int			capacity;
	int			policy;
}	t_heap;
```

**RESULT** — appended directly below `t_request`.

```
┌ CONCEPT ┐  Storing pointers instead of values
```

`items` is `t_request **` — an array of *pointers to* requests, not an
array of requests. The queue does not own the elements; something else
does.

That choice has two consequences, both of which the design later
depends on:

1. **No allocation per request.** Every `t_request` will live on the
   stack of the thread that is waiting. A thread waiting for a dongle is
   parked inside a function; its local variables persist for exactly as
   long as the wait. No `malloc`, no `free`, no leak possible.
2. **Identity comparison becomes meaningful.** `heap_peek(h) == &my_req`
   asks "is the front of the queue *my* request?" — a pointer comparison,
   not a field-by-field match. Two requests with identical contents are
   still distinguishable.

`policy` lives inside the heap rather than being passed at every call
because a queue's ordering never changes at runtime.

Also add the policy constants, just below the includes:

```c
# define FIFO 0
# define EDF 1
```

### A.3 — allocation

**SO FAR** — `heap.c` contains only `#include "codexion.h"`.

**ADD**

```c
int	heap_init(t_heap *h, int capacity, int policy)
{
	h->items = malloc(sizeof(t_request *) * capacity);
	if (!h->items)
		return (0);
	h->size = 0;
	h->capacity = capacity;
	h->policy = policy;
	return (1);
}

void	heap_destroy(t_heap *h)
{
	free(h->items);
	h->items = NULL;
	h->size = 0;
	h->capacity = 0;
}
```

**RESULT** — a heap can be created and released. Add the prototypes to
the header under a `// HEAP` comment.

Setting `items = NULL` after freeing makes `heap_destroy` **idempotent**
— calling it twice is harmless, because `free(NULL)` is defined to do
nothing. This matters when initialisation fails half-way and cleanup runs
over a partially built structure.

**On capacity.** Passing `n_coders` is safe. The true bound is **2**:
dongle `i` sits between coder `i` and coder `i+1`, so at most two coders
can ever queue for it. Knowing that bound is a good defence answer;
keeping the slack costs 8 bytes per dongle and protects against a silent
hang if `heap_push` ever returned 0.

### A.4 — the comparator, where both policies live

**SO FAR** — init and destroy.

**ADD**

```c
int	heap_less(t_request *a, t_request *b, int policy)
{
	if (policy == FIFO)
		return (a->seq < b->seq);
	if (a->deadline != b->deadline)
		return (a->deadline < b->deadline);
	if (a->seq != b->seq)
		return (a->seq < b->seq);
	return (a->coder_id < b->coder_id);
}
```

**RESULT** — one function is the entire difference between `fifo` and
`edf`. This is what to point at when asked "where is the scheduler?".

```
┌ CONCEPT ┐  Total order and why ties matter
```

A comparison defines a **partial order** if some pairs of elements are
incomparable, and a **total order** if every pair of distinct elements
can be ranked — for any distinct `a` and `b`, exactly one of
`a < b` or `b < a` holds.

Ordering by deadline alone is only partial: two coders can share a
deadline, and then neither is "less" than the other. Consequences:

- The heap's behaviour becomes **implementation-defined** — which of two
  tied elements surfaces depends on insertion order and array layout.
  The same input can produce different output on different runs.
- The phrase "the highest-priority request" stops naming a unique
  request, and any argument that relies on it collapses. Module F's
  liveness proof relies on exactly that phrase.

The fallback chain deadline → seq → id makes the order total. `seq` is
unique by construction (one global counter), so the chain always
terminates in a decision. The subject's requirement for a tie-breaker
rule is asking for precisely this.

### A.5 — swap, and a new field in the header

**SO FAR** — the heap has no way to remove an element that is not at the
front. It will need one: a coder whose request is queued when the
simulation stops must withdraw it from the middle of the queue.

Finding an element by scanning is `O(n)` and, worse, requires the caller
to know which heap it is in. Storing the position inside the element
makes removal `O(log n)` and self-locating.

**ADD** — to `t_request` in the header:

```c
	int		idx;
```

**RESULT** — the request now knows where it lives:

```c
typedef struct s_request
{
	int		coder_id;
	long	deadline;
	long	seq;
	int		idx;
}	t_request;
```

**ADD** — to `heap.c`:

```c
void	heap_swap(t_heap *h, int i, int j)
{
	t_request	*tmp;

	tmp = h->items[i];
	h->items[i] = h->items[j];
	h->items[j] = tmp;
	h->items[i]->idx = i;
	h->items[j]->idx = j;
}

t_request	*heap_peek(t_heap *h)
{
	if (h->size == 0)
		return (NULL);
	return (h->items[0]);
}
```

**RESULT** — `heap.c` is complete at five functions.

```
┌ CONCEPT ┐  Invariant
```

An **invariant** is a condition that must be true before and after every
operation, even though operations may break it temporarily inside
themselves.

This module has two:

- **Heap property**: every parent outranks its children. Broken by
  `heap_push` inserting at the end; restored by `heap_up`.
- **Index correspondence**: for every element,
  `h->items[r->idx] == r`. Broken by any pointer move; restored
  immediately.

The second is the one that bites. **Every operation that moves a pointer
within the array must update that element's `idx`.** Swap does it here;
push sets it for the new element; pop fixes it for the promoted element;
remove fixes it for the element pulled in from the end.

Miss one and `heap_remove` will remove the *wrong* request — and only for
certain arrival orders, which means intermittently. Putting the update
inside `heap_swap` rather than at each call site makes the invariant hold
by construction: it is impossible to swap without fixing the indices.

### A.6 — sifting

**SO FAR** — `heap_ops.c` contains only the include.

**ADD**

```c
void	heap_up(t_heap *h, int i)
{
	int	parent;

	while (i > 0)
	{
		parent = (i - 1) / 2;
		if (!heap_less(h->items[i], h->items[parent], h->policy))
			return ;
		heap_swap(h, i, parent);
		i = parent;
	}
}

void	heap_down(t_heap *h, int i)
{
	int	best;
	int	c;

	best = i;
	c = 2 * i + 1;
	while (c < h->size)
	{
		if (heap_less(h->items[c], h->items[best], h->policy))
			best = c;
		c++;
		if (c < h->size && heap_less(h->items[c], h->items[best], h->policy))
			best = c;
		if (best == i)
			return ;
		heap_swap(h, i, best);
		i = best;
		c = 2 * i + 1;
	}
}
```

**RESULT** — the array can restore the heap property in either direction.

`heap_up` moves an element toward the root while it outranks its parent.
`heap_down` moves an element toward the leaves while a child outranks it,
always swapping with the *better* of the two children — swapping with the
worse one would break the property against the other.

Both are **iterative**. Recursion reads more cleanly but Norminette's
25-line limit and bounded stack usage make the loop the better fit.

The `c++` between the two child checks tests left then right without a
second index variable. It is the one line a reader stumbles on; if you
prefer, name them `left` and `right` and spend the extra variable.

### A.7 — push and pop

**ADD**

```c
int	heap_push(t_heap *h, t_request *r)
{
	if (h->size >= h->capacity)
		return (0);
	h->items[h->size] = r;
	r->idx = h->size;
	h->size++;
	heap_up(h, h->size - 1);
	return (1);
}

t_request	*heap_pop(t_heap *h)
{
	t_request	*top;

	if (h->size == 0)
		return (NULL);
	top = h->items[0];
	h->size--;
	if (h->size > 0)
	{
		h->items[0] = h->items[h->size];
		h->items[0]->idx = 0;
		heap_down(h, 0);
	}
	top->idx = -1;
	return (top);
}
```

**RESULT** — a working priority queue.

Push appends at the end (the only position that keeps the tree complete)
and sifts up. Pop takes index 0, moves the last element into the hole,
and sifts down.

`top->idx = -1` marks the request as "in no heap". `heap_remove` uses
that sentinel as a guard against double removal.

### A.8 — removal from the middle

**SO FAR** — a conventional heap.

**ADD** — the operation textbook heaps omit, and the reason `idx` exists:

```c
void	heap_remove(t_heap *h, t_request *r)
{
	int	i;

	i = r->idx;
	if (i < 0 || i >= h->size || h->items[i] != r)
		return ;
	h->size--;
	if (i != h->size)
	{
		h->items[i] = h->items[h->size];
		h->items[i]->idx = i;
		heap_up(h, i);
		heap_down(h, i);
	}
	r->idx = -1;
}
```

**RESULT** — `heap_ops.c` complete at five functions.

Two subtleties:

**The triple guard.** `idx` in range *and* `items[idx] == r`. The pointer
check catches a stale index that happens to be in bounds — the exact
symptom of a missed `idx` update elsewhere. It converts silent corruption
into a no-op, which is the difference between a bug you can find and one
you cannot.

**Both `heap_up` and `heap_down` are called.** The element dragged in
from the end may outrank the parent at that position (must rise) or be
outranked by a child (must sink). You cannot know which without
comparing, and exactly one of the two calls will do work while the other
returns immediately. Calling only `heap_down` is a classic bug that
survives most casual testing, because the failing case needs a specific
shape of tree.

### A.9 — test it now, before any thread exists

**ADD** — a throwaway `main` in a scratch file:

```c
int	main(void)
{
	t_heap		h;
	t_request	r[6];
	t_request	*p;
	long		d[6];
	int			i;

	d[0] = 500; d[1] = 100; d[2] = 900; d[3] = 300; d[4] = 100; d[5] = 700;
	heap_init(&h, 16, EDF);
	i = 0;
	while (i < 6)
	{
		r[i].coder_id = i;
		r[i].seq = i;
		r[i].deadline = d[i];
		r[i].idx = -1;
		heap_push(&h, &r[i]);
		i++;
	}
	heap_remove(&h, &r[3]);
	while ((p = heap_pop(&h)))
		printf("id=%d deadline=%ld\n", p->coder_id, p->deadline);
	heap_destroy(&h);
	return (0);
}
```

**RESULT** — three things must hold:

- With `EDF`: deadlines emerge as `100, 100, 500, 700, 900`.
- Between the two 100s, coder 1 before coder 4 — the `seq` tie-breaker
  working.
- With `FIFO`: ids emerge as `0, 1, 2, 4, 5`.
- In both: the removed element (id 3) never appears.

Ten minutes here saves a night later. Delete the scratch file before
committing — Norminette checks every `.c` in the folder, and two `main`
functions will not link.
---

## Module B

### Time and serialised output

Everything here is short, but two of these functions run tens of
thousands of times per second, so their locking shape matters.

```
┌ CONCEPT ┐  Race condition
```

A **race condition** is a defect where the result depends on the relative
timing of threads. The canonical case is `x++` on a shared variable: it
compiles to *read, add, write*, and two threads interleaving those six
steps can lose an increment entirely.

Reading is not safe either. An unsynchronised read of a variable another
thread writes is **undefined behaviour** in C — not "usually fine". The
compiler is permitted to assume no other thread exists, and may hoist the
read out of a loop, turning `while (!stop) { ... }` into an infinite
loop. This optimisation typically appears at `-O2` and not at `-O0`,
which is why it surfaces on the evaluator's machine and not yours.

```
┌ CONCEPT ┐  Critical section and mutex
```

A **critical section** is a region of code that must not run
concurrently with itself. A **mutex** (mutual exclusion lock) enforces
that: at most one thread holds it at a time; others block at
`pthread_mutex_lock` until it is released.

The rule that follows: **every access to shared mutable state, read or
write, happens inside the same critical section.** Protecting only writes
is not protection.

The rule that follows *that*: **critical sections should be short and
must never contain a blocking operation** you do not control. A thread
holding a lock while sleeping blocks every other thread that needs it.

### B.1 — the header learns about the simulation

**SO FAR** — the header has `t_request` and `t_heap`.

**ADD** — a structure for everything shared. Start with only what this
module needs: an origin for timestamps, a stop flag, a sequence counter,
and the locks guarding them.

```c
typedef struct s_sim
{
	long			start_ms;
	int				stop;
	long			seq_counter;
	pthread_mutex_t	stop_lock;
	pthread_mutex_t	seq_lock;
	pthread_mutex_t	log_lock;
}	t_sim;
```

**RESULT** — appended below `t_heap`.

Configuration fields (`n_coders`, the durations, the policy) and the
entity arrays are absent on purpose: nothing in this module reads them.
They arrive in Module C.

> **Note on `t_sim`'s final form.** In Module C this becomes a forward
> typedef plus `struct s_sim { ... };`, because `t_coder` will need a
> pointer to it before it is defined. Until that need exists, the plain
> form is correct.

`start_ms` exists so log lines can print *elapsed* milliseconds rather
than a Unix timestamp — the subject's example output starts at 0.

Three separate mutexes rather than one, because the three pieces of state
are independent. A coder reading the stop flag has no reason to wait
behind a coder printing a line.

### B.2 — the clock

**ADD** — `time.c`:

```c
long	now_ms(void)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);
	return ((long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

long	elapsed(t_sim *sim)
{
	return (now_ms() - sim->start_ms);
}
```

**RESULT** — absolute and relative time in milliseconds.

The `(long)` cast on `tv_sec` before multiplying is not paranoia: on a
32-bit `time_t`, `tv_sec * 1000` overflows before the cast is applied.
Cast first, multiply second.

```
┌ CONCEPT ┐  Wall-clock vs monotonic time
```

`CLOCK_REALTIME` (which `gettimeofday` reads) is **wall-clock** time: it
tracks calendar time and can jump when NTP corrects it or an
administrator sets the clock. `CLOCK_MONOTONIC` counts steadily from an
arbitrary origin and never jumps.

For measuring *durations*, monotonic is technically correct. This project
uses `gettimeofday` because `pthread_cond_timedwait` takes its deadline
on `CLOCK_REALTIME` by default, so the same number can be used for both
with no conversion.

If you switch `now_ms` to `clock_gettime(CLOCK_MONOTONIC, ...)` — which
the subject permits — you **must** also switch the condition variable via
`pthread_condattr_setclock`. Changing one and not the other produces
deadlines decades in the past or future. Both clocks are compared against
each other in Module H, for a reason.

### B.3 — guarded reads

**ADD**

```c
int	sim_stopped(t_sim *sim)
{
	int	s;

	pthread_mutex_lock(&sim->stop_lock);
	s = sim->stop;
	pthread_mutex_unlock(&sim->stop_lock);
	return (s);
}

long	next_seq(t_sim *sim)
{
	long	s;

	pthread_mutex_lock(&sim->seq_lock);
	s = sim->seq_counter++;
	pthread_mutex_unlock(&sim->seq_lock);
	return (s);
}
```

**RESULT** — two of `time.c`'s five functions. A third,
`coder_deadline`, arrives in Module C once `t_coder` exists.

Both follow the same shape: **lock, copy into a local, unlock, return the
local.** The critical section is a handful of instructions and contains
nothing that can block. This is what keeps `stop_lock` — hammered by
every thread in its inner loop — from becoming a bottleneck.

`next_seq` is where FIFO order is created. `seq_counter++` under a mutex
guarantees every request gets a distinct number, which is what makes the
comparison chain in A.4 terminate.

### B.4 — serialised logging

**ADD** — `log.c`:

```c
void	log_state(t_coder *c, char *msg)
{
	pthread_mutex_lock(&c->sim->log_lock);
	if (!sim_stopped(c->sim))
		printf("%ld %d %s\n", elapsed(c->sim), c->id, msg);
	pthread_mutex_unlock(&c->sim->log_lock);
}
```

*(`t_coder` arrives in Module C; write this file then, or stub the
parameter for now.)*

**RESULT** — every state change goes through one funnel.

Four lines, two mechanisms:

**The mutex is held across the whole `printf`.** `printf` is not atomic —
two threads calling it concurrently can interleave mid-line. Holding the
lock for the duration is what satisfies the subject's "a displayed state
message should not be mixed up with another message".

**The stop check is *inside* the critical section.** A coder can decide
to log "is compiling", get descheduled, and resume after the monitor has
already printed "burned out". With the check inside the lock, that thread
finds the flag set and prints nothing. With the check outside, state
lines appear after the burnout line.

```
┌ CONCEPT ┐  Lock ordering
```

When a thread holds lock A and requests lock B while another holds B and
requests A, neither can proceed. This is **lock-order inversion**, and it
deadlocks.

The standard prevention is a **global lock order**: a fixed sequence
that every thread respects. If all threads acquire locks in the same
order, no cycle can form.

`log_state` establishes the project's first ordering constraint:

```
log_lock  →  stop_lock
```

because `sim_stopped` takes `stop_lock` while `log_lock` is held. Write
it down. Module F adds `arb_lock → stop_lock`, and Module H must obey
both or the monitor and a coder will deadlock against each other.

The resulting discipline: **`stop_lock` is a leaf.** Nothing is ever
acquired while holding it.

---

## Module C

### The entities and the ring

```
┌ CONCEPT ┐  The ring topology
```

`N` coders and `N` dongles alternate around a circle. Dongle `i` sits
between coder `i` and coder `i+1`, so:

- each coder is adjacent to exactly **two** dongles;
- each dongle is adjacent to exactly **two** coders.

The second fact bounds every queue at two entries, and makes the scan in
Module F trivially cheap.

For an **even** `N`, coders split into two alternating groups that can
compile simultaneously without conflict — the graph is 2-colourable, and
at most `N/2` coders compile at once. For an **odd** `N` it is not
2-colourable, so the ring needs three phases and throughput is lower.
This is not a defect; it is the topology. Module Z quantifies it.

### C.1 — the header learns about dongles

**SO FAR** — the header has `t_request`, `t_heap`, `t_sim`.

**ADD** — the shared resource.

```c
typedef struct s_dongle
{
	int		id;
	int		available;
	long	available_at;
	t_heap	queue;
}	t_dongle;
```

**RESULT** — appended below `t_heap`, above `t_sim`.

`t_heap` is embedded **by value**, so it must be fully defined above this
point. A forward declaration would not help: the compiler needs the size
to lay out the struct. Ordering the header correctly beats
forward-declaring wherever possible.

**Why two fields for one idea.** `available` means "nobody holds it".
`available_at` is the instant its cooldown expires. A dongle can be
available and still not grantable. Collapsing them into one flag makes
the cooldown rule inexpressible.

> **Note.** In versions v1 and v2 this struct also carried a
> `pthread_mutex_t` and a `pthread_cond_t`. Module F explains why they
> moved out. If you are building along, add them in Module D.

### C.2 — the header learns about coders

**SO FAR** — as above.

**ADD** — one per thread. This struct needs a pointer to `t_sim`, and
`t_sim` will need a pointer to `t_coder`, so the cycle must be broken
with forward typedefs at the top of the header:

```c
typedef struct s_coder	t_coder;
typedef struct s_sim	t_sim;
```

and `t_sim`'s definition changes from `typedef struct s_sim {...} t_sim;`
to `struct s_sim { ... };`.

```c
typedef struct s_coder
{
	int				id;
	pthread_t		thread;
	long			last_compile_start;
	int				compiles_done;
	pthread_mutex_t	state_lock;
	t_dongle		*first;
	t_dongle		*second;
	t_sim			*sim;
}	t_coder;
```

**RESULT** — the header now has all five types.

```
┌ CONCEPT ┐  Forward declaration and incomplete types
```

A type is **incomplete** when the compiler knows its name but not its
size. You may declare *pointers* to an incomplete type, because all
pointers are the same size; you may not embed one by value or dereference
it.

That is why exactly **two** forward typedefs are needed here, not five:
`t_request → t_coder` and `t_coder → t_sim` are pointer references, so a
forward declaration suffices. `t_dongle` embeds `t_heap` by value and
`t_coder` embeds a `pthread_mutex_t` by value, so those must be complete
— which correct ordering already achieves.

> **Consistency note.** Once `t_coder` is forward-typedef'd, its body
> should be written `struct s_coder { ... };` — matching `s_sim`. Writing
> `typedef struct s_coder { ... } t_coder;` produces a *duplicate typedef*
> of the same type. C11 permits it, gcc accepts it silently and
> Norminette passes, so it is easy to leave in by accident. It is
> redundant, and the asymmetry against `s_sim` is the kind of thing an
> evaluator notices.

Field by field:

- **`state_lock` protects exactly two fields**: `last_compile_start` and
  `compiles_done`. Both are written by the owning coder and read by the
  monitor. One lock per coder rather than one global, because coder 3's
  counter is genuinely independent of coder 5's.
- **`first` and `second` are not left and right.** They are the coder's
  two dongles *sorted by id*. That sorting is the deadlock-prevention
  mechanism (Module D); putting it in the struct means the acquisition
  code never has to think about it.
- **`sim` is a back-pointer.** `pthread_create` passes a single
  `void *`, so this is how a coder thread reaches shared configuration —
  without a global variable, which the subject forbids outright.

### C.3 — the header learns the configuration

**ADD** — the remaining fields of `struct s_sim`:

```c
struct s_sim
{
	int				n_coders;
	long			t_burnout;
	long			t_compile;
	long			t_debug;
	long			t_refactor;
	int				must_compile;
	long			cooldown;
	int				policy;
	long			start_ms;
	int				stop;
	long			seq_counter;
	pthread_mutex_t	stop_lock;
	pthread_mutex_t	seq_lock;
	pthread_mutex_t	log_lock;
	t_dongle		*dongles;
	t_coder			*coders;
};
```

**RESULT** — the header describes the whole program state. One more
mutex and one condition variable arrive in Module F.

The configuration fields are written once, before any thread starts, and
only read afterwards. Read-only-after-creation data needs no lock — a
useful category to recognise, and a good answer when asked why
`t_compile` is read without one.

### C.4 — the third time function

**ADD** — to `time.c`, now that `t_coder` exists:

```c
long	coder_deadline(t_coder *c)
{
	long	d;

	pthread_mutex_lock(&c->state_lock);
	d = c->last_compile_start + c->sim->t_burnout;
	pthread_mutex_unlock(&c->state_lock);
	return (d);
}
```

**RESULT** — `time.c` has four of its five functions. This is the bridge
between the coder's state and the EDF ordering: it computes the value
that goes into `t_request.deadline`.

---

## Module D

### Acquisition v1 — one dongle at a time  *(this version is wrong)*

The natural first design. Understanding why it fails is most of what this
project teaches.

```
┌ CONCEPT ┐  Deadlock and Coffman's four conditions
```

**Deadlock** is a state in which a set of threads are all blocked, each
waiting for a resource held by another in the set, so none can ever
proceed. Coffman, Elphick and Shoshani (1971) showed deadlock requires
**all four** of these simultaneously:

1. **Mutual exclusion** — a resource is held by at most one thread.
2. **Hold and wait** — a thread holding a resource can request another.
3. **No pre-emption** — a resource is released only voluntarily.
4. **Circular wait** — a cycle exists in the "waits for" graph.

Breaking any one prevents deadlock. In this project:

- (1) is the problem statement — a dongle cannot be shared.
- (3) would mean forcibly reclaiming a dongle mid-compile, which the
  simulation semantics forbid.
- (4) is what v1 breaks, by imposing a resource ordering.
- (2) is what v2 breaks, and it is the one that actually matters here.

```
┌ CONCEPT ┐  Resource ordering
```

Assign every resource a unique number and require threads to acquire in
increasing order. A cycle in the wait-for graph would need some thread
waiting for a lower-numbered resource while holding a higher-numbered
one, which the rule forbids. So condition (4) cannot hold.

### D.1 — the dongle gains synchronisation

**ADD** — to `t_dongle` in the header:

```c
	pthread_mutex_t	lock;
	pthread_cond_t	cond;
```

**RESULT** — in v1, each dongle owns its mutex, its condition variable
and its queue.

```
┌ CONCEPT ┐  Condition variable
```

A mutex answers "may I touch this state?". A **condition variable**
answers "may I wait until this state becomes what I need?".

`pthread_cond_wait(&cond, &mutex)` does three things atomically: release
the mutex, block, and — when woken — re-acquire the mutex before
returning. The atomicity of release-and-block is the point: without it, a
signal arriving between the two would be lost.

Three rules that follow, all of which this project depends on:

- **Always wait inside a `while`, never an `if`.** A woken thread must
  re-check its predicate, because (a) **spurious wake-ups** are permitted
  by POSIX, and (b) another thread may have consumed the condition
  between the wake-up and this thread re-acquiring the mutex.
- **The mutex must be held when calling wait**, and is held again on
  return. This is invisible in a function signature, so document it.
- **`signal` wakes one arbitrary waiter; `broadcast` wakes all.** Use
  `signal` only when any waiter will do.

### D.2 — the grant predicate

**ADD** — `dongle_utils.c`:

```c
void	ms_to_timespec(long target_ms, struct timespec *ts)
{
	ts->tv_sec = target_ms / 1000;
	ts->tv_nsec = (target_ms % 1000) * 1000000;
}

long	wake_time(t_dongle *d, long now)
{
	if (d->available && d->available_at > now)
		return (d->available_at);
	return (now + 1);
}

int	can_take(t_dongle *d, t_request *req, long now)
{
	return (d->available && now >= d->available_at
		&& heap_peek(&d->queue) == req);
}
```

**RESULT** — the subject's rule in three conjuncts: the dongle is free,
its cooldown has expired, and **I am at the head of the queue**.

That third clause is the arbitration. It is also what breaks in Module E,
so read it carefully now.

`wake_time` returns the cooldown expiry when there is one to wait for,
and `now + 1` otherwise — a 1 ms poll for the case where the dongle is
held by someone else and no cooldown deadline applies.

### D.3 — waiting for a turn

**ADD** — `dongle.c`:

```c
void	request_init(t_coder *c, t_request *req)
{
	req->coder_id = c->id;
	req->seq = next_seq(c->sim);
	req->deadline = coder_deadline(c);
	req->idx = -1;
}

int	wait_turn(t_coder *c, t_dongle *d, t_request *req)
{
	struct timespec	ts;
	long			now;

	now = now_ms();
	while (!sim_stopped(c->sim) && !can_take(d, req, now))
	{
		ms_to_timespec(wake_time(d, now), &ts);
		pthread_cond_timedwait(&d->cond, &d->lock, &ts);
		now = now_ms();
	}
	return (!sim_stopped(c->sim));
}
```

**RESULT** — a coder sleeps until the cooldown expires or someone
signals.

```
┌ CONCEPT ┐  Timed waiting, and why plain wait is wrong here
```

`pthread_cond_wait` blocks until another thread signals. That works when
some thread is *responsible* for making the condition true.

The end of a cooldown has no such thread. It is a **temporal event**:
the condition becomes true because time passed, and no thread is
scheduled to notice. A plain wait would sleep until some unrelated
release happened to wake it, which may never occur.

`pthread_cond_timedwait` takes an **absolute** deadline — a point in
time, not a duration. `wake_time` computes exactly when the state could
next change, so the thread sleeps no longer than necessary and no shorter.

The pre-condition of `wait_turn` is invisible in its signature: it must
be called with `d->lock` held and returns with it still held. That is how
condition variables work, and it is exactly the contract a future
refactor breaks. Write it in a comment.

### D.4 — acquire and release

**ADD**

```c
int	dongle_acquire(t_coder *c, t_dongle *d)
{
	t_request	req;

	request_init(c, &req);
	pthread_mutex_lock(&d->lock);
	heap_push(&d->queue, &req);
	pthread_cond_broadcast(&d->cond);
	if (!wait_turn(c, d, &req))
	{
		heap_remove(&d->queue, &req);
		pthread_mutex_unlock(&d->lock);
		return (0);
	}
	heap_pop(&d->queue);
	d->available = 0;
	pthread_mutex_unlock(&d->lock);
	log_state(c, "has taken a dongle");
	return (1);
}

void	dongle_release(t_dongle *d, t_sim *sim)
{
	pthread_mutex_lock(&d->lock);
	d->available = 1;
	d->available_at = now_ms() + sim->cooldown;
	pthread_cond_broadcast(&d->cond);
	pthread_mutex_unlock(&d->lock);
}
```

**RESULT** — `t_request req` is a **local variable**. It lives exactly as
long as the thread is inside this function, and every exit path removes
it from the queue first: `heap_pop` on success, `heap_remove` on
shutdown. No dangling pointer, no allocation.

**Why `broadcast` and not `signal`?** Under EDF the head of a queue
changes whenever a new request arrives with a tighter deadline. `signal`
wakes one arbitrary waiter — possibly one that still cannot proceed —
while the coder that should now win stays asleep. `broadcast` forces
everyone to re-evaluate. The broadcast after `heap_push` exists for
exactly that case.

### D.5 — resource ordering at init

**ADD** — in `init.c`:

```c
void	assign_dongles(t_sim *sim, int i)
{
	t_dongle	*left;
	t_dongle	*right;

	left = &sim->dongles[i];
	right = &sim->dongles[(i + 1) % sim->n_coders];
	if (left->id < right->id)
	{
		sim->coders[i].first = left;
		sim->coders[i].second = right;
	}
	else
	{
		sim->coders[i].first = right;
		sim->coders[i].second = left;
	}
}
```

**RESULT** — every coder requests `first` then `second`, which is
ascending id order. The `%` wraparound means exactly one coder — the last
— gets the reversed pair. That single asymmetry is what breaks the cycle.

Coffman condition (4) is now impossible. There is no deadlock. Run it:

**BREAKS** — `./codexion 5 800 200 200 200 5 0 fifo`

```
1 1 has taken a dongle      ← coder 1 holds d0, waits for d1
1 2 has taken a dongle      ← coder 2 holds d1, waits for d2
2 3 has taken a dongle      ← coder 3 holds d2, waits for d3
2 4 has taken a dongle
2 4 has taken a dongle      ← only coder 4 completes a pair
2 4 is compiling
202 4 is debugging
202 3 has taken a dongle
202 3 is compiling          ← the chain unwinds, 200 ms per step
402 2 has taken a dongle
402 2 is compiling
602 1 has taken a dongle
602 1 is compiling
801 5 burned out            ← coder 5 waited 800 ms for d0
```

### D.6 — diagnosis

Nothing is stuck. The chain unwinds: coder 3 compiles at 202, coder 2 at
402, coder 1 at 602. Deadlock prevention worked.

Coder 5 burns out anyway.

The cause is condition **(2), hold-and-wait** — untouched. Coders 1, 2
and 3 each took one dongle and then sat *holding* it while waiting for
the second. Coder 5 needed d0, which coder 1 had held, unused, since
millisecond 1.

Instead of two or three coders compiling concurrently, exactly one made
progress at a time. The ring took 600 ms to pass the resources around,
and the deadline was 800.

```
┌ CONCEPT ┐  Safety vs liveness
```

A **safety** property says *nothing bad ever happens* — no deadlock, no
two coders holding the same dongle. It is violated by a specific bad
event, and once violated, stays violated.

A **liveness** property says *something good eventually happens* — every
coder eventually compiles, no coder starves. It is violated by an
infinite absence, and cannot be disproved by looking at any single
moment.

> **The sentence to have ready:** *deadlock freedom is safety; meeting a
> deadline is liveness. Proving the first says nothing about the second.*
> A resource that is held but idle is exactly as unavailable as one being
> used.

Switching to `edf` does not help, and it is worth knowing why:
scheduling decides **who gets a dongle next**, but coder 1 is not
*requesting* d0 — it already **has** it. No arbitration policy can
reclaim a held resource. That would require breaking Coffman (3),
pre-emption, which the semantics forbid.

---

## Module E

### Acquisition v2 — atomic pairs  *(closer, still wrong)*

Attack condition (2) at the root: a coder must never hold one dongle
while waiting for the other.

```
┌ CONCEPT ┐  Atomic multi-resource acquisition
```

An operation is **atomic** with respect to other threads if they observe
it as either not started or fully finished, never half-done.

Acquiring two resources atomically means the coder is never in the
intermediate state of holding one. Concretely: register interest in both,
wait until *both* can be taken, and only then mark either as held. The
thread holds nothing while it waits, so it can never be the thing
blocking someone else.

### E.1 — pair predicates

**ADD** — to `dongle_utils.c`:

```c
long	pair_wake(t_coder *c, long now)
{
	long	w1;
	long	w2;

	w1 = wake_time(c->first, now);
	w2 = wake_time(c->second, now);
	if (w1 < w2)
		return (w1);
	return (w2);
}

int	pair_ready(t_coder *c, t_request *r1, t_request *r2, long now)
{
	return (can_take(c->first, r1, now) && can_take(c->second, r2, now));
}
```

**RESULT** — "can I start?" becomes a question about two dongles at once.
The wake-up deadline is the *earlier* of the two, because the earlier
event is the first thing that could change the answer.

### E.2 — waiting on two mutexes

**ADD** — replacing `wait_turn` and `dongle_acquire` in `dongle.c`:

```c
int	wait_pair(t_coder *c, t_request *r1, t_request *r2)
{
	struct timespec	ts;
	long			now;

	now = now_ms();
	while (!sim_stopped(c->sim) && !pair_ready(c, r1, r2, now))
	{
		ms_to_timespec(pair_wake(c, now), &ts);
		pthread_mutex_unlock(&c->second->lock);
		pthread_cond_timedwait(&c->first->cond, &c->first->lock, &ts);
		pthread_mutex_lock(&c->second->lock);
		now = now_ms();
	}
	return (!sim_stopped(c->sim));
}

int	acquire_pair(t_coder *c, t_request *r1, t_request *r2)
{
	pthread_mutex_lock(&c->first->lock);
	pthread_mutex_lock(&c->second->lock);
	heap_push(&c->first->queue, r1);
	heap_push(&c->second->queue, r2);
	pthread_cond_broadcast(&c->first->cond);
	pthread_cond_broadcast(&c->second->cond);
	if (!wait_pair(c, r1, r2))
	{
		heap_remove(&c->first->queue, r1);
		heap_remove(&c->second->queue, r2);
		pthread_mutex_unlock(&c->second->lock);
		pthread_mutex_unlock(&c->first->lock);
		return (0);
	}
	heap_pop(&c->first->queue);
	heap_pop(&c->second->queue);
	c->first->available = 0;
	c->second->available = 0;
	pthread_mutex_unlock(&c->second->lock);
	pthread_mutex_unlock(&c->first->lock);
	return (1);
}

int	dongle_acquire(t_coder *c)
{
	t_request	r1;
	t_request	r2;

	request_init(c, &r1);
	r2 = r1;
	if (!acquire_pair(c, &r1, &r2))
		return (0);
	log_state(c, "has taken a dongle");
	log_state(c, "has taken a dongle");
	return (1);
}
```

**RESULT** — a coder holds nothing until it can take everything.

Three points:

- **`r2 = r1`** copies `seq` and `deadline`. It is *one logical request*
  registered in two queues, which is what makes FIFO and EDF treat the
  pair as an indivisible unit. Two independent requests with different
  `seq` values would let the coder win one queue and lose the other.
- **Mutexes are taken in ascending id order**, including on the re-lock
  after the wait. The Module D argument, now applied to the mutexes
  rather than the resources.
- **We wait on `first->cond`, but `second` may be released by someone
  else**, who broadcasts on *its* condition variable — which this thread
  is not waiting on. `wake_time`'s `now + 1` fallback covers the gap with
  a 1 ms poll. Inelegant, and the first thing an attentive evaluator asks
  about.

The two `log_state` calls emit "has taken a dongle" twice, matching the
subject's example where each "is compiling" is preceded by two such
lines.

**BREAKS** — `./codexion 4 800 200 200 200 3 300 fifo`

```
0   1 has taken a dongle (×2), 1 is compiling
501 2 has taken a dongle (×2), 2 is compiling
801 3 burned out
```

Coders 3 and 4 **never requested anything**. And with 200 coders,
`./codexion 200 800 200 200 200 2 0 edf` had only nine compiling in the
first wave — out of a possible hundred.

### E.3 — diagnosis

With four coders: coder 1 uses d0+d1, coder 2 uses d1+d2, coder 3 uses
d2+d3, coder 4 uses d3+d0. Threads are created in order, so coder 1 gets
the lowest `seq`, coder 2 the next, and so on.

Now apply `can_take`, which requires being **head of the queue**:

- Coder 3 wants d2. Head of d2's queue is coder 2. Blocked.
- Coder 2 wants d1. Head of d1's queue is coder 1. Blocked.
- Coder 4 wants d0. Head of d0's queue is coder 1. Blocked.

Meanwhile **d3 is completely free and nobody touches it.**

Coder 3 is queued behind coder 2 for a dongle that is idle. Coder 2 is
not *using* d1 — it is merely *ahead in line*, and it cannot proceed
either, because its own pair is incomplete.

```
┌ CONCEPT ┐  Starvation, and blocking by priority
```

**Starvation** is a liveness failure in which a thread is
indefinitely denied a resource it needs, while the system as a whole
continues to make progress. Unlike deadlock, nothing is stuck — the
victim is simply always passed over.

Here the mechanism is specific: a request that is *first in line* but
*unable to proceed* blocks every request behind it. The queue discipline
propagates a blockage from one coder to its neighbours, and around the
ring. The result is that a system with `N/2` available parallelism runs
at 1.

> **The lesson:** "first in the queue" and "able to proceed" are
> different predicates. A queue discipline that lets a blocked request
> block those behind it converts a parallel system into a serial one.
---

## Module F

### Acquisition v3 — arbitration by eligibility

The rule changes from *"am I first in the queue?"* to *"am I the highest
priority **among the requests that can actually start right now**?"*.

A request that outranks me but whose own pair is unavailable no longer
blocks me. It keeps its priority for the moment its pair frees up, so
FIFO/EDF ordering is preserved among *eligible* requests.

```
┌ CONCEPT ┐  Eligibility
```

A request is **eligible** when the resources it needs are all currently
free. Priority and eligibility are independent: a request can be highest
priority and ineligible, or lowest priority and eligible.

Arbitration should rank **within the eligible set**, not within the whole
queue. Ranking within the whole queue lets ineligible requests exert
priority they cannot use.

### F.1 — the obstacle, and why the lock design must change

Evaluating eligibility means reading the availability of dongles
belonging to **other** coders. Coder 3, holding the locks for d2 and d3,
would have to inspect d1 — which it cannot lock without violating
ascending-id order, and cannot read at all without a lock.

There is no way around this with per-dongle mutexes. The decision is
intrinsically about a **global snapshot**: two dongles plus every
competitor's pair state.

**ADD** — to `struct s_sim` in the header:

```c
	pthread_mutex_t	arb_lock;
	pthread_cond_t	arb_cond;
```

**REMOVE** — from `t_dongle`: its `lock` and `cond`. The struct returns
to its Module C form:

```c
typedef struct s_dongle
{
	int		id;
	int		available;
	long	available_at;
	t_heap	queue;
}	t_dongle;
```

**RESULT** — per-dongle *state* stays distinct. Only the **decision** is
serialised.

```
┌ CONCEPT ┐  Lock granularity
```

**Fine-grained** locking uses many small locks: more concurrency, but
every operation spanning several locks needs an ordering discipline, and
any read outside a lock is a race.

**Coarse-grained** locking uses one lock over a larger region: less
concurrency, but multi-object operations become trivially consistent.

The right granularity follows from the *operations*, not from the data.
An operation that must observe several objects atomically forces those
objects under one lock. Here, arbitration is such an operation.

> **Expect to defend this.** The subject says "protect each dongle's
> state with a mutex". One mutex protecting the state of all dongles
> satisfies that literally, and the justification is the paragraph above.
> Distributing the decision forces either an unsynchronised read or a
> lock-ordering violation. Have the answer ready — it is a fair question.

### F.2 — the request learns who owns it

**SO FAR** — `t_request` has `coder_id`, `deadline`, `seq`, `idx`.

The arbitration scan will walk a queue and, for each competing request,
ask "is *that* coder's pair free?". `coder_id` is an `int`; getting from
it to the coder's two dongles would mean indexing back into
`sim->coders`, which means carrying a `t_sim *` and doing arithmetic.

**ADD** — a direct pointer:

```c
	t_coder	*owner;
```

**RESULT**

```c
typedef struct s_request
{
	int		coder_id;
	long	deadline;
	long	seq;
	int		idx;
	t_coder	*owner;
}	t_request;
```

This is why `t_coder` is forward-typedef'd at the top of the header:
`t_request` is defined before `t_coder`, and needs to point at it.

### F.3 — availability predicates

**ADD** — in `dongle_utils.c`, replacing `can_take` and `pair_ready`:

```c
int	dongle_free(t_dongle *d, long now)
{
	return (d->available && now >= d->available_at);
}

int	pair_free(t_coder *c, long now)
{
	return (dongle_free(c->first, now) && dongle_free(c->second, now));
}
```

**RESULT** — `dongle_utils.c` has five functions: `ms_to_timespec`,
`wake_time`, `pair_wake`, `dongle_free`, `pair_free`.

Note what is gone: the queue-position clause. These predicates are now
**pure availability**. Position moves to its own module, which is what
makes both testable in isolation.

### F.4 — eligibility

**ADD** — a new file, `arb.c`:

```c
int	is_best(t_heap *h, t_request *req, long now)
{
	int	i;

	i = 0;
	while (i < h->size)
	{
		if (h->items[i] != req
			&& heap_less(h->items[i], req, h->policy)
			&& pair_free(h->items[i]->owner, now))
			return (0);
		i++;
	}
	return (1);
}

int	can_take_pair(t_coder *c, t_request *r1, t_request *r2, long now)
{
	if (!pair_free(c, now))
		return (0);
	return (is_best(&c->first->queue, r1, now)
		&& is_best(&c->second->queue, r2, now));
}
```

**RESULT** — the arbitration rule in twenty lines.

`is_best` returns 0 only when it finds a competitor that is **both**
higher priority *and* eligible. An ineligible competitor is skipped
regardless of priority. This is where `owner` earns its place: without
it, there is no route from a queued request to its coder's pair state.

The scan is `O(size)`, and `size ≤ 2` always, because each dongle is
adjacent to exactly two coders. It is a linear scan over two elements.

```
┌ CONCEPT ┐  Why this cannot livelock
```

**Livelock** is a liveness failure in which threads keep executing but
none makes progress — for example, each repeatedly yields to another.
The eligibility rule could plausibly cause it: everyone waiting for
someone else to go first. It does not, and here is the argument to give
in defence:

> The priority order is **total** (deadline → seq → id, no ties, by A.4).
> Consider the set `E` of all requests whose pair is currently free. If
> `E` is non-empty it has a unique minimum `m`. Now ask whether `m`
> passes `is_best` in its own queues: a competitor could only block it by
> being both higher priority *and* eligible — but such a competitor would
> be in `E` and rank below `m`, contradicting `m`'s minimality. So `m`
> passes both scans and proceeds.
>
> Therefore: **whenever any pair is free, at least one coder makes
> progress.** No coder starves under EDF given feasible parameters.

Note what the argument depends on: the order being *total*. This is the
payoff for the tie-breaker chain in A.4 — without uniqueness, "the
minimum" may not exist and the proof does not close.

### F.5 — the final acquisition

**ADD** — `dongle.c` in its final form:

```c
void	request_init(t_coder *c, t_request *req)
{
	req->coder_id = c->id;
	req->seq = next_seq(c->sim);
	req->deadline = coder_deadline(c);
	req->owner = c;
	req->idx = -1;
}

int	wait_pair(t_coder *c, t_request *r1, t_request *r2)
{
	struct timespec	ts;
	long			now;

	now = now_ms();
	while (!sim_stopped(c->sim) && !can_take_pair(c, r1, r2, now))
	{
		ms_to_timespec(pair_wake(c, now), &ts);
		pthread_cond_timedwait(&c->sim->arb_cond, &c->sim->arb_lock, &ts);
		now = now_ms();
	}
	return (!sim_stopped(c->sim));
}

int	acquire_pair(t_coder *c, t_request *r1, t_request *r2)
{
	int	ok;

	pthread_mutex_lock(&c->sim->arb_lock);
	heap_push(&c->first->queue, r1);
	heap_push(&c->second->queue, r2);
	pthread_cond_broadcast(&c->sim->arb_cond);
	ok = wait_pair(c, r1, r2);
	heap_remove(&c->first->queue, r1);
	heap_remove(&c->second->queue, r2);
	if (ok)
	{
		c->first->available = 0;
		c->second->available = 0;
	}
	pthread_mutex_unlock(&c->sim->arb_lock);
	return (ok);
}

int	dongle_acquire(t_coder *c)
{
	t_request	r1;
	t_request	r2;

	request_init(c, &r1);
	r2 = r1;
	if (!acquire_pair(c, &r1, &r2))
		return (0);
	log_state(c, "has taken a dongle");
	log_state(c, "has taken a dongle");
	return (1);
}

void	dongle_release(t_dongle *d, t_sim *sim)
{
	pthread_mutex_lock(&sim->arb_lock);
	d->available = 1;
	d->available_at = now_ms() + sim->cooldown;
	pthread_cond_broadcast(&sim->arb_cond);
	pthread_mutex_unlock(&sim->arb_lock);
}
```

**RESULT** — five functions, one mutex, no nesting.

`acquire_pair` is far simpler than v2: one lock, one unlock, no
interleaved unlock/relock inside the wait. `wait_pair` releases and
re-acquires `arb_lock` automatically, because that is what
`pthread_cond_timedwait` does.

**Both exit paths now use `heap_remove`.** On success you are no longer
guaranteed to be at index 0 — you were the best *eligible* request, which
may sit anywhere in the array — so `heap_pop` would remove the wrong
element. This is the moment `heap_remove` and the whole `idx` discipline
pay for themselves.

This also adds the second lock-ordering constraint, because `wait_pair`
calls `sim_stopped` while holding `arb_lock`:

```
arb_lock  →  stop_lock
log_lock  →  stop_lock
```

`stop_lock` remains a leaf.

**RESULT** — `./codexion 200 800 200 200 200 2 0 edf`

Count the first wave:

```bash
./codexion 200 800 200 200 200 2 0 edf | awk '$1<200 && /is compiling/' | wc -l
```

**93**, against a theoretical maximum of 100 (a 200-ring is 2-colourable,
so at most half can compile at once). Under v2 the same count was nine.
The run completes with no burnout.

How early the wave lands depends on the machine — thread creation is
serial, so on four cores most start within ~10 ms, while on a single core
the same 93 spread across the first 200 ms. The *count* is the robust
measurement; the timestamps are not.

> **Verified dead code.** With v3 in place, `heap_peek` and `heap_pop` are
> no longer called anywhere. `heap_peek` was v1's head-of-queue test,
> replaced by `is_best`; `heap_pop` was the grant path, replaced by
> `heap_remove`. Confirm with:
>
> ```bash
> grep -rn "heap_peek\|heap_pop" *.c *.h
> ```
>
> Only definitions and prototypes come back. Nothing forces your hand —
> unused non-static functions warn under neither gcc nor Norminette.
> Delete them, or keep them and be ready to say why. Dead code you cannot
> account for is the worse option.

---

## Module G

### The coder thread

### G.1 — interruptible sleeping

**ADD** — `coder.c`:

```c
void	precise_sleep(t_sim *sim, long ms)
{
	long	end;

	end = now_ms() + ms;
	while (now_ms() < end)
	{
		if (sim_stopped(sim))
			return ;
		usleep(200);
	}
}
```

**RESULT** — a sleep that ends on time and reacts to shutdown.

A single `usleep(ms * 1000)` fails twice. First, `usleep` guarantees a
*minimum*, never a maximum — the kernel may return well after the
requested time. Second, the thread would be deaf to the stop flag for the
entire phase, which for a 200 ms phase means up to 200 ms of latency
against a 10 ms requirement.

The loop re-checks both conditions every 200 µs, trading a little CPU for
bounded latency.

### G.2 — one compile cycle

**ADD**

```c
int	do_compile(t_coder *c)
{
	if (!dongle_acquire(c))
		return (0);
	pthread_mutex_lock(&c->state_lock);
	c->last_compile_start = now_ms();
	pthread_mutex_unlock(&c->state_lock);
	log_state(c, "is compiling");
	precise_sleep(c->sim, c->sim->t_compile);
	dongle_release(c->first, c->sim);
	dongle_release(c->second, c->sim);
	pthread_mutex_lock(&c->state_lock);
	c->compiles_done++;
	pthread_mutex_unlock(&c->state_lock);
	return (1);
}
```

**RESULT** — acquire → stamp → log → work → release → count.

The ordering is deliberate:

- **`last_compile_start` is written only after both dongles are held.**
  The subject defines that instant as the start of a compile, and it is
  the basis of the EDF deadline. Written under `state_lock` because the
  monitor and `coder_deadline` read it concurrently.
- **`compiles_done` is incremented after release.** The monitor can
  therefore never observe a coder as "finished a compile" while it still
  holds resources — the two facts are consistent in every observable
  state.

### G.3 — the single-coder case

**ADD**

```c
void	take_single(t_coder *c)
{
	pthread_mutex_lock(&c->sim->arb_lock);
	c->first->available = 0;
	pthread_mutex_unlock(&c->sim->arb_lock);
	log_state(c, "has taken a dongle");
}

void	*lone_coder(t_coder *c)
{
	take_single(c);
	while (!sim_stopped(c->sim))
		usleep(200);
	return (NULL);
}
```

**RESULT** — with `N == 1`, `assign_dongles` gives `first == second`:
`(0 + 1) % 1 == 0`, so both point at the same dongle. A normal pair
acquisition would require it to be free twice over and would wait
forever.

The correct behaviour per the subject: take the one dongle, log it, never
compile, burn out at `time_to_burnout`.

```
$ ./codexion 1 800 200 200 200 5 0 fifo
0 1 has taken a dongle
801 1 burned out
```

Every project in this family has this edge case, and evaluators test it
first because it is the one people forget.

### G.4 — the loop

**ADD**

```c
void	*coder_routine(void *arg)
{
	t_coder	*c;

	c = (t_coder *)arg;
	if (c->sim->n_coders == 1)
		return (lone_coder(c));
	while (!sim_stopped(c->sim))
	{
		if (!do_compile(c))
			break ;
		log_state(c, "is debugging");
		precise_sleep(c->sim, c->sim->t_debug);
		log_state(c, "is refactoring");
		precise_sleep(c->sim, c->sim->t_refactor);
	}
	return (NULL);
}
```

**RESULT** — `coder.c` complete at five functions.

`do_compile` returning 0 means the simulation stopped mid-acquisition, so
the thread leaves the loop rather than proceeding to a debug phase that
would log after shutdown.

---

## Module H

### The monitor thread

The subject requires the burnout message within **10 ms** of the event.
That is a number, so expect it to be measured.

### H.1 — shutdown

**ADD** — `monitor.c`:

```c
void	stop_sim(t_sim *sim)
{
	pthread_mutex_lock(&sim->stop_lock);
	sim->stop = 1;
	pthread_mutex_unlock(&sim->stop_lock);
	pthread_mutex_lock(&sim->arb_lock);
	pthread_cond_broadcast(&sim->arb_cond);
	pthread_mutex_unlock(&sim->arb_lock);
}
```

**RESULT** — flag set, then everyone woken.

**Read the order carefully.** `stop_lock` is released *before* `arb_lock`
is taken. The two constraints established so far —
`log_lock → stop_lock` and `arb_lock → stop_lock` — make `stop_lock` a
**leaf**: nothing is ever acquired while holding it. Take `arb_lock`
while holding `stop_lock` here and you have a textbook lock-order
inversion against every coder thread, which hangs on some runs and not
others.

The broadcast is not optional. Setting the flag alone leaves threads
parked in `pthread_cond_timedwait` until their next timeout, and
`pthread_join` in `main` stalls behind them.

### H.2 — the check

**ADD**

```c
int	check_burnout(t_sim *sim, int i)
{
	long	last;
	long	now;

	pthread_mutex_lock(&sim->coders[i].state_lock);
	last = sim->coders[i].last_compile_start;
	pthread_mutex_unlock(&sim->coders[i].state_lock);
	now = now_ms();
	if (now - last <= sim->t_burnout)
		return (0);
	pthread_mutex_lock(&sim->log_lock);
	printf("%ld %d burned out\n", now - sim->start_ms, sim->coders[i].id);
	pthread_mutex_unlock(&sim->log_lock);
	stop_sim(sim);
	return (1);
}
```

**RESULT** — detect, print, stop.

Two deliberate departures from `log_state`:

- **This `printf` does not test the stop flag.** It is the one message
  that must appear *after* the simulation ends, so it cannot go through
  `log_state`.
- **`log_lock` is released before `stop_sim`.** Same leaf-lock
  discipline.

The timestamp is captured *before* the write, so a slow terminal cannot
inflate the reported time.

Note the comparison is `>`, strictly greater. A coder starting a compile
at exactly `last + t_burnout` survives. This single millisecond decides
the boundary case in Step Z.5.

### H.3 — the completion check

**ADD**

```c
int	all_done(t_sim *sim)
{
	int	i;
	int	done;

	if (sim->must_compile <= 0)
		return (0);
	i = 0;
	while (i < sim->n_coders)
	{
		pthread_mutex_lock(&sim->coders[i].state_lock);
		done = sim->coders[i].compiles_done;
		pthread_mutex_unlock(&sim->coders[i].state_lock);
		if (done < sim->must_compile)
			return (0);
		i++;
	}
	return (1);
}
```

**RESULT** — the second stop condition: every coder has met its quota.

Early return on the first coder below quota — no reason to read the rest.

### H.4 — absolute deadlines

```
┌ CONCEPT ┐  Relative vs absolute deadlines, and drift
```

A **relative** wait says "sleep 1 ms from now". An **absolute** wait says
"wake at time T".

With relative waits, every overrun is added to the next target. Ten
iterations that each overrun by 3 ms leave the tenth wake-up 30 ms late.
This accumulation is **drift**.

With absolute waits computed from a fixed origin (`start + 1`,
`start + 2`, …), an overrun affects only the iteration it occurred in.
The next target was already fixed and does not move.

**ADD**

```c
void	monitor_wait(t_sim *sim, long *next)
{
	struct timespec	ts;

	*next += 1;
	ms_to_timespec(*next, &ts);
	pthread_mutex_lock(&sim->arb_lock);
	pthread_cond_timedwait(&sim->arb_cond, &sim->arb_lock, &ts);
	pthread_mutex_unlock(&sim->arb_lock);
}

void	*monitor_routine(void *arg)
{
	t_sim	*sim;
	int		i;
	long	next;

	sim = (t_sim *)arg;
	next = now_ms();
	while (!sim_stopped(sim))
	{
		i = 0;
		while (i < sim->n_coders)
		{
			if (check_burnout(sim, i))
				return (NULL);
			i++;
		}
		if (all_done(sim))
			return (stop_sim(sim), NULL);
		monitor_wait(sim, &next);
	}
	return (NULL);
}
```

**RESULT** — `monitor.c` complete at five functions.

`next` advances in fixed 1 ms steps from a fixed origin, so drift cannot
accumulate. The condition variable is used purely as a timer here — the
monitor is not waiting for a condition, it is waiting for a time.

Sharing `arb_cond` means the monitor also wakes on every dongle release,
which only makes it more responsive. If you prefer no coupling, add a
dedicated `mon_lock`/`mon_cond` pair that nobody signals.

### H.5 — a debugging method worth stealing

On the development machine, this configuration occasionally reported
burnout at 850–1000 ms instead of 801:

```
$ for i in $(seq 1 10); do ./codexion 5 800 200 200 200 3 300 fifo; done \
      | grep "burned out"
801 ...  801 ...  988 ...  801 ...  1002 ...  801 ...
```

The temptation is to start changing the monitor. **Measure first.** Four
rounds of instrumentation, each answering one question:

| question | instrument | answer |
|---|---|---|
| Is the loop late? | interval between iterations | Yes — 130–240 ms gaps, each late burnout ending as a gap ended |
| Is it blocked on a mutex? | time each `check_burnout` and `all_done` | **Zero** slow calls. Mutexes exonerated |
| Is it the sleep primitive? | time the sleep itself | `usleep(100)` returning after 724 ms. Swapping to `cond_timedwait` changed nothing |
| Is the clock lying? | read both clocks across the gap | `wall=236 mono=236`. Not an NTP jump |

Two independent wait primitives failing identically means the cause lies
beneath both. Then the question that should have been asked first:

**Is it only the monitor, or the whole process?** Line the monitor's gaps
up against the coders' own log:

```
GAP 200 ending at 453      453 3 is refactoring   ← should have been 400
GAP 236 ending at 692      692 4 has taken...     ← should have been ~500
```

Every thread froze together, for the same interval.
`systemd-detect-virt` answered the rest: **oracle** — a VirtualBox guest
being descheduled by its hypervisor. On 42 cluster hardware the same
binary reports **801 ms in 20 runs out of 20**.

> **Three transferable lessons.**
> *(a)* Instrument before optimising — the sleep mechanism was changed
> three times before anyone established what was slow.
> *(b)* When two independent mechanisms fail identically, the cause is
> beneath both of them.
> *(c)* Ask **"who is affected?"** before "why is X slow?". One `grep` of
> the coders' timestamps would have found this in the first minute.
>
> Keep the logs. They convert "my program is sometimes late" into "my VM
> stalls, and here is the proof".

---

## Module I

### Parse, init, run, cleanup

Mechanical, with three details that bite.

### I.1 — parse.c

**ADD**

```c
int	usage(void)
{
	fprintf(stderr, "usage: ./codexion n_coders time_to_burnout ");
	fprintf(stderr, "time_to_compile time_to_debug time_to_refactor ");
	fprintf(stderr, "n_compiles dongle_cooldown [fifo|edf]\n");
	return (0);
}

int	parse_long(char *s, long *out)
{
	long	v;
	int		i;

	v = 0;
	i = 0;
	if (!s[0])
		return (0);
	while (s[i])
	{
		if (s[i] < '0' || s[i] > '9')
			return (0);
		v = v * 10 + (s[i] - '0');
		if (v > 2147483647)
			return (0);
		i++;
	}
	*out = v;
	return (1);
}
```

**RESULT** — digits only, so `-5`, `12a`, `1.5` and `""` are all
rejected without a separate negative check. The subject's requirement
falls out of the design.

`atoi` is permitted but useless here: it cannot distinguish `"0"` from
`"abc"`, since both return 0, and it has no error channel. The overflow
guard inside the loop catches huge inputs before the value wraps.

`usage` returns 0 so every check can be written `return (usage());` —
print and fail in one expression.

**ADD**

```c
int	parse_policy(t_sim *sim, char *s)
{
	if (strcmp(s, "fifo") == 0)
	{
		sim->policy = FIFO;
		return (1);
	}
	if (strcmp(s, "edf") == 0)
	{
		sim->policy = EDF;
		return (1);
	}
	return (0);
}

int	parse_times(t_sim *sim, char **av)
{
	if (!parse_long(av[2], &sim->t_burnout)
		|| !parse_long(av[3], &sim->t_compile)
		|| !parse_long(av[4], &sim->t_debug)
		|| !parse_long(av[5], &sim->t_refactor)
		|| !parse_long(av[7], &sim->cooldown))
		return (0);
	return (sim->t_burnout > 0);
}

int	parse_args(t_sim *sim, int ac, char **av)
{
	long	n;
	long	m;

	if (ac != 9)
		return (usage());
	if (!parse_long(av[1], &n) || n < 1 || n > 500)
		return (usage());
	if (!parse_times(sim, av))
		return (usage());
	if (!parse_long(av[6], &m) || m < 1)
		return (usage());
	if (!parse_policy(sim, av[8]))
		return (usage());
	sim->n_coders = (int)n;
	sim->must_compile = (int)m;
	return (1);
}
```

**RESULT** — `parse.c` complete at five functions. `strcmp` is exact, so
`FIFO`, `fifo ` and `f` are all rejected, as the subject requires.

### I.2 — init.c

**ADD**

```c
int	init_dongles(t_sim *sim)
{
	int	i;

	sim->dongles = malloc(sizeof(t_dongle) * sim->n_coders);
	if (!sim->dongles)
		return (0);
	memset(sim->dongles, 0, sizeof(t_dongle) * sim->n_coders);
	i = 0;
	while (i < sim->n_coders)
	{
		sim->dongles[i].id = i;
		sim->dongles[i].available = 1;
		if (!heap_init(&sim->dongles[i].queue, sim->n_coders, sim->policy))
			return (0);
		i++;
	}
	return (1);
}

int	init_coders(t_sim *sim)
{
	int	i;

	sim->coders = malloc(sizeof(t_coder) * sim->n_coders);
	if (!sim->coders)
		return (0);
	memset(sim->coders, 0, sizeof(t_coder) * sim->n_coders);
	i = 0;
	while (i < sim->n_coders)
	{
		sim->coders[i].id = i + 1;
		sim->coders[i].sim = sim;
		pthread_mutex_init(&sim->coders[i].state_lock, NULL);
		assign_dongles(sim, i);
		i++;
	}
	return (1);
}

int	init_sim(t_sim *sim)
{
	pthread_mutex_init(&sim->stop_lock, NULL);
	pthread_mutex_init(&sim->seq_lock, NULL);
	pthread_mutex_init(&sim->log_lock, NULL);
	pthread_mutex_init(&sim->arb_lock, NULL);
	pthread_cond_init(&sim->arb_cond, NULL);
	if (!init_dongles(sim))
		return (0);
	if (!init_coders(sim))
		return (0);
	return (1);
}
```

**RESULT** — allocate, zero, then set up each element.

The `memset` to zero before anything else is what makes cleanup safe
after a partial failure: `items` is `NULL`, pointers are `NULL`, and
`free(NULL)` is defined to do nothing.

**The trap:** the global mutexes are initialised in `init_sim`, **not**
inside the loop in `init_dongles`. Re-initialising an already-initialised
mutex is undefined behaviour, and a loop would do it `n` times. It is an
easy line to put in the wrong place and it will not warn you.

Note `sim->coders[i].id = i + 1` — the subject numbers coders from 1,
while array indices start at 0. Every log line uses `id`; every array
access uses `i`.

### I.3 — run.c, and the one-line bug that fakes a burnout

**ADD**

```c
void	prime_deadlines(t_sim *sim)
{
	int	i;

	sim->start_ms = now_ms();
	i = 0;
	while (i < sim->n_coders)
	{
		sim->coders[i].last_compile_start = sim->start_ms;
		i++;
	}
}

int	start_coders(t_sim *sim)
{
	int	i;

	i = 0;
	while (i < sim->n_coders)
	{
		if (pthread_create(&sim->coders[i].thread, NULL,
				coder_routine, &sim->coders[i]) != 0)
			return (i);
		i++;
	}
	return (i);
}

void	join_coders(t_sim *sim, int count)
{
	int	i;

	i = 0;
	while (i < count)
	{
		pthread_join(sim->coders[i].thread, NULL);
		i++;
	}
}

int	run_sim(t_sim *sim)
{
	pthread_t	monitor;
	int			started;

	prime_deadlines(sim);
	if (pthread_create(&monitor, NULL, monitor_routine, sim) != 0)
		return (0);
	started = start_coders(sim);
	if (started < sim->n_coders)
		stop_sim(sim);
	pthread_join(monitor, NULL);
	join_coders(sim, started);
	return (1);
}
```

**RESULT** — deadlines primed, then threads launched, then joined.

**`prime_deadlines` must run before any `pthread_create`, including the
monitor's.** Leave `last_compile_start` at zero and the monitor computes
`now - 0` — roughly fifty-seven years — and declares burnout on its first
tick. The symptom is "everyone burns out instantly"; the cause is one
missing initialisation. It is a rite of passage in this project family.

`start_coders` returns how many threads it actually created, and
`join_coders` joins exactly that many. Joining a `pthread_t` that was
never created is undefined behaviour, which is why the count is threaded
through rather than assuming `n_coders`.

### I.4 — cleanup.c and main

**ADD**

```c
void	cleanup_dongles(t_sim *sim)
{
	int	i;

	if (!sim->dongles)
		return ;
	i = 0;
	while (i < sim->n_coders)
	{
		heap_destroy(&sim->dongles[i].queue);
		i++;
	}
	free(sim->dongles);
	sim->dongles = NULL;
}

void	cleanup_coders(t_sim *sim)
{
	int	i;

	if (!sim->coders)
		return ;
	i = 0;
	while (i < sim->n_coders)
	{
		pthread_mutex_destroy(&sim->coders[i].state_lock);
		i++;
	}
	free(sim->coders);
	sim->coders = NULL;
}

void	cleanup_sim(t_sim *sim)
{
	cleanup_dongles(sim);
	cleanup_coders(sim);
	pthread_mutex_destroy(&sim->stop_lock);
	pthread_mutex_destroy(&sim->seq_lock);
	pthread_mutex_destroy(&sim->log_lock);
	pthread_mutex_destroy(&sim->arb_lock);
	pthread_cond_destroy(&sim->arb_cond);
}
```

```c
int	main(int ac, char **av)
{
	t_sim	sim;

	memset(&sim, 0, sizeof(t_sim));
	if (!parse_args(&sim, ac, av))
		return (1);
	if (!init_sim(&sim))
	{
		cleanup_sim(&sim);
		return (1);
	}
	run_sim(&sim);
	cleanup_sim(&sim);
	return (0);
}
```

**RESULT** — the program is complete.

`t_sim` lives on `main`'s stack. That is how "global variables are
forbidden" is satisfied without any global at all: one stack object,
passed by pointer to every thread. Its lifetime covers every thread's
lifetime because `main` does not return until every join completes.

The `if (!sim->dongles) return ;` guards make cleanup safe on the partial
failure path, where `init_dongles` may have returned early.

---

## Step Z

### Validation

### Z.1 — Norminette

```
norminette
```

All fifteen files must say `OK!`. The three limits that bite here: **25
lines per function**, **5 functions per file**, **5 variable declarations
per function**. When a function outgrows the limit, extract a logical
sub-operation rather than compressing — `request_init` and `wait_pair`
were both born that way, and both are clearer for it.

### Z.2 — functional battery

```bash
./codexion 5 800 200 200 200 5 0 fifo      # completes, no burnout
./codexion 5 800 200 200 200 5 50 edf      # completes with cooldown
./codexion 1 800 200 200 200 5 0 fifo      # burnout at 801
./codexion 2 800 200 200 200 3 0 fifo      # smallest even ring
./codexion 3 800 200 200 200 3 0 edf       # odd ring
./codexion 200 800 200 200 200 2 0 edf     # 201 threads
./codexion 4 1500 200 200 200 3 300 fifo   # high cooldown, feasible
./codexion 5 800 200 200 abc 5 0 fifo      # rejected
./codexion 5 800 200 200 200 5 0 rr        # rejected
./codexion 4 0 200 200 200 3 0 fifo        # rejected
./codexion 5 800 200 200 200 0 0 fifo      # rejected
./codexion 5 -1 200 200 200 3 0 fifo       # rejected
```

### Z.3 — precision

```bash
for i in $(seq 1 20); do ./codexion 5 800 200 200 200 3 300 fifo; done \
    | grep "burned out" | awk '{print $1}' | sort | uniq -c
```

Expected on real hardware: `20  801`. An 800 ms deadline detected within
one millisecond, twenty times out of twenty.

### Z.4 — the analysers

```bash
valgrind --tool=helgrind ./codexion 4 800 200 200 200 3 0 edf 2>&1 | tail -3
valgrind --leak-check=full ./codexion 4 800 200 200 200 3 0 fifo 2>&1 | tail -5
```

Expect no data races, no lock-order inversions, and every block freed.

**On the allocation count.** The project allocates exactly
**`n_coders + 2`** blocks from three call sites: `init_dongles`,
`init_coders`, and one `heap_init` per dongle. For `N=4` that is 6.
`memcheck` reports a higher total (around 12) because `printf` allocates
a stdio buffer and the pthread runtime allocates too. What matters is
that allocations equal frees.

**On helgrind warnings.** Valgrind 3.18 emits six
`pthread_cond_{signal,broadcast}: dubious: associated lock is not held`
warnings. Read the stack traces: every one originates *inside* glibc's
`pthread_cond_timedwait`, not in project code — every project broadcast
is made while holding the associated mutex. Valgrind 3.25 suppresses
them. Worth stating in the README so nobody re-derives it.

### Z.5 — feasibility: telling a bug from a boundary

Not every parameter set is survivable. Knowing where the line is converts
"my program burns out" into a defensible answer.

For an **even** ring the coders split into two alternating groups. Group
A compiles, releases, and its dongles enter cooldown; group B compiles;
then group A is ready again. A coder's next compile can begin at the
later of two moments: when its own cycle finishes, and when its dongles
finish the other group's turn plus cooldown. So:

```
period = max(C + D + R, 2C + 2 * cooldown)
```

Measured against `4 800 200 200 200 3 <cd> fifo`, watching when coder 1
starts each compile:

| cooldown | predicted period | observed starts | outcome |
|---|---|---|---|
| 0 | 600 ms | 1, 601, 1201 | survives |
| 100 | 600 ms | 2, 603, 1203 | survives |
| 200 | **800 ms** | 3, 803, 1603 | **boundary — either way** |
| 300 | 1000 ms | 2, then burnout at 803 | burns out |

The prediction matches the observed period to the millisecond in every
row. That is the point: the model is right, so a burnout can be
*explained* rather than guessed at.

The `cd=200` row is the interesting one. The period lands on exactly 800,
precisely `time_to_burnout`. The check in H.2 is `>`, strictly greater,
so a compile starting at exactly `last + 800` survives by one
millisecond. Which way a run falls is decided by sub-millisecond jitter:
on one machine it completed 10 runs out of 10; on a VirtualBox guest it
burned out at 801. **Both are correct behaviour.**

Verify it:

```bash
for i in $(seq 1 10); do
    ./codexion 4 800 200 200 200 3 200 fifo | grep -c "burned out"
done
```

**Odd rings are worse.** The ring is not 2-colourable, so it needs three
phases rather than two, and a cooldown survivable at `N=4` is not at
`N=5`. Burnout in these cases is the correct outcome — and saying so,
with the arithmetic, is a much better answer than "it burns out
sometimes".

---

## Appendix

### Glossary

**Atomic** — observed by other threads as either not started or fully
finished, never half-done.

**Binary heap** — a priority queue stored as an array representing a
complete binary tree, where every parent outranks its children.

**Broadcast** — waking all threads blocked on a condition variable.
Required when the set of threads able to proceed can change.

**Coffman conditions** — the four properties (mutual exclusion, hold and
wait, no pre-emption, circular wait) that must *all* hold for deadlock.
Breaking any one prevents it.

**Condition variable** — a primitive for blocking until shared state
satisfies a predicate. Always used with a mutex and always inside a
`while` loop.

**Critical section** — a region of code that must not execute
concurrently with itself.

**Deadlock** — a set of threads each blocked waiting for a resource held
by another in the set. A safety failure.

**Drift** — accumulated timing error from chaining relative waits, where
each overrun shifts every subsequent target.

**Eligible** — describing a request whose required resources are all
currently free. Independent of priority.

**Incomplete type** — a type whose name is known but whose size is not.
Pointers to it are legal; embedding it by value is not.

**Invariant** — a condition true before and after every operation, though
possibly broken temporarily inside one.

**Liveness** — a property asserting something good eventually happens.
Violated by an infinite absence, never by a single moment.

**Livelock** — threads executing continuously without making progress,
typically by repeatedly yielding to each other.

**Lock-order inversion** — two threads acquiring two locks in opposite
orders, which deadlocks.

**Monotonic clock** — a clock that counts steadily and never jumps.
Correct for measuring durations, unlike wall-clock time.

**Mutex** — a lock ensuring at most one thread is inside a critical
section at a time.

**Race condition** — a defect whose outcome depends on thread timing.

**Resource ordering** — assigning resources unique numbers and requiring
acquisition in increasing order, which prevents circular wait.

**Safety** — a property asserting something bad never happens. Violated
by a specific event, and permanently once violated.

**Spurious wake-up** — a return from `pthread_cond_wait` without any
signal having occurred. Permitted by POSIX, hence the `while` loop.

**Starvation** — a thread indefinitely denied a resource while the system
makes progress around it. A liveness failure.

**Total order** — a comparison under which every pair of distinct
elements can be ranked. Required for "the highest-priority element" to
name a unique element.

**Translation unit** — one `.c` file plus everything it includes;
compiled in isolation into one `.o`.

### Build order at a glance

```
 Step 0  skeleton + Makefile (-pthread on BOTH rules, header as dep)
 A  heap.c/_ops   header gains: t_request{id,deadline,seq}, t_heap
                  then t_request.idx when removal needs it
                  ← TEST WITH NO THREADS
 B  time.c/log.c  header gains: t_sim{start_ms, stop, seq, 3 locks}
 C  entities      header gains: t_dongle, t_coder, forward typedefs,
                  and the rest of t_sim's configuration
 D  acquisition   header gains: t_dongle{lock, cond}
                  v1: one dongle at a time    ← fails: hold-and-wait
 E  acquisition   v2: atomic pairs            ← fails: head-of-queue
 F  arb.c         header: lock/cond move to t_sim; t_request gains owner
                  v3: eligibility arbitration ← correct
 G  coder.c       compile/debug/refactor + N==1
 H  monitor.c     absolute-deadline polling
 I  parse/init/run/cleanup
 Z  norminette + battery + helgrind + memcheck
```

Modules D and E are dead ends. Build them anyway if you are learning the
project — the failures are the content.

### Eight principles that recur

1. **Deadlock freedom is safety; meeting a deadline is liveness.**
   Proving the first says nothing about the second.
2. **A held-but-idle resource is unavailable.** Eliminating hold-and-wait
   matters more than breaking the cycle.
3. **Priority must not block the ineligible.** Rank within the set that
   can actually start.
4. **Every predicate is re-tested under its lock, in a `while`.**
   Spurious wake-ups are real and predicates go stale.
5. **Temporal events need timed waits.** Nobody signals the end of a
   cooldown.
6. **`broadcast`, not `signal`,** whenever the winner can change.
7. **One lock order, written down, obeyed everywhere.** Here:
   `arb_lock`/`log_lock` → `stop_lock`, with `stop_lock` a leaf.
8. **Measure before fixing.** Instrument what you suspect, then what lies
   beneath it, then ask who else is affected.

### Defence cheat-sheet

- **Where is the scheduler?** `heap_less`. FIFO by `seq`; EDF by
  deadline → seq → id, a total order, hence deterministic.
- **Why a heap?** The subject requires a priority queue and forbids
  stdlib ones. Swapping `heap_less` swaps the policy without touching the
  structure.
- **Why does `t_request` have `idx`?** To remove a request from the
  middle of a queue — needed on both the grant and the shutdown paths.
- **Why does `t_request` have `owner`?** So the arbitration scan can ask
  whether a *competitor's* pair is free.
- **How is deadlock prevented?** Two ways: ascending-id acquisition
  breaks Coffman's circular wait, and atomic pair acquisition eliminates
  hold-and-wait. The second is the one that matters.
- **Show me you tested that.** The v1 log: coders 1–3 each holding one
  dongle, the chain unwinding 200 ms at a step, coder 5 burning out at
  801.
- **How is starvation prevented?** `is_best` yields only to competitors
  that outrank *and* are eligible. Liveness follows from the priority
  order being total — see the minimality argument in F.4.
- **Why one global `arb_lock` instead of per-dongle mutexes?** The
  decision needs a consistent snapshot of two dongles plus every
  competitor's pair state. Distributing it forces either an
  unsynchronised read or a lock-ordering violation.
- **Why `timedwait` and not `wait`?** Cooldown expiry has no signaller;
  it is a temporal event.
- **Why `broadcast` and not `signal`?** Under EDF the queue winner
  changes on every enqueue; `signal` could wake the wrong thread.
- **Why is the stop check inside `log_lock`?** Otherwise a state message
  can be printed after "burned out".
- **What is the lock order?** `arb_lock`/`log_lock` → `stop_lock`.
  `stop_lock` is a leaf; nothing is acquired while holding it.
- **What happens with `N == 1`?** `first == second`; `lone_coder` takes
  the one dongle, never compiles, burns out at `time_to_burnout`.
- **How many mallocs?** `n_coders + 2`, from three call sites. Requests
  live on thread stacks, so **zero allocation per request**. memcheck's
  total is higher because stdio and pthread allocate too.
- **Is a burnout always a bug?** No.
  `period = max(C+D+R, 2C+2·cooldown)`; if that exceeds
  `time_to_burnout`, burnout is correct. Odd rings need three phases.
- **Those helgrind warnings?** All inside glibc's `cond_timedwait`; no
  data races, no lock-order inversions; absent under Valgrind 3.25.

### Pitfalls and how to read the error

| symptom | cause | fix |
|---|---|---|
| `unknown type name 't_heap'` | type used by value before definition | define `t_heap` above `t_dongle`; forward-typedef only pointers |
| `multiple definition of 'now_ms'` | function *body* in the header | braces in `.c`, semicolons in `.h` |
| `implicit declaration of 'heap_init'` | `.c` missing `#include "codexion.h"` | every `.c` includes it |
| `undefined reference to 'pair_wake'` | prototype exists, body never written | grep the header against the sources |
| everyone burns out at t≈0 | `last_compile_start` left at 0 | `prime_deadlines` before any `pthread_create` |
| coders each hold one dongle, one compiles | hold-and-wait | acquire the pair atomically (Module E) |
| free dongle, nobody takes it | blocking by priority | arbitrate by eligibility (Module F) |
| a thread hangs with `N == 1` | `first == second`, acquired twice | `lone_coder` |
| `heap_remove` removes the wrong request | a missing `idx` update | update `idx` in swap, push, pop, remove |
| infinite loop only at `-O2` | unsynchronised read of `stop` | read it under `stop_lock` |
| burnout late, coders' timestamps also skip | whole process stalled (VM) | `systemd-detect-virt`; test on real hardware |
| burnout late, only the monitor idle | relative sleep drifting | absolute deadline via `timedwait` |
| stale build behaves like a race | header not a Make dependency | `%.o: %.c $(HDRS)` |
| links here, fails elsewhere | `-pthread` missing from the link rule | `$(CFLAGS)` on both rules |
| `TOO_MANY_LINES` | function over 25 lines | extract a sub-operation, don't compress |
