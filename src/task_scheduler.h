#ifndef VERSE_TASK_SCHEDULER_H
#define VERSE_TASK_SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>

// Worker pool for moving whole subsystems off the main thread.
//
// Tasks here are coarse — generate a world, step one world's physics — not a parallel-for.
// Nothing in this file locks a World for you: each task must own its data exclusively for
// its lifetime. Two tasks that touch the same World are a data race.
//
// If the pool is not running (init was never called, or it failed) submitted tasks execute
// inline on the calling thread before submit returns. Callers therefore behave correctly
// either way and never need to branch on whether threading is available, which is what
// keeps the headless tools and tests working without starting threads.
//
// Priority (Devlog #27): HIGH tasks (physics, lighting) are always preferred over BACKGROUND
// (remesh, world gen). When the main thread waits on a group it helps drain HIGH work first so
// gen cannot stall the frame.

typedef void (*TaskFn)(void *user_data);

typedef enum
{
  TASK_PRIORITY_BACKGROUND = 0,
  TASK_PRIORITY_HIGH = 1
} TaskPriority;

// worker_count <= 0 derives a default from the CPU count. Safe to call twice; the second
// call is a no-op that returns true.
bool task_scheduler_init(int worker_count);

// Waits for queued work to drain, then joins every worker.
void task_scheduler_shutdown(void);

bool task_scheduler_is_running(void);
int task_scheduler_worker_count(void);

// Groups track a set of submitted tasks so the caller can wait for, or poll, all of them.
// A group is owned by one thread — the one that created it and submits to it.
typedef struct TaskGroup TaskGroup;

TaskGroup *task_group_create(const char *label);

// Returns false only when the group is NULL or the task function is NULL. When no worker
// is available the task has already run to completion by the time this returns.
bool task_group_submit(TaskGroup *group, TaskFn fn, void *user_data);

// Same as task_group_submit but with an explicit priority. Defaults to HIGH for the plain submit
// so existing physics callers stay latency-sensitive.
bool task_group_submit_priority(TaskGroup *group, TaskFn fn, void *user_data, TaskPriority priority);

int task_group_pending(TaskGroup *group);
bool task_group_is_complete(TaskGroup *group);

// Blocks until every task submitted to this group has finished. The calling thread helps run
// queued HIGH-priority work while it waits (Devlog #27 main-thread participation).
void task_group_wait(TaskGroup *group);

// Waits for outstanding tasks, then frees the group.
void task_group_destroy(TaskGroup *group);

#endif // VERSE_TASK_SCHEDULER_H
