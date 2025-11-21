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

`
Slowdown Factor = Mutex_Time / Original_Time
Overhead Percentage = ((Mutex_Time - Original_Time) / Original_Time) × 100%
`

**Results:**

| Threads | Original Time | Mutex Time | Slowdown Factor | Overhead Percentage |
|---------|---------------|------------|-----------------|---------------------|
| 1       | 6.350311s     | 6.204864s  | 0.98×           | -2.3%               |
| 2       | 3.179568s     | 8.664895s  | 2.73×           | +172.5%             |
| 4       | 1.951982s     | 9.015849s  | 4.62×           | +361.9%             |
| 8       | 2.121027s     | 9.378774s  | **4.42×**       | **+342.2%**         |

**At 8 threads:**

`
Slowdown Factor = 9.378774s / 2.121027s = 4.42
Overhead = ((9.378774 - 2.121027) / 2.121027) × 100%
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

```markdown
Overhead = ((Spinlock_Time - Original_Time) / Original_Time) × 100%
```

**Results:**

| Threads | Original | Spinlock | Slowdown Factor | Overhead |
|---------|----------|----------|-----------------|----------|
| 1       | 6.35s    | 6.42s    | 1.01×           | +1.1%    |
| 2       | 3.18s    | 6.10s    | 1.92×           | +91.8%   |
| 4       | 1.95s    | 6.54s    | 3.35×           | +235.3%  |
| 8       | 2.12s    | 18.11s   | **8.54×**       | **+754%**|

**At 8 threads:** `(18.11 - 2.12) / 2.12 × 100% = 754%`

### How We Estimated this Overhead

We ran all three implementations (original, mutex, spinlock) with identical workloads and compared retrieve times at each thread count. The spinlock overhead is calculated relative to the original to show the total synchronization cost.

### Explanation of Overhead for Spinlock

At 8 threads, the spinlock version is **8.54× slower** than the original (754% overhead) and **1.93× slower** than mutex (93% overhead). This catastrophic performance degradation occurs because:

- **Busy-waiting:** 7 threads continuously spin while 1 thread holds the lock, wasting CPU cycles
- **CPU contention:** Spinning threads compete with the working thread for CPU time on limited cores
- **Cache thrashing:** Lock variable bounces between cores, causing excessive cache invalidations
- **No yield:** Unlike mutex (which sleeps), spinlock never releases CPU, causing the scheduler to fight with spinning threads

The overhead explodes from 1.1% (1 thread) to 754% (8 threads) because contention grows exponentially. Spinlock performs **worse than single-threaded** (18.11s > 6.42s baseline), proving that busy-waiting under high contention is counter-productive.

## Part 3: Retrieve Parallelization [20 Points]

### Do We Need a Lock for Retrieval?

**Yes, we need a lock for retrieval**, but we can optimize how we use locks.

#### Why We Need Locks for Reading

Even though `retrieve()` only reads data, concurrent writes could be modifying the linked list structure while a read is in progress. Without synchronization:

- A thread traversing a linked list (following `next` pointers) could see **inconsistent state**
- A concurrent `insert()` could modify `table[i]` or `next` pointers mid-traversal
- This could cause reads to follow **invalid pointers**, segfault, or miss entries
- Even pure reads need protection when concurrent writes are possible

#### Optimization Opportunity

We don't need to lock the **entire table** for retrieval. Key insight: **Multiple retrievals from different buckets can safely run in parallel** since each bucket is an independent linked list.

### Implementation Changes

**Original approach (Part 1):**

```c
pthread_mutex_t table_mutex;  // One lock for everything - ALL operations serialize
```

**Optimized approach (Part 3):**

```c
pthread_mutex_t bucket_locks[NUM_BUCKETS];  // One lock per bucket
```

**In `retrieve()`:**

```c
bucket_entry * retrieve(int key) {
  int i = key % NUM_BUCKETS;
  pthread_mutex_lock(&bucket_locks[i]);  // Lock ONLY this bucket
  
  for (b = table[i]; b != NULL; b = b->next) {
    if (b->key == key) {
      pthread_mutex_unlock(&bucket_locks[i]);
      return b;
    }
  }
  
  pthread_mutex_unlock(&bucket_locks[i]);
  return NULL;
}
```

**Impact:** This allows retrievals from different buckets to run concurrently, significantly reducing contention from **100%** (all operations fight for 1 lock) to **~20%** (only operations on the same bucket contend, assuming uniform distribution across 5 buckets).

## Part 4: Insert Parallelization [20 Points]

### When Can Insertions be Safely Parallelized?

**Multiple insertions can happen safely when they target different buckets.**

#### Key Insight: What's a Bucket?

The hash table uses **separate chaining**: `table[NUM_BUCKETS]` is an array where each element is the head of an independent linked list (bucket).

```markdown
table → entryA → entryB → NULL
table → entryC → NULL
table → entryD → entryE → entryF → NULL
table → NULL
table → entryG → NULL
```

Since buckets don't share data structures:

- Inserting into bucket 0 **doesn't affect** bucket 1
- Each bucket head (`table[i]`) is independent
- Only insertions to the **same bucket** conflict

#### When Synchronization IS Needed

Insertions to the **same bucket** must serialize because they both modify the same `table[i]` head pointer, causing the race condition described in Part 1.

### Implementation Changes

**Per-bucket mutex array:**

```
pthread_mutex_t bucket_locks[NUM_BUCKETS];

// In main(): Initialize all locks
for (i = 0; i < NUM_BUCKETS; i++) {
    pthread_mutex_init(&bucket_locks[i], NULL);
}
```

**In `insert()`:**

```
void insert(int key, int val) {
  int i = key % NUM_BUCKETS;
  bucket_entry *e = (bucket_entry *) malloc(sizeof(bucket_entry));
  if (!e) panic("No memory to allocate bucket!");
  
  pthread_mutex_lock(&bucket_locks[i]);  // Lock only target bucket
  e->next = table[i];
  e->key = key;
  e->val = val;
  table[i] = e;
  pthread_mutex_unlock(&bucket_locks[i]);
}
```

**Effect:** With 5 buckets and uniform hash distribution, there's only a **20% chance** that two random operations target the same bucket. This means **80% of operations can proceed in parallel** without contention, enabling true parallelism while maintaining correctness.

---

## Performance Summary

### Final Results - All Implementations

| Threads | Original (Unsafe) | Global Mutex | Spinlock  | Per-Bucket Mutex |
|---------|-------------------|--------------|-----------|------------------|
| 1       | 6.35s             | 6.20s        | 6.42s     | 6.71s            |
| 2       | 3.18s             | 8.66s        | 6.10s     | **4.47s** ✓      |
| 4       | 1.95s             | 9.02s        | 6.54s     | **4.67s** ✓      |
| 8       | 2.12s             | 9.38s        | 18.11s    | **4.14s** ✓      |

### Performance Graph

### Key Achievements - Per-Bucket Mutex

**Correctness:** 0 keys lost (thread-safe)  
**Performance:** 1.62x speedup at 8 threads vs single-threaded  
**Efficiency:** 2.3x faster than global mutex at 8 threads  
**Scalability:** 4.4x faster than spinlock at 8 threads  

### Why Per-Bucket Locking Works

**Fine-grained locking reduces contention.** Instead of all threads competing for one lock, they only contend when accessing the same bucket (20% probability with 5 buckets and uniform hashing). This allows the benefits of parallelism while maintaining correctness.

The optimized version achieves **38.4% speedup** over single-threaded (6.71s → 4.14s) with 8 threads, while the global mutex version shows **51.2% slowdown** (6.20s → 9.38s). This demonstrates that with careful design, we can have both thread-safety and good parallel performance.

---

## Compilation and Testing

### Compile All Versions

```
gcc -pthread parallel_hashtable.c -o parallel_hashtable
gcc -pthread parallel_mutex.c -o parallel_mutex
gcc -pthread parallel_spin.c -o parallel_spin
gcc -pthread parallel_mutex_opt.c -o parallel_mutex_opt
```

### Run Tests

```
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

```
[main] Inserted 100000 keys in X.XXXXXX seconds
[thread 0] 0 keys lost!
[thread 1] 0 keys lost!
...
[main] Retrieved 100000/100000 keys in X.XXXXXX seconds
```

### Final Comparison

| Implementation    | Correctness  | Performance (8 threads) | Best Use Case                 |
|-------------------|--------------|-------------------------|-------------------------------|
| Original          | ❌ Unsafe    | 2.12s (fastest)         | Never (incorrect)             |
| Global Mutex      | ✅ Safe      | 9.38s (slow)            | Simple, low-performance needs |
| Spinlock          | ✅ Safe      | 18.11s (very slow)      | Low contention only           |
| Per-Bucket Mutex  | ✅ Safe      | 4.14s **(best!)**       | Production use                |

The optimized per-bucket approach shows that with careful design, we can achieve both **thread-safety and good parallel performance**. The key is identifying opportunities for fine-grained locking where operations on different data structures can proceed independently.
