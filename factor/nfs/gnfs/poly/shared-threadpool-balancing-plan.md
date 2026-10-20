# Dynamic Stage-1 / Stage-2 Load Balancing — Shared Threadpool (Option B)

Alternative to `dynamic-stage2-balancing-plan.md`'s elastic-borrowing approach
(Option A). Where Option A keeps separate stage-1/stage-2 pools and adds a
non-blocking steal primitive, Option B removes the pool split entirely: one
`N`-worker threadpool, one priority-ordered task queue, stage-2 tasks always
win. GPU stage-1 engines are unaffected either way — they run outside this
pool and feed hits in via the existing `handle_collision` path.

---

## 1. Task model

One queue, two task kinds:

```c
enum task_kind { TASK_STAGE1_CHUNK, TASK_STAGE2_HIT };
```

- `TASK_STAGE1_CHUNK` — search one q-sub-window for one `a_d`, then re-enqueue
  its own continuation (or move to the next `a_d`). Replaces the persistent
  per-producer loop in `search_coeff_core`.
- `TASK_STAGE2_HIT` — same payload as today's `stage1_hit_data_t`.

**Priority rule:** stage-2 tasks always dequeue before stage-1 tasks. This is
the entire balancing mechanism — no watermark, no borrow trigger, no fairness/
aging logic needed, because stage-2 priority is the explicit design goal
(finish polynomials), not a load-balance compromise.

## 2. Stage-1 progression is queue-resident, not thread-resident

`a_d` ownership becomes state that travels through the queue instead of
living on a producer thread's stack:

```c
typedef struct {
    mp_t     ad;
    uint32_t q_cursor;      /* resume point in [special_q_min2, special_q_max2) */
    uint64_t deadline_end;  /* absolute wall-clock deadline, set once per a_d */
} stage1_chunk_state_t;
```

Worker executing a `TASK_STAGE1_CHUNK`:
1. Run `v->specialq()` over one chunk starting at `q_cursor`.
2. Any collisions → `handle_collision` → enqueue `TASK_STAGE2_HIT` (high
   priority).
3. Check `deadline_end` (wall-clock — see §5); if exceeded or window
   exhausted, call `find_next_ad` and reset cursor; else advance `q_cursor`.
4. Re-enqueue itself as a new `TASK_STAGE1_CHUNK` (low priority) rather than
   looping in place, so a pending stage-2 task can jump ahead on the next
   dequeue.

`num_stage1_slots` in-flight continuations are seeded once at startup and
perpetuate 1:1 — no separate slot-throttling logic (see §4: not needed, since
workers should never idle).

## 3. Stage-2 bundles: bundle pool, not per-thread bundle

`stage2_worker_t` (private `sizeopt_data`/`rootopt_data`) can no longer be
indexed by thread identity — any worker can run any `TASK_STAGE2_HIT` at any
time. Replace with a fixed-size free-list:

```c
stage2_worker_t *bundle_pool[N];  /* N = total worker count; mutex-guarded free-list */
```

Sized exactly `N` — a worker can only run one task at a time, so concurrent
stage-2 executions are bounded by `N` automatically. No tunable cap, no
exhaustion/requeue handling: worst case is all `N` workers on stage-2 at once,
which is exactly `N` bundles in use, never more.

`file_lock`/stats-mutex usage inside `rootopt_callback` is unchanged — bundles
stay private per in-flight task; only the pool they're drawn from is now
shared/dynamic rather than a static array indexed by `threadid`.

## 4. Scheduling policy

- Dequeue order: check stage-2 lane first; if empty, check stage-1 lane.
- Workers never idle: stage-1 continuations are always available to fall back
  to (until the search space is genuinely exhausted), so no
  backlog-triggered throttle on stage-1 slot count is needed — holding a
  worker back from stage-1 would only produce idling, never a benefit.
- No fairness/aging rule for stage-1: starvation under sustained stage-2
  volume is working as intended, not a bug.

## 5. Timing: wall-clock, not `get_cpu_time()`

msieve's existing `get_cpu_time()` (via `getrusage(RUSAGE_SELF, ...)` on
non-Windows) measures **process-wide** user CPU time, not per-thread or
per-task. In the shared pool, an `a_d`'s successive chunks can land on
different threads interleaved with unrelated stage-1 and stage-2 work, so a
`get_cpu_time()` delta doesn't isolate this `a_d`'s time. Even
`RUSAGE_THREAD` wouldn't fix it, since it's current-thread-only and chunks
move between threads.

Use wall-clock elapsed time instead:

```c
double get_wall_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
```

Add as a new function alongside `get_cpu_time()` in `factor/shared/common/util.c` (don't
change `get_cpu_time()`'s semantics — other callers may depend on it). Set
`deadline_end = get_wall_time() + coeff_deadline` once per `a_d`; compare
against `get_wall_time()` at each chunk boundary.

## 6. `-t 1` degeneration

Free by construction: one worker, one queue, same priority rule. It runs any
pending `TASK_STAGE2_HIT` before the next `TASK_STAGE1_CHUNK`. No dedicated-
pool-size-0 special case, no inline-fallback deadlock avoidance, no bundle-
index partitioning — all of which Option A needs specifically to handle
`N=1` safely.

## 7. Early-abort interaction

Once a stage-2 task reports Murphy-E above the abort threshold: stop pulling
new tasks from the stage-1 lane (just don't dequeue/re-enqueue further
`TASK_STAGE1_CHUNK`s) and let already-in-flight stage-2 tasks finish. Because
stage-1 state lives entirely in queued task payloads rather than producer
thread stacks, "drop remaining stage-1 work" is just clearing that lane —
no thread cancellation or join logic, and no need to interrupt a chunk mid-
`specialq()`; in-flight chunks simply finish and their continuations aren't
re-queued.

## 8. GPU path

Unchanged. GPU engines run on their own thread(s) outside the shared pool and
call `handle_collision` → enqueues `TASK_STAGE2_HIT` into the same shared
queue. The `cpu_bound` vtable flag from Option A becomes unnecessary — GPU
engines were never pool workers under this design in the first place.

## 9. Comparison to Option A (elastic borrowing)

| | Option A | Option B (this plan) |
|---|---|---|
| Producer identity | fixed per OS thread | none — `a_d` state travels as task payload |
| Stage-2 concurrency bound | `stage2_threads` (thread count) | fixed at `N`, no tunable |
| Balancing mechanism | watermark-triggered steal at chunk checkpoints | implicit — priority dequeue |
| New primitive needed | `threadpool_try_run_task` | priority-aware dequeue |
| `N=1` handling | special-cased (dedicated=0, inline fallback, index partitioning) | free by construction |
| Early abort | not addressed | trivial — clear stage-1 lane, let stage-2 drain |
| Code churn | moderate, additive | larger — bundle-per-thread → bundle pool; producer loop → continuation tasks |

---

## Proposed code changes

### `thread.c` / `thread.h`
- Replace single FIFO with a two-lane (or single-list-with-priority-field)
  queue; dequeue always checks the stage-2 lane first.
- `worker_thr_routine` becomes a flat "dequeue highest-priority task, run it,
  repeat" loop — no fixed producer/consumer roles.
- No `threadpool_try_run_task` needed (that primitive is Option-A-specific).

### `stage1.c` — `search_coeff_core`
- Replace the single blocking `v->specialq()` call over the whole window with
  chunked execution driven by `TASK_STAGE1_CHUNK` payloads (`stage1_chunk_state_t`
  above): one chunk per task invocation, self-re-enqueuing continuation.
- `stage1_engine_cell_fits` check moves inside the per-chunk path.
- Deadline check per chunk boundary using `get_wall_time()` (§5) against
  `deadline_end`, computed once when an `a_d` is picked.

### `search_coeffs` (same file)
- Parse `coeff_deadline=` from `obj->nfs_args` (replacing the hardcoded
  `deadline_per_coeff = 8640000`); seed `num_stage1_slots` continuation tasks
  at startup.

### `factor/shared/common/util.c`
- Add `get_wall_time()` (`CLOCK_MONOTONIC`-based), alongside the existing
  `get_cpu_time()` — don't modify `get_cpu_time()` itself.

### `poly_skew.c`
- Replace `stage2_workers[]` static per-thread array with `bundle_pool[N]`
  free-list (§3): acquire on `TASK_STAGE2_HIT` dequeue, release after
  `stage1_hit_run`/callback chain completes.
- `stage2_threads=` parameter is superseded — no dedicated-pool sizing logic
  needed; pool is just `N` workers total.

### `stage1_engine.h` (vtable)
- `cpu_bound` flag from Option A is not needed here — omit.

### `poly_stats.{c,h}`
- `poly_stats_backlog()` accessor is optional under this design (no
  watermark-driven throttle), but may still be useful for the roll-up
  display line. Keep if cheap; not required for the balancing logic itself.

### Early-abort path (wherever it currently lives / will be added per phase 3)
- On abort trigger: stop dequeuing/re-enqueuing `TASK_STAGE1_CHUNK`; let
  in-flight `TASK_STAGE2_HIT` executions drain naturally.

---

> 这一节逐条对着当前代码核过一遍（2026-10-20）。状态有五种：
> **已落地**（代码里已经是这样做的）、**选型变了**（代码里换成了另一种做法）、
> **不适用**（对应的功能不该启用，这件事本身没有值可定）、
> **确实待定**（代码里需要运行时决定，或有多个候选没定下来）、
> **功能不存在**（代码里没有这个东西）。
>
> 有一件事影响下面好几条：**分块（chunking）的那套实现目前在树里是被
> `if (0)` 关着的**（`factor/nfs/gnfs/poly/stage1/stage1.cpp:740` 那一整块）。
> 运行时走的是 `:799-814` 的整窗一次性调用，真正生效的 deadline 强制在
> 引擎内层循环里（`stage1_sieve_cpu.cpp:1349-1365`，每 16384 个 q-root
> 查一次墙钟）。所以凡是以"分块"为前提的条目，分块尺寸那两个常量
> （`STAGE1_QCHUNK_DIVISOR` / `STAGE1_QCHUNK_MAX`）目前不影响运行。

## TODO / open items

1. Priority queue implementation — **功能不存在**：从未实现。代码保留的是
   msieve 原生的两个独立线程池（stage-1 池 `stage1.cpp:583-585`、
   stage-2 池 `:590-591`），单 FIFO 队列（`factor/shared/common/thread.cpp:526`），
   没有优先级字段也没有双 lane。这个设计已被 v2 方案取代。

2. Confirm `stage1_engine_cell_fits` is safe/cheap to call per-chunk —
   **已落地**：与文档一第 4 条同一结论 —— 每个 `a_d` 只调一次，放在循环外
   （`stage1.cpp:805`）；钳制只针对引擎绝对上限，与块大小无关
   （`stage1_engine.cpp:177-186`）。

3. Pick chunk-size tunables analogous to Option A's `QCHUNK_DIVISOR`/
   `QCHUNK_MAX` — **不适用（分块不该启用）**：同一组硬编码（20 / 2000000，
   `stage1.cpp:629-630`），没有命令行参数，只被关着的 `if (0)` 块引用。
   分块唯一买到的东西（deadline 在块边界及时生效）在整窗调用下已经由
   引擎内层的 16384 粒度检查做到（`stage1_sieve_cpu.cpp:1349-1365`），
   而分块要多付 19 次 p 等差数列重建 —— 直接测重建耗时，p[1000,2400] 时
   0.036 ms 一次、p[100000,240000] 时 12.1 ms 一次。
   （分块实现本身的 deadline 语义 bug 已于 2026-10-20 修好，详见
   `dynamic-stage2-balancing-plan.md` 同一节；修好后结论不变。）

4. Decide whether "clear the stage-1 lane" on early abort should also try
   to interrupt an in-flight chunk — **选型变了**：代码会打断，而且比"块边界"
   更细：`MSIEVE_FLAG_STOP_SIEVING` 由引擎内层循环检查 —— CPU 引擎每
   16384 个 q-root 查一次并 `goto finished`（`stage1_sieve_cpu.cpp:1349-1365`），
   hashtable 引擎每块查一次并 break
   （`stage1_sieve_cpu_hashtable.cpp:559-564,667-672`）。

5. Standalone concurrency test — **功能不存在**：同文档一第 7 条 ——
   原 `s2_pattern_test.c` 已在 `e341861` 删除，树内无替代。

6. Wire `get_wall_time()` into msieve's build — **已落地**：`get_wall_time()`
   已在 `util.cpp` 里与 `get_cpu_time()` 并存（`factor/shared/common/util.cpp:98-104`，
   用 `clock_gettime(CLOCK_MONOTONIC, ...)`），头文件 `util.h:135` 已声明，
   stage-1 侧多处调用（`stage1.cpp:766,781,878,897`、
   `stage1_sieve_cpu.cpp:1211,1361`）。
