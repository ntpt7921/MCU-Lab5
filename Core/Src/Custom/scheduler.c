/*
 * scheduler.c
 *
 *  Created on: Nov 13, 2022
 *      Author: ntpt
 */

#include "Custom/scheduler.h"
#include "Custom/priority_queue.h"
#include "Custom/scheduler_task.h"

#ifdef CUSTOM_SCHEDULER_USE_WATCHDOG
#include "iwdg.h"
#include "stm32f1xx_hal_iwdg.h"
#endif

#include "stdint.h"
#include "stm32f1xx_hal_pwr.h"
#include "stm32f1xx_hal_tim.h"
#include "tim.h"

/*
 * HELPER FUNCTIONS
 * NOTE:
 * A race condition can happen if the timestamp values wihtin the system is updated inside
 * an ISR (which is the case).
 *
 * A way to defer timestamp updated is employed so that the timestamp won't get updated
 * during a critical section. This will not affect the timing accuracy of the scheduler
 * more than the effect the preemptive policy already have.
 */
// increment timestamp (with check and fix overflow)
static inline void increment_timestamp(uint32_t volatile *ts, uint32_t amount);
// fix timestamp for all task (runAtTick) when overflow occur
static inline void fix_all_timestamp_overflow();

// global tick counter
static uint32_t volatile system_tick_count = 0;
// static array contaning the priorirty queue
static SchedTask_t bin_heap[CUSTOM_SCHEDULER_BIHEAP_SIZE];
// counter for the number of task within heap
static size_t task_count = 0;
// if the scheduler is running or not
static uint8_t scheduler_is_running = 0;
// if a task is currently running
static uint8_t task_is_running = 0;

// if we need to defer interrupt for updating system_tick_count
static uint8_t defer_tick_update = 0;
// if number of tick deferred
static uint32_t defer_tick_update_count = 0;

// compare function for task
// assume that e1 and e2 points to SchedTask_t
__weak uint8_t Custom_SchedTask_Compare_Smaller(void *task1, void *task2)
{
    SchedTask_t *elem1 = (SchedTask_t*) task1;
    SchedTask_t *elem2 = (SchedTask_t*) task2;

    // compare by priority first
    if (elem1->priority < elem2->priority)
    {
        return 1;
    }
    else if (elem1->priority > elem2->priority)
    {
        return 0;
    }
    else // elem1->priority == elem2->priority
    {
        // if priority equal, compare by runAtTick time
        // a task that is older (smaller runAtTick) should be ordered higher
        if (elem1->runAtTick < elem2->runAtTick)
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }

    return 0; // should not reach this
}

void Custom_Scheduler_Init(void)
{
    if (scheduler_is_running)
    {
        task_count = 0; // clear all old task
    }
    else
    {
        // keep task_count value, since there may be task added before scheduler is started
    }
    system_tick_count = 0;
    scheduler_is_running = 1;
    task_is_running = 0;

    defer_tick_update = 0;
    defer_tick_update_count = 0;

    Custom_PQueue_Create(bin_heap, CUSTOM_SCHEDULER_BIHEAP_SIZE, sizeof(SchedTask_t),
            task_count, Custom_SchedTask_Compare_Smaller);

    // init timer and watchdog
#ifdef CUSTOM_SCHEDULER_USE_WATCHDOG
    MX_IWDG_Init(); // defined by CubeMx in iwdg.c
#endif

    MX_TIM3_Init(); // defined by CubeMx in tim.c
    HAL_TIM_Base_Start_IT(&htim3);
}

void Custom_Scheduler_Update(void)
{
#ifdef CUSTOM_SCHEDULER_USE_WATCHDOG
    if (!task_is_running)
    {
        // refresh the watchdog if no task is running
        // this is so that when there is no task available to run, the watchdog won't trip and reset
        HAL_IWDG_Refresh(&hiwdg);
    }
#endif
    if (!defer_tick_update)
    {
        increment_timestamp(&system_tick_count, 1u);
    }
    else
    {
        defer_tick_update_count++;
    }
}

void Custom_Scheduler_Add(SchedTask_Func_t pTask, void *pArg,
        uint8_t priority, uint32_t period, uint32_t delay, uint8_t ID)
{
    if (task_count == CUSTOM_SCHEDULER_BIHEAP_SIZE)
    {
        Custom_Err_SetStatus(ERR_SCHEDULER_FULLADD);
        return;
    }

    if (scheduler_is_running)
    {
        // assume that the the binary heap is already created
        SchedTask_t new_task =
        {
            .pTask = pTask,
            .pTaskArg = pArg,
            .priority = priority,
            .periodTick = period,
            .runAtTick = system_tick_count,
            .taskID = ID,
        };

        Custom_PQueue_Insert(bin_heap, CUSTOM_SCHEDULER_BIHEAP_SIZE, sizeof(SchedTask_t), task_count,
                &new_task, Custom_SchedTask_Compare_Smaller);

        increment_timestamp(&new_task.runAtTick, delay);
    }
    else
    {
        // assume that we are adding task before the heap is created
        // so task can be added sequentially
        SchedTask_t *current = &bin_heap[task_count];
        current->pTask = pTask;
        current->pTaskArg = pArg;
        current->priority = priority;
        current->periodTick = period;
        current->runAtTick = delay;
        current->taskID = ID;
    }
    task_count++;
}

void Custom_Scheduler_Delete(uint8_t ID)
{
    if (task_count == 0)
    {
        Custom_Err_SetStatus(ERR_SCHEDULER_EMPTYDELETE);
        return;
    }

    for (size_t i = 0; i < task_count; i++)
    {
        if (bin_heap[i].taskID == ID)
        {
            // found the task, delete it from heap
            Custom_PQueue_Delete(bin_heap, CUSTOM_SCHEDULER_BIHEAP_SIZE, sizeof(SchedTask_t),
                    task_count, i, Custom_SchedTask_Compare_Smaller);
            task_count--;
            return;
        }
    }
}

void Custom_Scheduler_Dispatch()
{
    if (task_count == 0)
    {
        // go back to sleep
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
        return;
    }

    // search the top of binary heap
    // if found task that is overdue, run it and update task record
    defer_tick_update = 1; // start of critical section
    SchedTask_t *top = &bin_heap[0];
    while (task_count > 0 && top->runAtTick < system_tick_count)
    {
#ifdef CUSTOM_SCHEDULER_USE_WATCHDOG
        HAL_IWDG_Refresh(&hiwdg);
#endif
        // call the function (by the pointer stored), passing any argument
        task_is_running = 1;
        top->pTask(top->pTaskArg);
        task_is_running = 0;
#ifdef CUSTOM_SCHEDULER_USE_WATCHDOG
        HAL_IWDG_Refresh(&hiwdg);
#endif

        if (top->periodTick == 0) // one-time task
        {
            // delete the task
            Custom_PQueue_Pop(bin_heap, CUSTOM_SCHEDULER_BIHEAP_SIZE, sizeof(SchedTask_t),
                    task_count, Custom_SchedTask_Compare_Smaller);
            task_count--;
        }
        else
        {
            // if not reload the task and push it down the heap
            increment_timestamp(&top->runAtTick, top->periodTick);
            Custom_PQueue_PushDown(bin_heap, sizeof(SchedTask_t),
                    task_count, Custom_SchedTask_Compare_Smaller);
        }
    }
    defer_tick_update = 0;
    if (defer_tick_update_count > 0)
    {
        increment_timestamp(&system_tick_count, defer_tick_update_count);
        defer_tick_update_count = 0;
    }

    // go back to sleep
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
}

// increment timestamp (with check and fix overflow)
static inline void increment_timestamp(uint32_t volatile *ts, uint32_t amount)
{
    uint32_t limit = UINT32_MAX - amount;
    if (*ts > limit)
    {
        fix_all_timestamp_overflow();
    }
    *ts += amount;
}

// fix timestamp for all task (runAtTick) and system_tick_count when overflow occur
static void fix_all_timestamp_overflow()
{
    // find the smallest timestamp
    uint32_t min_ts = system_tick_count;
    for (size_t i = 0; i < task_count; i++)
    {
        if (bin_heap[i].runAtTick < min_ts)
        {
            min_ts = bin_heap[i].runAtTick;
        }
    }

    // shift all timestamp relatively such that the smallest time stamp is now 0
    for (size_t i = 0; i < task_count; i++)
    {
        bin_heap[i].runAtTick -= min_ts;
    }

    // shift the system_tick_count by that amount too
    system_tick_count -= min_ts;
}

