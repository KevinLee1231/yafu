# Stage-1 Chunking with Inline Stage-2 (CPU path) — Revised Plan

Supersedes the shared-priority-queue sketch (Option B) for the **CPU-only**
case. The queue-based design solved a problem CPU-only doesn't actually have:
letting an idle thread help drain someone else's backlog. On CPU, every
thread is always either searching or running stage 2 — there's no idle
thread to borrow work onto, so the coordination machinery (shared queue,
priority lanes, bundle pool) is unnecessary overhead. What CPU *does* still
need from the original elastic-balancing plan is finer-grained deadline
enforcement (`coeff_deadline` is currently dead code — see
`dynamic-stage2-balancing-plan.md` intro). This plan keeps chunking for that
purpose only, and removes load-balancing-via-queue entirely for CPU.

**GPU stage-1 is unaffected and out of scope here** — it keeps the existing
dedicated `stage2_threadpool` with its own consumer threads (see §6).

---

## 1. Core idea

- A thread still **owns one `a_d` for its lifetime**, exactly as today
  (`search_coeff_async` submits one task per `a_d` to `d->stage1_threadpool`,
  which naturally caps in-flight `a_d`s at `num_threads` via blocking
  submission).
- Inside `search_coeff_core`, the single whole-window call to
  `v->specialq()` is replaced by a **loop over smaller q-sub-windows**
  (chunks), all still executed by the *same* thread, in the *same* call
  stack — no re-queuing, no task handoff between threads.
- After each chunk: check the wall-clock deadline (§4) and stop early if
  exceeded — this is the actual bug fix from the original plan
  (`coeff_deadline` currently isn't read anywhere).
- **Any collision found within a chunk runs stage 2 immediately, inline, on
  the same thread**, before the loop continues to the next chunk. No
  stage-2 threadpool submission, no separate consumer thread, no bundle
  pool.

Because the owning thread never changes for a given `a_d`, all the
complications from the queue-based design disappear:
- `d->threads[threadid].sieve_q_fb` / `sieve_p_fb` (set up once per `a_d` in
  `search_coeff_core`) stay valid across all of that `a_d`'s chunks — no
  per-chunk reinit, no state needing to travel with a task payload.
- Stage-2 working state (`sizeopt_data`/`rootopt_data`) can be **permanent
  per-thread state**, allocated once (already the shape of
  `thread_data_init`/`thread_data_free` in the engine vtable), not a
  pooled/acquired resource. No bundle pool, no `N`-sizing, no acquire/
  release.
- No priority queue, no `TASK_STAGE1_CHUNK`/`TASK_STAGE2_HIT` task kinds —
  those existed only to let stage-2 work move to a different thread than
  the one that produced it. It never needs to now.

## 2. What `search_coeff_core` looks like

Today: one call, `v->specialq(task, threadid, q_min, q_max, p_min, p_max)`,
covering the whole `[special_q_min2, special_q_max2)` window.

Revised: chunk that window and loop, still inside `search_coeff_core`:

```c
uint64 chunk_lo = q_min;
while (chunk_lo < q_max) {
    uint64 chunk_hi = MIN(q_max, chunk_lo + q_chunk_size);

    v->specialq(task, threadid, chunk_lo, chunk_hi, p_min, p_max);
    /* any collisions inside this call already ran stage 2 to completion
       before specialq() returns — see §3 */

    if (get_wall_time() >= task->deadline_end)
        break;

    chunk_lo = chunk_hi;
}
```

`stage1_engine_cell_fits`'s clamp still applies once up front to `q_max`
(the envelope check is about the engine's absolute cap, not about chunk
size, so it doesn't need to move inside the loop).

`q_chunk_size` is a new tunable (analogous to the original plan's
`QCHUNK_DIVISOR`/`QCHUNK_MAX`) — needs benchmarking; too small adds
per-chunk call overhead for little deadline-precision benefit, too large
defeats the point of chunking.

## 3. What `handle_collision` looks like

Today: builds a `stage1_hit_data_t`, submits it to `d->stage2_threadpool`
via blocking `threadpool_add_task`; a separate consumer thread eventually
runs `stage1_hit_run`, which picks stage-2 state via `stage2_workers[threadid]`.

Revised (CPU path): no task object, no submission, no separate consumer.
`handle_collision` calls the stage-2 pipeline directly, using *this*
thread's own permanent stage-2 state:

```c
/* instead of building hit_data + threadpool_add_task(d->stage2_threadpool, ...) */
d->poly->callback(c->high_coeff, c->p, c->m,
                   &d->threads[threadid].sizeopt_data);

if (d->stats) {
    poly_stats_add_hit(d->stats);
    poly_stats_stage2_done(d->stats);   /* completes synchronously now */
}
```

This runs the same `stage1_callback → poly_sizeopt_run → sizeopt_callback →
poly_rootopt_run → rootopt_callback` chain as today, just inline instead of
dispatched — the callback chain itself, the `file_lock`-protected `.p`
write, and the stats mutex are all **unchanged** (§7). Only the dispatch
mechanism (queue vs. direct call) goes away.

`stage1_hit_run` and `stage1_hit_data_t` are no longer needed for the CPU
path — they still exist for the `-np1`/single-worker fallback and for GPU
(§6).

## 3a. Where permanent per-thread stage-2 state lives

`stage1_sieve_data_t`'s existing `stage2_workers[]`/`num_stage2_workers`
array is sized and indexed for the **GPU consumer pool**
(`stage1_hit_run` looks up `stage2_workers[threadid]` where `threadid` is a
*consumer* thread id from `stage2_threadpool`). Reusing it for CPU producer
threads would conflate two differently-sized index spaces (`num_threads` vs
`num_stage2_workers`). Instead, add a new field to
`stage1_sieve_thread_data_t` (which is already indexed by producer
`threadid` via `d->threads[]`):

```c
typedef struct {
    void   *sieve_p_fb;
    void   *sieve_q_fb;
    double  cumulative_elapsed;
    void   *hw_thread_data;
} stage1_sieve_thread_data_t;
```

（这四���就是现在的全部字段 —— 计划里"给每个生产者线程加一个
`stage2_local`"的做法没有被采纳。）

CPU 路径改为复用共享的 `d->stage2_workers[threadid].sizeopt_data`：
`stage2_workers[]` 原本是按 GPU 消费者索引的，现在把数组撑到不小于
生产者线程数（`num_s2 = MAX(num_s2, obj->num_threads)`，
`factor/nfs/gnfs/poly/poly_skew.cpp:490-492`），CPU 线程按自己的
threadid 取同一块空间。下标不越界由这个抬高保证。

## 4. Deadline: wall-clock, computed once per `a_d`

Same reasoning as before: msieve's `get_cpu_time()` is process-wide
(`getrusage(RUSAGE_SELF, ...)`), not per-thread, so it can't isolate one
`a_d`'s elapsed time even in the *simpler* per-thread-owns-its-`a_d` model —
other threads' CPU consumption still pollutes the delta. Wall-clock is
still the right tool:

```c
double get_wall_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
```

Add alongside (not replacing) `get_cpu_time()` in `factor/shared/common/util.c`.

No new struct field is needed: `task_data_t.coeff_deadline` is already a
plain duration (`uint32`), so `search_coeff_core` just computes a local
`double deadline_end = get_wall_time() + task->coeff_deadline;` once at the
top of the function and checks it at each chunk boundary (§2). Nothing needs
to persist beyond that single call.

This directly fixes the original bug: `coeff_deadline` is stored on
`task_data_t` today but nothing ever reads it, so a thread can currently
sit on one `a_d` indefinitely. Chunking is *for* creating checkpoints where
that field finally gets checked.

## 5. `search_coeffs` / `search_coeff_async` — unchanged in shape

The outer `a_d`-selection loop and the one-task-per-`a_d` submission to
`d->stage1_threadpool` don't need to change at all — a thread still gets
handed exactly one `a_d` per task, exactly as today. The only change here is
parsing `coeff_deadline=` from `obj->nfs_args` instead of the hardcoded
`deadline_per_coeff = 8640000`, so the deadline is actually configurable
(it was already being *passed through* to `task->coeff_deadline`, just
never enforced).

## 6. GPU path — unchanged, dedicated stage-2 pool retained

GPU engines keep `d->stage2_threadpool` and its consumer threads exactly as
they work today. Rationale (per the original discussion): one GPU producer
generates enough hits to keep several CPU-side stage-2 consumers busy, and
the GPU thread must stay feeding the device rather than doing root-opt
itself. `handle_collision` needs a branch (or the vtable's existing
`envelope.is_gpu` flag) to pick inline-call (CPU) vs. threadpool-submit
(GPU) behavior. `stage1_hit_run` and `stage1_hit_data_t` stay exactly as
they are today for this path — no changes there at all.

## 7. What doesn't change

- `rootopt_callback`'s `file_lock`-protected `.p` write and `save_poly`.
- The stats mutex and `poly_stats_add_hit`/`add_sizeopt`/`add_rootopt`/
  `stage2_done` counters and semantics.
- `stage1_engine_vtable_t`, `specialq`'s signature, `stage1_engine_cell_fits`.
- The GPU consumer-pool code path in its entirety.
- `build_stage2_workers()` — still used for GPU; not needed for CPU threads,
  whose stage-2 state instead comes from the existing per-thread
  `thread_data_init`/`thread_data_free` slots (`d->threads[threadid]`),
  extended to also hold `sizeopt_data`/`rootopt_data` permanently.

## 8. Early-abort interaction

Simpler than either prior design: a thread checks shared best-E (mutex-read,
already tracked in `poly_stage_stats_t`) at the top of each `a_d` (before
calling `find_next_ad` again) or at each chunk boundary. If the abort
threshold is met, the thread just stops requesting more work — no queue to
clear, no in-flight task to let drain, since "in-flight" now just means
"this thread's current call stack," which naturally unwinds once
`search_coeff_core` returns.

## 9. Comparison across all three designs

| | Elastic borrowing (Option A) | Shared priority queue (Option B) | Inline stage-2 + chunking (this plan) |
|---|---|---|---|
| Stage-2 dispatch | dedicated pool + steal | shared queue, priority order | inline, same thread, no pool |
| Stage-2 state | per-worker bundle, fixed array | pooled, acquired per task | permanent per-thread, like today |
| New primitive | `threadpool_try_run_task` | priority-aware dequeue | none |
| Chunking purpose | balancing checkpoint + deadline | balancing checkpoint + deadline | deadline only |
| `N=1` handling | special-cased | free by construction | free by construction (always was) |
| Cross-thread work-stealing | yes | yes | no — each thread's hits stay on that thread |
| Code churn | moderate | large | small |

The tradeoff accepted here: no cross-thread help if one thread's `a_d`
produces a burst of collisions (it works through them serially via root-opt
fan-out while other threads keep progressing their own `a_d`s). Given each
thread independently cycles through many `a_d`s over a run, this should
average out; worth confirming with the standalone concurrency/benchmark
test (§10, item 3).

---

## Proposed code changes

### `factor/shared/common/util.c`
- Add `get_wall_time()` (`CLOCK_MONOTONIC`), alongside existing
  `get_cpu_time()` — don't change the latter's semantics.

### `stage1.c` — `search_coeff_core`
- Replace the single whole-window `v->specialq()` call with a chunked loop
  (§2): `q_chunk_size`-sized sub-windows, wall-clock deadline check after
  each chunk via `task->deadline_end`.
- `stage1_engine_cell_fits` clamp stays where it is (once, before the loop).

### `stage1.c` — `search_coeffs`
- Parse `coeff_deadline=` from `obj->nfs_args`, replacing the hardcoded
  `deadline_per_coeff = 8640000`.

### `stage1.c` — `handle_collision`
- CPU path: replace `stage1_hit_data_t` construction + 
  `threadpool_add_task(d->stage2_threadpool, ...)` with a direct call to
  `d->poly->callback(...)` using `d->threads[threadid]`'s permanent
  sizeopt state (§3), followed by inline stats updates.
- GPU path (via `v->envelope.is_gpu` or equivalent): unchanged, still
  submits to `d->stage2_threadpool`.

### `stage1.h` — `stage1_sieve_thread_data_t`
- 不加字段。CPU 路径的 sizeopt/rootopt 状态复用 `stage2_workers[]` 里按
  threadid 取的那一份（`factor/nfs/gnfs/poly/stage1/stage1.cpp:233-235`），
  数组大小由 `poly_skew.cpp:490-492` 抬到不小于线程数来保证不越界。

### `poly_skew.c`
- `build_stage2_workers()` / `stage2_workers[]` retained but now only
  populated/used for the GPU path's dedicated consumer pool; CPU threads
  don't participate in it.

### Untouched
- `poly_stats.{c,h}`, `rootopt_callback`, `file_lock`, `.p` file writing,
  `stage1_engine_vtable_t` signatures, GPU consumer pool code.

---

> 这一节逐条对着当前代码核过一遍（2026-10-20）。状态只有四种：
> **已落地**（代码里已经是这样做的）、**选型变了**（代码里换成了另一种做法）、
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

1. Pick `q_chunk_size` — **确实待定**：硬编码成
   `STAGE1_QCHUNK_DIVISOR 20` / `STAGE1_QCHUNK_MAX 2000000`
   （`stage1.cpp:629-630`），只在 `if (0)` 关着的分块实现里用
   （`stage1.cpp:763-769`）；代码里没有 `q_chunk_size` 这个名字。
   分块实现当前没有启用，运行时走整窗路径（`stage1.cpp:799-814`），
   真正生效的 deadline 强制在引擎内层（`stage1_sieve_cpu.cpp:1349-1365`）。

2. ~~Confirm `find_next_ad`'s locking~~ — **已落地**：`find_next_ad` 定义在
   `stage1.cpp:364`，只被 `search_coeffs` 里的驱动线程调用
   （`stage1.cpp:966,1094,1112`）；工作线程只走
   `task_data_run → search_coeff_core`（`stage1.cpp:860-865`），从不调用它。
   没有加锁的必要，也没改。

3. Benchmark whether losing cross-thread stage-2 work-stealing has any
   measurable throughput cost — **选型变了**：问题本身消解了。最终设计里
   不存在跨线程的 stage-2 偷取 —— 每个生产者线程内联处理自己的命中
   （`stage1.cpp:230-241`），只有 GPU 路径才走池（`stage1.cpp:208-228`），
   没有可比的代价。

4. ~~Decide where `sizeopt_data`/`rootopt_data` allocation lives~~ —
   **已落地**（本节原先写的做法与代码不符，已按代码改正）：并没有新增
   `stage2_worker_t stage2_local` 字段。`stage1_sieve_thread_data_t` 仍是原来
   那四项（`stage1.h:219-225`）；CPU 路径改为复用共享的
   `d->stage2_workers[threadid].sizeopt_data`（`stage1.cpp:233-235`），
   靠把数组撑到不小于线程数来保证下标不越界
   （`num_s2 = MAX(num_s2, obj->num_threads)`，`poly_skew.cpp:490-492`；
   数组构建 `poly_skew.cpp:139-194`）。

5. `handle_collision`'s CPU/GPU branch — **已落地**：`envelope.is_gpu` 够用，
   代码就是用它分流的（GPU 提交 `stage1.cpp:208`，CPU 内联 `:230-241`），
   vtable 没有新增"dispatch mode"字段（`stage1_engine.h:42-63`）。

6. Standalone test: verify deadline enforcement terminates a long-running
   `a_d`, and that inline stage-2 + chunking produces identical `.p` output —
   **功能不存在**：没有这样的测试。分块代码被 `if (0)` 关闭
   （`stage1.cpp:740`），运行期不存在"块边界终止"行为；实际生效的 deadline
   终止在 CPU 引擎内层（`stage1_sieve_cpu.cpp:1349-1365`），也没有 `.p`
   输出比对的用例。

7. Confirm early-abort's best-E check frequency — **已落地**：实际是每保存
   一个多项式检查一次，位置在 `poly_stats_add_rootopt`（持 stats 锁，与漏斗
   计数器同一把锁，`poly_stats.cpp:187-210`，检查在 `:203-206`）—— 既不是
   每块也不是每 `a_d`；没有往热循环里新增无锁读。超阈值只置
   `MSIEVE_FLAG_STOP_SIEVING`，引擎无锁读该标志
   （`stage1_sieve_cpu.cpp:1349`）。
