# Dynamic Stage-1 / Stage-2 Load Balancing — Plan Summary

Goal: replace the fixed `stage2_threads=S` split with elastic balancing —
CPU-bound stage-1 producers can temporarily act as stage-2 consumers when the
stage-2 queue backs up, while GPU-bound stage-1 engines never do (one GPU
producer is already enough to saturate many consumers). Chosen approach:
**Option A, elastic borrowing** — keep the existing two-pool structure from
`parallel_stage2.md`; add a non-blocking way for an idle-ish producer to steal
and run one queued stage-2 hit.

This also fixes a real, separately-discovered bug: the per-coefficient
deadline (`coeff_deadline`) is stored on `task_data_t` but never read anywhere
in `search_coeff_core` — stage 1 can currently sit indefinitely on one `a_d`.
Chunking stage-1 work (needed for borrowing checkpoints anyway) fixes this for
free.

---

## 1. Core mechanism

- `search_coeff_core` currently makes **one** blocking call to
  `v->specialq(...)` covering the whole `[special_q_min2, special_q_max2)`
  window for the chosen `a_d`. Nothing checks time or backlog until that call
  returns.
- Change: slice that window into resumable sub-chunks. After each chunk:
  1. Check `task->coeff_deadline` against elapsed time — **actually enforce
     it** (currently dead code).
  2. If the stage-2 backlog is high **and** the current engine is CPU-bound,
     try to steal and run one queued stage-2 task via a new non-blocking
     threadpool primitive.
- GPU engines (`gpu_cubsort`, `gpu_gerbicz`) skip step 2 entirely via a new
  `cpu_bound` flag on the engine vtable — they're latency-bound on the device,
  not consuming a CPU scheduling slot, so they should never be pulled into
  stage-2 work.
- Backlog signal reuses the **existing** `poly_stage_stats_t` counters
  (`hits - stage2_done`, already computed for the roll-up line) via a new thin
  accessor `poly_stats_backlog()`. No new source of truth.

## 2. Single-thread scaling (`-t 1`)

The scheme above does **not** degrade safely to single-threaded operation as
originally specified. Two distinct issues:

1. **Thread-count overshoot.** `num_stage2_dedicated` defaults to 1
   independent of the producer count, so `-t 1` would actually spawn 2
   threads (1 producer + 1 dedicated consumer) — violating "single-threaded"
   before borrowing logic even runs.
   - Fix: `num_stage2_dedicated = 0` whenever `obj->num_threads <= 1`,
     overriding the `stage2_threads=` parse in that case. `stage2_workers`
     sizing (`num_stage2_dedicated + obj->num_threads`) already collapses
     correctly to a single bundle with no further change.

2. **Potential deadlock at N=1.** Borrowing only drains the stage-2 queue at
   chunk *boundaries* (between `v->specialq(...)` calls). `handle_collision`
   currently submits each hit with `threadpool_add_task(pool, &tc, 1)` —
   **blocking**. If one chunk produces enough hits to fill the queue before
   the call returns, and nothing else drains it (dedicated=0, and the lone
   producer is busy inside `specialq`, not at a checkpoint), that blocking
   add waits forever.
   - Fix: make the submission itself deadlock-proof, independent of thread
     count. In `handle_collision`, switch to non-blocking submission with a
     synchronous inline fallback:
     ```c
     if (threadpool_add_task(d->stage2_threadpool, &task_control, 0) == -2) {
         /* queue (or free-task pool) full and nobody's draining fast enough —
            run it in place, on this thread, right now */
         stage1_hit_run(hit_data,
                        d->num_stage2_dedicated + threadid);
     }
     ```
     This is a general safety valve, not just a single-thread patch — at any
     thread count, if backlog spikes faster than the periodic
     borrow-checkpoints keep up, this catches it instead of relying on chunk
     size being tuned exactly right.

Net effect at N=1: no extra thread spawned; every hit either enqueues
(harmlessly — the same thread pops it moments later at its own checkpoint) or,
once the small queue fills, runs synchronously inline immediately. This
degenerates to the original sequential stage-1/stage-2 behavior through the
*same* code paths, not a special case — deadline enforcement at chunk
boundaries still applies.

## 3. Threadpool primitive (`thread.c` / `thread.h`)

Investigated `thread.c`: no non-blocking dequeue exists. `threadpool_add_task`
can be non-blocking (enqueue side only). The only dequeue path,
`threadpool_task_get_task`, is `static` and always blocks on `new_tasks_cond`.

Required addition:
- Extract the existing "run task, return it to free-queue, broadcast" block
  out of `worker_thr_routine` into a shared static helper
  `threadpool_execute_and_release()` — pure refactor, no behavior change.
- Add a new public function:
  ```c
  int threadpool_try_run_task(struct threadpool *pool, int thread_num);
  /* returns 0 = ran a task, 1 = queue empty (no-op), -1 = error */
  ```
  Non-blocking: locks `pool->mutex`, checks `threadpool_queue_is_empty`,
  dequeues if non-empty, unlocks, then executes via the shared helper on the
  *calling* thread (no new pthread spawned).
- Same lock (`pool->mutex`) and same execute/release path the normal worker
  loop already uses — no new lock ordering introduced.

## 4. Bundle-index partitioning (avoiding a collision hazard)

`stage1_hit_run` resolves per-worker private state via
`stage2_workers[thread_num]`. If a borrowing producer used its own `threadid`
directly, it could collide with a dedicated stage-2 consumer thread that
happens to share the same numeric id in its own pool.

Fix: partition the index space.
```
stage2_workers[]  size = num_stage2_dedicated + num_threads (producers)
  [0 .. num_stage2_dedicated)                    -> dedicated pool workers
  [num_stage2_dedicated .. +num_threads)          -> producer i borrows index
                                                      num_stage2_dedicated + i
```
Each producer always borrows into the same private slot, so no contention
even though the *task* came from the shared queue.

## 5. Code changes

### `thread.c` / `thread.h`
- New `threadpool_execute_and_release()` (extracted, static).
- New public `threadpool_try_run_task(pool, thread_num)`.

### `stage1.c` — `search_coeff_core`
- Wrap the single `v->specialq(...)` call in a chunked loop over
  `[special_q_min2, special_q_max2)`, chunk size `window / QCHUNK_DIVISOR`
  capped at `QCHUNK_MAX` (both new tunables, need benchmarking).
- `stage1_engine_cell_fits` check moves inside the loop (per-chunk clamp
  instead of once for the whole window).
- After each chunk: enforce `task->coeff_deadline` (fixes the dead-code bug);
  then, if `v->cpu_bound` and `poly_stats_backlog(d->stats) >
  STAGE2_HIGH_WATERMARK`, call
  `threadpool_try_run_task(d->stage2_threadpool, d->num_stage2_dedicated + threadid)`.

### `search_coeffs` (same file)
- Parse `coeff_deadline=` from `obj->nfs_args` instead of the hardcoded
  `deadline_per_coeff = 8640000`.

### `stage1_engine.h` (vtable)
- Add `cpu_bound` flag so GPU engines opt out of borrowing.

### `poly_stats.{c,h}`
- Add `poly_stats_backlog()` accessor wrapping the existing mutex-guarded
  `hits - stage2_done` read. No new state.

### `poly_skew.c`
- `stage1_sieve_data_t` gains `num_stage2_dedicated` (copied through next to
  `sieve_data.stats = data->stats;`).
- `num_stage2_dedicated` computation: 0 whenever `obj->num_threads <= 1`
  (overrides `stage2_threads=` in that case), otherwise the existing
  `stage2_threads=` parse.
- `stage2_workers` array sized `num_stage2_dedicated + obj->num_threads`
  instead of just the dedicated count; `build_stage2_workers()` itself is
  unchanged (it already just builds N identical independent bundles). At
  `num_stage2_dedicated = 0` this collapses to exactly one bundle.
- Dedicated `d->stage2_threadpool` still sized `num_stage2_dedicated` only —
  only the bundle *array* grows to cover borrowers. At `num_stage2_dedicated
  = 0`, no dedicated pool thread is spawned at all.
- Teardown loop bound changes from `num_stage2_dedicated` to
  `num_stage2_dedicated + obj->num_threads`.

### `stage1.c` — `handle_collision`
- Stage-2 submission changes from blocking (`threadpool_add_task(pool, &tc,
  1)`) to non-blocking with a synchronous inline fallback: on a `-2` return
  (queue/free-task pool full), run the hit immediately on the calling thread
  via `stage1_hit_run(hit_data, d->num_stage2_dedicated + threadid)` instead
  of enqueuing. Prevents deadlock at `num_stage2_dedicated = 0` and acts as a
  general backlog safety valve at any thread count.

## 6. What doesn't change

- The `file_lock`/`save_poly` shared-write serialization from
  `parallel_stage2.md` §2c is untouched — borrowed tasks run the identical
  `stage1_hit_run` → callback chain, just dispatched from a different call
  site (a producer's chunk-boundary checkpoint instead of a dedicated
  consumer's blocking `get_task`).
- `build_stage2_workers()` logic, `stage2_worker_t` layout, and the
  `sizeopt_callback`/`rootopt_callback` bodies are all unchanged.

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

1. **Confirm the actual msieve timing call** to replace the placeholder
   `read_clock()` in the chunk loop — **已落地**：用的是 `get_wall_time()`
   （`CLOCK_MONOTONIC` 墙钟，秒为单位的 double，`factor/shared/common/util.cpp:98-104`，
   声明在 `factor/shared/include/util.h:135`）。`read_clock()` 现在只剩
   `mpqs.h` 的 TIME1/TIME2 宏在用。

2. **Pick `QCHUNK_DIVISOR` / `QCHUNK_MAX`** — **不适用（分块不该
   启用）**：硬编码成 `#define STAGE1_QCHUNK_DIVISOR 20` 与
   `#define STAGE1_QCHUNK_MAX 2000000`（`stage1.cpp:629-630`），没有命令行开关。
   这两个常量只被 `if (0)` 关着的分块实现引用（`stage1.cpp:739-741`），
   不影响运行；分块本身在 CPU 引擎上不该启用，三条依据：
   - **分块唯一买到的东西，整窗调用已经做到了。** CPU 引擎的 specialq
     实现（`stage1_sieve_cpu.cpp:1349-1365`）在内层每 16384 个 q-root
     查一次墙钟并对照 `task->coeff_deadline`，超时 `goto finished`。
     分块不替代这个检查，只是在块边界再加一次更粗的检查。
   - **实测（2026-10-20，100 位半素数，`a_d=1000`，`p[1000,2400]`，
     `q[1000,1e8]`，`cpu_gerbicz` 引擎）：`coeff_deadline=10` 时，
     不分块 10.28 秒停、分块 10.27 秒停** —— 及时性上没有区别。
   - **分块是纯开销。** `STAGE1_QCHUNK_DIVISOR = 20` 意味着把 1 次
     specialq 调用变成 20 次，而每次都要重建全部 p 的等差数列
     （`stage1_sieve_cpu.cpp:1235-1242`：p_packed_reset + sieve_fb_reset
     + 遍历 sieve_fb_next）—— 这部分与 q 窗口大小无关。
     同一输入不限 deadline 的总耗时（两轮）：不分块 95.94 / 95.77 秒、
     分块 97.70 / 98.14 秒，命中数四轮都是 51388，分块慢 1.9%。

   结论：这两个常量不需要"定值"，因为分块整体不启用。它们保持现在的
   硬编码即可，改动它们不会影响任何运行路径。

3. **Pick `STAGE2_HIGH_WATERMARK`** — **功能不存在**：树里没有水位线，
   也没有 `poly_stats_backlog()`；backlog 只是统计字段（`hits` /
   `stage2_done`，`factor/nfs/gnfs/poly/include/poly_stats.h:35-38`），
   深度只在打印行里临时算一下（`poly_stats.cpp:60`）。整套"借用"机制
   （`threadpool_try_run_task`、backlog 阈值、专用池下限）在代码里零残留，
   不是待定，是从未实现，且已被 v2 方案的设计取代。

4. **Verify `stage1_engine_cell_fits` is safe to call per-chunk** —
   **已落地**：代码里没有每块调用它 —— 每个 `a_d` 在进循环前调一次
   （`stage1.cpp:805` 是生效分支，`:746` 是关着的分块分支）。它的钳制只把
   `*special_q_max` 压到引擎的绝对上限（`stage1_engine.cpp:177-186`），
   与块大小无关，所以放在循环外是对的。

5. **Add `cpu_bound` to all four engine vtables** — **选型变了**：vtable
   里没有 `cpu_bound` 字段（`stage1_engine.h:42-63`）。CPU/GPU 分流用既有的
   `envelope.is_gpu`（`stage1_engine.h:34`，表项在 `stage1_engine.cpp:50,61,72,83`，
   使用点 `stage1.cpp:208`）。文档二与文档三都明确写了"不再需要 `cpu_bound`"。

6. **`best_saved_combined_e` semantics** — **已落地**：它是统计量，不是门槛。
   真正的门槛是 `min_e_bernstein` 与 `min_e`（`stage2/optimize.cpp:1250,1256`）；
   `best_saved_combined_e` 只在过了门槛之后更新，而且是每个 bundle 私有的
   副本（副本来源 `stage2/stage2.cpp:254-255`、`poly_skew.cpp:157`）。
   共享的"最好成绩"另存在 stats 的 `best_e`，持锁更新
   （`poly_stats.cpp:192-199`）。

7. **Write/port the `s2_pattern_test.c`-style standalone concurrency test** —
   **功能不存在**：那个文件在 `e341861` 被删掉了，树里没有替代品，
   Makefile 与 `test/` 都不引用它。

8. **Decide dedicated-pool floor for multi-threaded runs** —
   **选型变了**：`num_stage2_dedicated` 这个概念不存在。stage-2 池的大小是
   `MAX(1, num_stage2_workers)`，后者来自 `stage2_threads=`，再被抬到
   `MAX(num_s2, obj->num_threads)`（`stage1.cpp:590-591`、
   `poly_skew.cpp:487-492`）。CPU 路径根本不用这个池（stage-2 内联执行，
   `stage1.cpp:230-241`），这个池只服务 GPU；`-t 1` 时池是 1 个线程而不是 0。

9. **Verify the synchronous fallback in `handle_collision` doesn't re-enter
   any lock the caller already holds** — **已落地**：CPU 路径是直接内联调用
   `d->poly->callback(...)`（`stage1.cpp:233-240`），不是"非阻塞提交失败后
   回退"。它会去拿 `file_lock`（`poly_skew.cpp:59-60,73-74`）与 stats 锁
   （`poly_stats.cpp:190,209`）。两个调用方引擎（CPU、hashtable）整文件
   没有任何 mutex 调用，所以调用栈上不存在冲突锁。

10. **Test `-t 1` end-to-end** — **功能不存在**：`test/` 下唯一涉及线程池的
    是通用生命周期测试（init/add/drain/free 计数，
    `test/layer3/test_qs_review.cpp:64-93`），不覆盖 stage-1/stage-2 配平，
    也没有单线程端到端用例。
