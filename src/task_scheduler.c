#include "task_scheduler.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TASK_QUEUE_CAPACITY 256
#define TASK_MAX_WORKERS 32

struct TaskGroup
{
  char label[32];
  pthread_mutex_t lock;
  pthread_cond_t drained;
  int pending;
};

typedef struct
{
  TaskFn fn;
  void *user_data;
  TaskGroup *group;
  TaskPriority priority;
} Task;

static struct
{
  pthread_mutex_t lock;
  pthread_cond_t work_available;
  pthread_t workers[TASK_MAX_WORKERS];
  int worker_count;
  Task queue[TASK_QUEUE_CAPACITY];
  int head;
  int count;
  bool running;
  bool shutting_down;
} g_pool;

static void task_group_mark_done(TaskGroup *group)
{
  if (!group)
    return;
  pthread_mutex_lock(&group->lock);
  if (group->pending > 0)
    group->pending--;
  if (group->pending == 0)
    pthread_cond_broadcast(&group->drained);
  pthread_mutex_unlock(&group->lock);
}

// Take one queued task and run it on the calling thread. False when the queue was empty, which is
// also what a caller that lost the race to another thread sees.
//
// This is shared by the workers and by task_group_wait, which is what makes a task able to wait for
// tasks it submitted itself.
static bool task_pool_try_run_one(void)
{
  if (!g_pool.running)
    return false;

  pthread_mutex_lock(&g_pool.lock);
  if (g_pool.count == 0)
  {
    pthread_mutex_unlock(&g_pool.lock);
    return false;
  }

  // Prefer HIGH-priority tasks (Devlog #27 isolation): scan the ring for the first HIGH entry.
  int pick = -1;
  for (int i = 0; i < g_pool.count; i++)
  {
    const int idx = (g_pool.head + i) % TASK_QUEUE_CAPACITY;
    if (g_pool.queue[idx].priority == TASK_PRIORITY_HIGH)
    {
      pick = i;
      break;
    }
  }
  if (pick < 0)
    pick = 0;

  const int abs_idx = (g_pool.head + pick) % TASK_QUEUE_CAPACITY;
  Task task = g_pool.queue[abs_idx];
  // Compact the ring by shifting the gap toward the head.
  for (int i = pick; i > 0; i--)
  {
    const int dst = (g_pool.head + i) % TASK_QUEUE_CAPACITY;
    const int src = (g_pool.head + i - 1) % TASK_QUEUE_CAPACITY;
    g_pool.queue[dst] = g_pool.queue[src];
  }
  g_pool.head = (g_pool.head + 1) % TASK_QUEUE_CAPACITY;
  g_pool.count--;
  pthread_mutex_unlock(&g_pool.lock);

  // Run outside the pool lock so tasks can be long (a 128^3 world takes seconds) and so
  // one task may submit another without deadlocking.
  if (task.fn)
    task.fn(task.user_data);
  task_group_mark_done(task.group);
  return true;
}

static void *task_worker_main(void *arg)
{
  (void)arg;
  for (;;)
  {
    pthread_mutex_lock(&g_pool.lock);
    while (g_pool.count == 0 && !g_pool.shutting_down)
      pthread_cond_wait(&g_pool.work_available, &g_pool.lock);
    const bool stop = (g_pool.count == 0 && g_pool.shutting_down);
    pthread_mutex_unlock(&g_pool.lock);

    if (stop)
      return NULL;

    // May find nothing: another thread can take the task between the wakeup and here. The outer
    // loop just waits again.
    task_pool_try_run_one();
  }
}

static int default_worker_count(void)
{
  long cpus = sysconf(_SC_NPROCESSORS_ONLN);
  if (cpus < 2)
    return 1;
  // Leave one core for the render/input thread.
  long workers = cpus - 1;
  if (workers > TASK_MAX_WORKERS)
    workers = TASK_MAX_WORKERS;
  return (int)workers;
}

bool task_scheduler_init(int worker_count)
{
  if (g_pool.running)
    return true;

  memset(&g_pool, 0, sizeof(g_pool));

  if (worker_count <= 0)
    worker_count = default_worker_count();
  if (worker_count > TASK_MAX_WORKERS)
    worker_count = TASK_MAX_WORKERS;

  if (pthread_mutex_init(&g_pool.lock, NULL) != 0)
    return false;
  if (pthread_cond_init(&g_pool.work_available, NULL) != 0)
  {
    pthread_mutex_destroy(&g_pool.lock);
    return false;
  }

  g_pool.running = true;
  g_pool.shutting_down = false;

  for (int i = 0; i < worker_count; i++)
  {
    if (pthread_create(&g_pool.workers[i], NULL, task_worker_main, NULL) != 0)
    {
      // Keep whatever started; a smaller pool is still useful, and zero workers falls back
      // to inline execution rather than failing the caller.
      fprintf(stderr, "[tasks] only %d of %d workers started\n", i, worker_count);
      g_pool.worker_count = i;
      if (i == 0)
      {
        g_pool.running = false;
        pthread_cond_destroy(&g_pool.work_available);
        pthread_mutex_destroy(&g_pool.lock);
        return false;
      }
      return true;
    }
    g_pool.worker_count = i + 1;
  }

  printf("[tasks] worker pool started with %d thread(s)\n", g_pool.worker_count);
  return true;
}

void task_scheduler_shutdown(void)
{
  if (!g_pool.running)
    return;

  pthread_mutex_lock(&g_pool.lock);
  g_pool.shutting_down = true;
  pthread_cond_broadcast(&g_pool.work_available);
  pthread_mutex_unlock(&g_pool.lock);

  for (int i = 0; i < g_pool.worker_count; i++)
    pthread_join(g_pool.workers[i], NULL);

  pthread_cond_destroy(&g_pool.work_available);
  pthread_mutex_destroy(&g_pool.lock);

  g_pool.running = false;
  g_pool.worker_count = 0;
  g_pool.count = 0;
  g_pool.head = 0;
}

bool task_scheduler_is_running(void)
{
  return g_pool.running && g_pool.worker_count > 0;
}

int task_scheduler_worker_count(void)
{
  return g_pool.running ? g_pool.worker_count : 0;
}

TaskGroup *task_group_create(const char *label)
{
  TaskGroup *group = (TaskGroup *)calloc(1, sizeof(TaskGroup));
  if (!group)
    return NULL;

  if (label)
  {
    strncpy(group->label, label, sizeof(group->label) - 1);
    group->label[sizeof(group->label) - 1] = '\0';
  }

  if (pthread_mutex_init(&group->lock, NULL) != 0)
  {
    free(group);
    return NULL;
  }
  if (pthread_cond_init(&group->drained, NULL) != 0)
  {
    pthread_mutex_destroy(&group->lock);
    free(group);
    return NULL;
  }
  return group;
}

bool task_group_submit(TaskGroup *group, TaskFn fn, void *user_data)
{
  return task_group_submit_priority(group, fn, user_data, TASK_PRIORITY_HIGH);
}

bool task_group_submit_priority(TaskGroup *group, TaskFn fn, void *user_data, TaskPriority priority)
{
  if (!group || !fn)
    return false;

  pthread_mutex_lock(&group->lock);
  group->pending++;
  pthread_mutex_unlock(&group->lock);

  if (task_scheduler_is_running())
  {
    pthread_mutex_lock(&g_pool.lock);
    if (g_pool.count < TASK_QUEUE_CAPACITY && !g_pool.shutting_down)
    {
      int slot = (g_pool.head + g_pool.count) % TASK_QUEUE_CAPACITY;
      g_pool.queue[slot].fn = fn;
      g_pool.queue[slot].user_data = user_data;
      g_pool.queue[slot].group = group;
      g_pool.queue[slot].priority = priority;
      g_pool.count++;
      pthread_cond_signal(&g_pool.work_available);
      pthread_mutex_unlock(&g_pool.lock);
      return true;
    }
    // Queue full, or shutting down: fall through and run inline rather than block or drop.
    pthread_mutex_unlock(&g_pool.lock);
  }

  fn(user_data);
  task_group_mark_done(group);
  return true;
}

int task_group_pending(TaskGroup *group)
{
  if (!group)
    return 0;
  pthread_mutex_lock(&group->lock);
  int pending = group->pending;
  pthread_mutex_unlock(&group->lock);
  return pending;
}

bool task_group_is_complete(TaskGroup *group)
{
  return task_group_pending(group) == 0;
}

void task_group_wait(TaskGroup *group)
{
  if (!group)
    return;

  for (;;)
  {
    pthread_mutex_lock(&group->lock);
    const bool done = (group->pending == 0);
    pthread_mutex_unlock(&group->lock);
    if (done)
      return;

    // Run queued work rather than only sleeping on it. A task that waits for tasks it submitted is
    // otherwise a deadlock whenever the pool has no spare worker: world generation fans 53 worlds
    // out from inside a job that is itself a task, and on the one-worker pool a single-core machine
    // gets, that one worker would block forever holding the only thread able to drain the queue.
    // Helping means a waiter can never be the reason the queue stalls.
    if (task_pool_try_run_one())
      continue;

    // Nothing left to take, so what this group is still waiting on is already running on another
    // thread. Sleep until it reports in — the broadcast in task_group_mark_done needs this same
    // lock, so a completion between the check above and here cannot be missed.
    pthread_mutex_lock(&group->lock);
    if (group->pending > 0)
      pthread_cond_wait(&group->drained, &group->lock);
    pthread_mutex_unlock(&group->lock);
  }
}

void task_group_destroy(TaskGroup *group)
{
  if (!group)
    return;
  task_group_wait(group);
  pthread_cond_destroy(&group->drained);
  pthread_mutex_destroy(&group->lock);
  free(group);
}
