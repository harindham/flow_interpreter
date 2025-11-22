# Assignment-04 Concurrency and Thread-Safe Hash Table

## Table of Contents

- [Overview](#overview)
- [Part 1: Mutex Implementation [30 Points]](#part-1-mutex-implementation-30-points)
- [Part 2: Spinlock Implementation [30 Points]](#part-2-spinlock-implementation-30-points)
- [Part 3: Retrieve Parallelization [20 Points]](#part-3-retrieve-parallelization-20-points)
- [Part 4: Insert Parallelization [20 Points]](#part-4-insert-parallelization-20-points)
- [Performance Summary](#performance-summary)
- [Compilation and Testing](#compilation-and-testing)
- [Assumptions](#assumptions)
- [Conclusions](#conclusions)

## Overview

This assignment implements thread-safe versions of a hash table using different synchronization mechanisms: global mutexes, spinlocks, and optimized per-bucket locking. The goal is to understand race conditions, synchronization overhead, and parallelization strategies in concurrent programming.

## Part 1: Mutex Implementation [30 Points]

### Analysis: What Causes Entries to be "Lost"?

An entry is considered **"lost"** when it is successfully inserted into the hash table but cannot be retrieved afterward. This happens due to a **race condition** in the `insert()` function.

#### The Race Condition

```c
void insert(int key, int val) {
  int i = key % NUM_BUCKETS;
  bucket_entry *e = (bucket_entry *) malloc(sizeof(bucket_entry));
  e->next = table[i];    // ← Read current head pointer
  e->key = key;
  e->val = val;
  table[i] = e;          // ← Write new head pointer
}
```

#### Why Entries Get Lost

When multiple threads simultaneously insert into the same bucket, they perform a **read-modify-write sequence** without synchronization. Consider this timeline:

1. **Thread A** reads `table[0]` → sees `NULL` (or previous head)
2. **Thread B** reads `table[0]` → sees the **same value**
3. **Thread A** writes `table[0] = entryA`
4. **Thread B** writes `table[0] = entryB` (**overwrites A's pointer!**)
5. **EntryA becomes unreachable** - it's **lost** in memory

The linked list structure means we need **atomicity** for the entire read-modify-write operation. Without synchronization, concurrent modifications to the same bucket head pointer cause entries to become disconnected from the list and unreachable.

**Parts of the program causing this:**

- **Primary culprit:** The `insert()` function's non-atomic read-modify-write of `table[i]`
- **Secondary issue:** The `retrieve()` function can also see inconsistent state during concurrent insertions

### Implementation

Added a global `pthread_mutex_t table_mutex` that protects all hash table operations. Both `insert()` and `retrieve()` acquire this lock before accessing the table, ensuring mutual exclusion.

**Key Changes:**

```c
pthread_mutex_t table_mutex;  // Global mutex

// In main()
pthread_mutex_init(&table_mutex, NULL);

// In insert() and retrieve()
pthread_mutex_lock(&table_mutex);
// ... critical section ...
pthread_mutex_unlock(&table_mutex);
```

### Performance Graph: Original vs Mutex

![Part 1 Graph: Original vs Mutex Performance Comparison](images/Graph%201.png)
**Key Observations from Graph:**

- Original (turquoise): Gets faster with threads (6.35s → 2.12s)
- Mutex (dark blue): Gets slower with threads (6.20s → 9.38s)

### Performance Results for Mutex

**Original (Unsafe) Version:**

| Threads | Insert Time | Retrieve Time | Keys Retrieved | Keys Lost |
|---------|-------------|---------------|----------------|-----------|
| 1       | 0.0067s     | 6.350311s     | 100000/100000  | 0         |
| 2       | 0.0043s     | 3.179568s     | 99179/100000   | 821       |
| 4       | 0.0047s     | 1.951982s     | 98696/100000   | 1,304     |
| 8       | 0.0062s     | 2.121027s     | 98907/100000   | 1,093     |

**Global Mutex (Safe) Version:**

| Threads | Insert Time | Retrieve Time | Keys Retrieved | Keys Lost |
|---------|-------------|---------------|----------------|-----------|
| 1       | 0.0110s     | 6.204864s     | 100000/100000  | 0         |
| 2       | 0.0064s     | 8.664895s     | 100000/100000  | 0         |
| 4       | 0.0082s     | 9.015849s     | 100000/100000  | 0         |
| 8       | 0.0101s     | 9.378774s     | 100000/100000  | 0         |

**Correctness achieved:** 0 keys lost across all thread counts

### Overhead Estimate for Mutex

We calculate overhead by comparing the mutex version to the **original (unsafe) version** at each thread count to show the cost of correctness.

**Calculation Formula:**

`Slowdown Factor = Mutex_Time / Original_Time`

`Overhead Percentage = ((Mutex_Time - Original_Time) / Original_Time) × 100%`

**Results:**

| Threads | Original Time | Mutex Time | Slowdown Factor | Overhead Percentage |
|---------|---------------|------------|-----------------|---------------------|
| 1       | 6.350311s     | 6.204864s  | 0.98×           | -2.3%               |
| 2       | 3.179568s     | 8.664895s  | 2.73×           | +172.5%             |
| 4       | 1.951982s     | 9.015849s  | 4.62×           | +361.9%             |
| 8       | 2.121027s     | 9.378774s  | **4.42×**       | **+342.2%**         |

**At 8 threads:**

`Slowdown Factor = 9.378774s / 2.121027s = 4.42`

`Overhead = ((9.378774 - 2.121027) / 2.121027) × 100%
= (7.257747 / 2.121027) × 100%
= 342.2%
`

### How We Estimated This

We ran both versions with identical workloads (1, 2, 4, 8 threads) and compared retrieve times at each thread count. The overhead calculation divides the time difference by the original time to show the synchronization cost as a percentage.

### Explanation of Overhead

The mutex version is **4.42× slower** at 8 threads (342% overhead). The global mutex serializes all operations - only 1 thread works while 7 wait. The original benefits from parallelism (6.35s→2.12s), while mutex gets worse (6.20s→9.38s) due to lock contention and context switching. Despite being slower, mutex achieves correctness: 0 keys lost vs 1,093 lost in the original.

## Part 2: Spinlock Implementation [30 Points]

### Hypothesis: What Will Happen with Spinlocks?

**Prediction:** Spinlocks will likely perform **worse** than mutexes for this workload, especially at higher thread counts.

**Reasoning:**

The key difference between mutexes and spinlocks is how they handle waiting:

- **Mutex:** When a thread can't acquire the lock, it **sleeps** (blocks) and the OS schedules another thread. The waiting thread releases CPU resources.
- **Spinlock:** When a thread can't acquire the lock, it **spins** in a busy-wait loop, continuously checking if the lock is available. The thread consumes 100% CPU while waiting.

**Why spinlocks will be worse:**

1. **Long critical sections:** Our hash table operations involve malloc and linked list traversal, which are relatively slow
2. **High contention:** With 8 threads competing for 1 lock, most threads will be spinning
3. **CPU waste:** Spinning threads consume CPU cycles without doing productive work
4. **Limited cores:** On a machine with 4-8 cores, spinning threads compete with the working thread for CPU time

Spinlocks are only beneficial for **very short** critical sections where `spin_time < context_switch_time`. Our critical sections are too long for this optimization.

### Performance Graph: Original vs Mutex vs Spinlock

![Part 2 Graph: Three-Way Performance Comparison](images/Graph%202.png)
**Key Observations from Graph:**

- **Original (turquoise):** Gets faster with threads (6.35s → 2.12s) but loses keys
- **Mutex (dark blue):** Gets slower with threads (6.20s → 9.38s) due to serialization
- **Spinlock (yellow):** Stays flat at 2-4 threads (~6s), outperforming mutex, but spikes catastrophically to 18.11s at 8 threads

### Performance Results

**Spinlock Version:**

| Threads | Insert Time | Retrieve Time | Keys Retrieved | Keys Lost |
|---------|-------------|---------------|----------------|-----------|
| 1       | 0.0069s     | 6.415592s     | 100000/100000  | 0         |
| 2       | 0.0041s     | 6.102441s     | 100000/100000  | 0         |
| 4       | 0.0058s     | 6.544894s     | 100000/100000  | 0         |
| 8       | 0.0061s     | 18.113673s    | 100000/100000  | 0         |

**Comparison Table:**

| Threads | Original | Mutex    | Spinlock  | Best Performance |
|---------|----------|----------|-----------|------------------|
| 1       | 6.35s    | 6.20s    | 6.42s     | Mutex            |
| 2       | 3.18s    | 8.66s    | 6.10s     | **Spinlock**     |
| 4       | 1.95s    | 9.02s    | 6.54s     | **Spinlock**     |
| 8       | 2.12s    | 9.38s    | 18.11s    | Mutex            |

### Analysis: Was the Hypothesis Correct?

**Partially correct** - the results show an interesting pattern:

#### At low thread counts (2-4): Spinlocks WIN

- 2 threads: 6.10s (spinlock) vs 8.66s (mutex) → **30% faster**
- 4 threads: 6.54s (spinlock) vs 9.02s (mutex) → **27% faster**
- Lock is held briefly, spinning is faster than context switching
- Low contention means spinning time is minimal

#### At high thread count (8): Spinlocks COLLAPSE

- 8 threads: 18.11s (spinlock) vs 9.38s (mutex) → **93% slower**
- Confirms hypothesis: high contention breaks spinlocks
- 7 threads spin wastefully while 1 thread works
- CPU fully utilized but most cycles wasted on spinning

### Overhead Estimate

We calculate overhead by comparing spinlock to the **original (unsafe) version**.

**Calculation Formula:**

`Overhead = ((Spinlock_Time - Original_Time) / Original_Time) × 100%`

**Results:**

| Threads | Original | Spinlock | Slowdown Factor | Overhead |
|---------|----------|----------|-----------------|----------|
| 1       | 6.35s    | 6.42s    | 1.01×           | +1.1%    |
| 2       | 3.18s    | 6.10s    | 1.92×           | +91.8%   |
| 4       | 1.95s    | 6.54s    | 3.35×           | +235.3%  |
| 8       | 2.12s    | 18.11s   | **8.54×**       | **+754%**|

**At 8 threads:** `(18.11 - 2.12) / 2.12 × 100% = 754%`

### How We Estimated this Overhead

We ran all three implementations (original, mutex, spinlock) with identical workloads (100,000 keys, 1/2/4/8 threads) and recorded retrieve times for each run. The spinlock overhead is calculated relative to the original unsafe version at the same thread count using the formula: `(Spinlock_Time - Original_Time) / Original_Time × 100%`. This shows the total cost of adding spinlock-based synchronization. We focused on retrieve times since they dominate performance (~6-18s vs ~0.004-0.01s for inserts).

### Explanation of Overhead for Spinlock

At 8 threads, the spinlock version is **8.54× slower** than the original (754% overhead) and **1.93× slower** than mutex (93% overhead). This catastrophic performance degradation occurs because:

- **Busy-waiting:** 7 threads continuously spin while 1 thread holds the lock, wasting CPU cycles
- **CPU contention:** Spinning threads compete with the working thread for CPU time on limited cores
- **Cache thrashing:** Lock variable bounces between cores, causing excessive cache invalidations
- **No yield:** Unlike mutex (which sleeps), spinlock never releases CPU, causing the scheduler to fight with spinning threads

The overhead explodes from 1.1% (1 thread) to 754% (8 threads) because contention grows exponentially. Spinlock performs **worse than single-threaded** (18.11s > 6.42s baseline), proving that busy-waiting under high contention is counter-productive.

## Part 3: Retrieve Parallelization [20 Points]

### Do We Need a Lock for Retrieval?

**In this specific workload, locks are not required for retrieval** because the test separates inserts and retrieves with a barrier (`pthread_join()`). During the retrieve phase, only readers access the hash table, and no modifications occur.

However, **in a general-purpose concurrent hash table** where inserts and retrieves happen simultaneously, locks would be necessary to prevent readers from seeing inconsistent state while writers modify the structure.

For this assignment, we implement per-bucket locks in `insert()` to enable parallel insertions, while `retrieve()` remains lock-free due to the phase-separated workload pattern.

### What We Changed to Enable Parallel Retrieval

**Original approach (Part 1 - Global Mutex):**

```c
pthread_mutex_t table_mutex; // ONE global lock for entire table
```

**Problem:** All operations serialize, even when accessing different buckets

**Optimized approach (Part 3 - Per-Bucket Mutex):**

```c
pthread_mutex_t bucket_locks[NUM_BUCKETS]; // ONE lock per bucket
```

**Benefit:** Operations on different buckets can run in parallel

### Implementation Changes in `parallel_mutex_opt.c`

**1. Declared per-bucket mutex array:**

```c
pthread_mutex_t bucket_locks[NUM_BUCKETS];
```

**2. Initialize all bucket locks in `main()`:**

```c
for (i = 0; i < NUM_BUCKETS; i++) {
pthread_mutex_init(&bucket_locks[i], NULL);
}
```

**3. Modified `insert()` to lock only the target bucket:**

```c
void insert(int key, int val) {
int i = key % NUM_BUCKETS;
bucket_entry *e = (bucket_entry*) malloc(sizeof(bucket_entry));
if (!e) panic("No memory to allocate bucket!");

pthread_mutex_lock(&bucket_locks[i]); // Lock ONLY bucket i
e->next = table[i];
e->key = key;
e->val = val;
table[i] = e;
pthread_mutex_unlock(&bucket_locks[i]);
}
```

**4. `retrieve()` remains lock-free:**

```c
bucket_entry *retrieve(int key) {
int i = key % NUM_BUCKETS;
bucket_entry*b;
for (b = table[i]; b != NULL; b = b->next) {
if (b->key == key) {
return b;
}
}
return NULL;
}
```

Since retrievals occur after all inserts complete, no synchronization is needed during the read-only phase.

**5. Cleanup all locks at the end:**

```c
for (i = 0; i < NUM_BUCKETS; i++) {
pthread_mutex_destroy(&bucket_locks[i]);
}
```

### Why This Enables Parallelization

**Contention reduction during insert phase:**

- **Global lock:** 100% contention - all insertions serialize
- **Per-bucket locks:** ~20% contention - only insertions to the same bucket conflict
- With 5 buckets and uniform hashing: collision probability = 1/5 = 20%

**Parallel execution during retrieve phase:**

- **No locks needed:** All threads are readers, no conflicts
- **Full parallelism:** All threads can retrieve simultaneously without any synchronization overhead
- Multiple threads can even read from the same bucket concurrently since no modifications occur

**Result:**

- Insert phase: 80% of operations proceed in parallel (different buckets)
- Retrieve phase: 100% parallelism (all readers, no locks)

**Performance impact:**

- Part 1 (global mutex): 9.38s retrieve at 8 threads
- Part 3 (per-bucket optimized): 2.81s retrieve at 8 threads (**3.3× faster**)
- Speedup from 1 to 8 threads: 7.15s → 2.81s (**2.5× parallelization gain**)

The combination of per-bucket locking for inserts and lock-free retrieval (enabled by phase separation) provides optimal performance while maintaining correctness for this workload pattern.

## Part 4: Insert Parallelization [20 Points]

### When Can Multiple Insertions Happen Safely?

**Multiple insertions can happen safely in parallel when they target different buckets.**

**Explanation:**

A **bucket** is an independent chain in the hash table. The hash table has 5 separate buckets (indices 0-4), and each bucket maintains its own linked list of entries. The key insight is that operations on different buckets are **completely independent**:

- Inserting into bucket 0 doesn't affect bucket 1's linked list
- Inserting into bucket 2 doesn't modify bucket 3's data structures
- Each bucket's head pointer (`table[i]`) is a separate memory location

**Safe parallel insertion scenario:**

```c
Thread A: insert(key=10) → bucket 0
Thread B: insert(key=23) → bucket 3
Thread C: insert(key=47) → bucket 2
Thread D: insert(key=15) → bucket 0  ← Conflicts with Thread A only

```

Threads A, B, and C can run **completely in parallel** because they access different buckets. Only Thread D must wait for Thread A (same bucket). With 5 buckets and uniform hashing, the probability of collision is only **20%** (1/5), meaning **80% of insertions can proceed in parallel**.

### What We Changed to Enable Parallel Insertion

The change is identical to Part 3 - we replaced the global lock with per-bucket locks. This optimization benefits **both** insert and retrieve operations.

**Original approach (Part 1 - Global Mutex):**

```c
pthread_mutex_t table_mutex;  // ONE lock for ALL buckets

void insert(int key, int val) {
  pthread_mutex_lock(&table_mutex);  // Serializes ALL inserts
  // ... insert logic ...
  pthread_mutex_unlock(&table_mutex);
}
```

**Problem:** All insertions serialize, even to different buckets

**Optimized approach (Part 4 - Per-Bucket Mutex):**

```c
pthread_mutex_t bucket_locks[NUM_BUCKETS];  // ONE lock per bucket

void insert(int key, int val) {
  int i = key % NUM_BUCKETS;
  pthread_mutex_lock(&bucket_locks[i]);  // Locks ONLY this bucket
  // ... insert logic ...
  pthread_mutex_unlock(&bucket_locks[i]);
}
```

**Benefit:** Insertions to different buckets run in parallel

### Implementation Details

The implementation in `parallel_mutex_opt.c` provides fine-grained locking:

**1. Per-bucket lock array:**

```c
pthread_mutex_t bucket_locks[NUM_BUCKETS];  // 5 independent locks
```

**2. Lock only the target bucket in `insert()`:**

```c
void insert(int key, int val) {
  int i = key % NUM_BUCKETS;  // Determine target bucket
  bucket_entry *e = (bucket_entry *) malloc(sizeof(bucket_entry));
  if (!e) panic("No memory to allocate bucket!");
  
  pthread_mutex_lock(&bucket_locks[i]);  // Lock ONLY bucket i
  e->next = table[i];
  e->key = key;
  e->val = val;
  table[i] = e;
  pthread_mutex_unlock(&bucket_locks[i]);
}
```

**Key design principle:** Each bucket is **independently lockable**, allowing:

- Thread-safe modifications within a bucket
- Parallel modifications across different buckets
- Minimal contention (only when threads access the same bucket)

### Why This Enables Parallelization for Insert

**Contention analysis:**

| Approach | Lock Scope | Contention | Parallelism |
|----------|-----------|------------|-------------|
| Global Mutex | Entire table | 100% | 0% (all serialize) |
| Per-Bucket Mutex | Single bucket | ~20% | ~80% |

**With 5 buckets and uniform hashing:**

- Probability two threads collide on same bucket: 1/5 = 20%
- Probability they access different buckets: 4/5 = 80%

**Parallel execution example:**

```markdown
Time T1: Thread 0 inserts to bucket 2 (locks bucket 2)
         Thread 1 inserts to bucket 0 (locks bucket 0) → PARALLEL
         Thread 2 inserts to bucket 4 (locks bucket 4) → PARALLEL
Time T2: Thread 3 inserts to bucket 2 → WAITS (bucket 2 locked)
         Thread 4 inserts to bucket 1 (locks bucket 1) → PARALLEL
```

Only Thread 3 waits; all others execute in parallel!

### Performance Impact

**Insert phase timing:**

| Threads | Global Mutex (Part 1) | Per-Bucket (Part 4) | Speedup |
|---------|----------------------|---------------------|---------|
| 1       | 0.0110s              | 0.0088s             | 1.25×   |
| 2       | 0.0064s              | 0.0056s             | 1.14×   |
| 4       | 0.0082s              | 0.0087s             | 0.94×   |
| 8       | 0.0101s              | 0.0067s             | **1.51×** |

**Key observations:**

- Insert phase is very fast (~0.006-0.01s) for both approaches
- Per-bucket optimization shows modest improvement at high thread counts
- Primary benefit is in **retrieve phase** (9.38s → 2.81s at 8 threads)
- The real win is enabling **scalable concurrent operations** without sacrificing correctness

**Why insert times are similar:**

- Insert operations are extremely fast (just prepending to linked list)
- Lock overhead is small compared to the operation
- The bottleneck is in retrieval (traversing chains), not insertion

The per-bucket design provides **correctness** (0 keys lost) while maintaining **near-optimal performance** by allowing independent operations to proceed in parallel.

## Performance Summary

### Final Results - All Implementations

| Threads | Original (Unsafe) | Global Mutex | Spinlock  | Per-Bucket Mutex |
|---------|-------------------|--------------|-----------|------------------|
| 1       | 6.35s             | 6.20s        | 6.42s     | 7.15s            |
| 2       | 3.18s             | 8.66s        | 6.10s     | **3.33s**        |
| 4       | 1.95s             | 9.02s        | 6.54s     | **2.06s**        |
| 8       | 2.12s             | 9.38s        | 18.11s    | **2.80s**        |

### Performance Graph

![Graph: Performance Comparison](images/Graph%203.jpg)

**Key Observations from Graph:**

- **Original (turquoise):** Gets faster with threads (6.35s → 2.12s) but loses keys
- **Mutex (dark blue):** Gets slower with threads (6.20s → 9.38s) due to global lock serialization
- **Spinlock (yellow):** Stays flat at 2-4 threads (~6s), then spikes catastrophically to 18.11s at 8 threads
- **Optimized (orange):** Shows true parallelization benefit - decreases from 7.15s → 2.81s at 8 threads

## Compilation and Testing

### Compile All Versions

```bash
gcc -pthread parallel_hashtable.c -o parallel_hashtable
gcc -pthread parallel_mutex.c -o parallel_mutex
gcc -pthread parallel_spin.c -o parallel_spin
gcc -pthread parallel_mutex_opt.c -o parallel_mutex_opt
```

### Run Tests

```bash
# Original (unsafe) - fast but incorrect
./parallel_hashtable 1
./parallel_hashtable 2
./parallel_hashtable 4
./parallel_hashtable 8

# Global mutex - correct but slow
./parallel_mutex 1
./parallel_mutex 2
./parallel_mutex 4
./parallel_mutex 8

# Spinlock - correct, varies by contention
./parallel_spin 1
./parallel_spin 2
./parallel_spin 4
./parallel_spin 8

# Optimized per-bucket - correct and fast!
./parallel_mutex_opt 1
./parallel_mutex_opt 2
./parallel_mutex_opt 4
./parallel_mutex_opt 8
```

### Expected Output Format

```markdown
[main] Inserted 100000 keys in X.XXXXXX seconds
[thread 0] 0 keys lost!
[thread 1] 0 keys lost!
...
[main] Retrieved 100000/100000 keys in X.XXXXXX seconds
```

### Final Comparison

The optimized per-bucket approach shows that with careful design, we can achieve both **thread-safety and good parallel performance**. The key is identifying opportunities for fine-grained locking where operations on different data structures can proceed independently.
