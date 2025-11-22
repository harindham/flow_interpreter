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

### Implementation of Spinlock

Added a global `pthread_spinlock_t table_spinlock;` that protects all hash table operations. Both `insert()` and `retrieve()` acquire this lock before accessing the table, ensuring mutual exclusion.

**Key Changes:**

```c
pthread_spinlock_t table_spinlock;  // Global spinlock

// In main()
pthread_spin_init(&table_spinlock, PTHREAD_PROCESS_PRIVATE);  // Initialize spinlock

// In insert() and retrieve()
pthread_spin_lock(&table_spinlock);
// ... critical section ...
pthread_spin_unlock(&table_spinlock);
```

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

Switching from a global lock to per-bucket locks allows retrieval operations to run in true parallel, instead of being forced through a bottleneck.

With the global mutex, every retrieve request—regardless of which bucket it touched—had to wait in line for the single lock. Even if multiple threads wanted completely different data, they couldn’t make progress together. This serialized all retrievals, wasting the potential of multi-core hardware.

The per-bucket optimization changes everything. Now, since each thread reads from its own bucket—and we use a barrier to guarantee all insertions finish before retrieval begins—every retrieval is just a read. There’s no chance of threads interfering, so there’s no need for locks during retrieval. Multiple threads can read different buckets, or even traverse the same bucket’s linked list, all at once without blocking.

This approach delivers dramatic speedup: at 8 threads, retrieval time drops from 9.38s (with a global mutex) to just 2.81s That’s real parallelism—the system is finally utilizing all available cores. The per-bucket design eliminates unnecessary serialization and takes full advantage of concurrent reads, delivering both correctness and significant performance gains.

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

The per-bucket locking strategy transforms the hash table from a single serialization bottleneck into multiple independent regions that can be accessed simultaneously.

**The key insight:** With 5 buckets, threads only conflict when they happen to insert into the **same bucket**. Since our hash function distributes keys uniformly, most threads will naturally target different buckets and can execute without waiting for each other.

**Real-world example from our testing:**

Imagine 4 threads simultaneously inserting keys:

- Thread 0 inserts key 10 → hashes to bucket 0
- Thread 1 inserts key 23 → hashes to bucket 3  
- Thread 2 inserts key 47 → hashes to bucket 2
- Thread 3 inserts key 15 → hashes to bucket 0

Threads 0, 1, and 2 run completely in parallel because they're modifying different buckets. Only Thread 3 has to wait briefly for Thread 0 to finish with bucket 0. That's 3 out of 4 threads (75%) running without any blocking.

**Why the global mutex was so bad:**

With a global lock, even though Thread 1 wants bucket 3 and Thread 2 wants bucket 2 (completely unrelated data!), they still have to wait in line behind Thread 0. It's like having one cashier at a grocery store when you could have five - people buying completely different items still have to queue up.

**Why per-bucket locking works:**

Now it's like having 5 cashiers, each handling one section of the store. Customers only wait if they both want items from the same section. Our testing confirms this: at 8 threads, we see significant speedup because most threads access different buckets and work in parallel, only occasionally colliding.

The performance improvement from 9.38s (global mutex) to 2.81s (per-bucket) at 8 threads demonstrates how effectively this reduces contention - we're now actually utilizing multiple CPU cores instead of forcing them all to wait their turn.

## Performance Summary

### Final Results - All Implementations

| Threads | Original (Unsafe) | Global Mutex | Spinlock  | Per-Bucket Mutex |
|---------|-------------------|--------------|-----------|------------------|
| 1       | 6.35s             | 6.20s        | 6.42s     | 7.15s            |
| 2       | 3.18s             | 8.66s        | 6.10s     | **3.33s**        |
| 4       | 1.95s             | 9.02s        | 6.54s     | **2.06s**        |
| 8       | 2.12s             | 9.38s        | 18.11s    | **2.80s**        |

### Performance Graph

![Graph: Performance Comparison](images/Graph%203.png)

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
