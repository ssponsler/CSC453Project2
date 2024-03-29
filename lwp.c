#include "lwp.h"
#include "rr.h"
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>

#define STACK_SIZE (8 * 1024 * 1024) // 8MB default

static tid_t thread_count = 0;
static thread active_lwp = NULL;
static thread threads;
static thread term_queue = NULL;
static thread waiting_queue = NULL;

struct scheduler rr_finish =
    {NULL, NULL, rr_admit, rr_remove, rr_next, rr_qlen};

static scheduler current_scheduler = &rr_finish;

/*
   Support functions for global threads list
*/
void add_to_threads(thread new_thread)
{
    thread temp = threads;
    if (threads == NULL)
    {
        threads = new_thread;
        threads->lib_two = NULL;
    }
    else
    {
        while (temp->lib_two != NULL)
        {
            temp = temp->lib_two;
        }
        new_thread->lib_two = NULL;
        temp->lib_two = new_thread;
    }
}

void pop_from_threads(thread victim)
{
    thread temp = threads;
    thread prev;

    if (temp == NULL || victim == NULL)
        return;

    if (temp == victim)
    {
        threads = victim->lib_two;
        return;
    }

    while (temp != NULL && temp != victim)
    {
        prev = temp;
        temp = temp->lib_two;
    }

    if (temp == NULL)
        return;
    else
    {
        prev->lib_two = temp->lib_two;
    }
}
/*
   Support functions for global termination queue list
*/
void admit_term_queue(thread new_thread)
{
    thread temp = term_queue;
    if (term_queue == NULL)
    {
        term_queue = new_thread;
        term_queue->lib_one = NULL;
    }
    else
    {
        while (temp->lib_one != NULL)
        {
            temp = temp->lib_one;
        }
        new_thread->lib_one = NULL;
        temp->lib_one = new_thread;
    }
}

void pop_term_queue(thread victim)
{
    thread temp = term_queue;
    thread prev;

    if (temp == NULL || victim == NULL)
        return;

    if (temp == victim)
    {
        term_queue = victim->lib_one;
        return;
    }

    while (temp != NULL && temp != victim)
    {
        prev = temp;
        temp = temp->lib_one;
    }

    if (temp == NULL)
        return;

    prev->lib_one = temp->lib_one;
}

void add_waiting_queue(thread new_thread)
{
    thread temp = waiting_queue;
    if (waiting_queue == NULL)
    {
        waiting_queue = new_thread;
        waiting_queue->lib_one = NULL;
    }
    else
    {
        while (temp->lib_one != NULL)
        {
            temp = temp->lib_one;
        }
        new_thread->lib_one = NULL;
        temp->lib_one = new_thread;
    }
}

void pop_waiting_queue(thread victim)
{
    thread temp = waiting_queue;
    thread prev;

    if (temp == NULL || victim == NULL)
        return;

    if (temp == victim)
    {
        waiting_queue = victim->lib_one;
        return;
    }

    while (temp != NULL && temp != victim)
    {
        prev = temp;
        temp = temp->lib_one;
    }

    if (temp == NULL)
        return;

    prev->lib_one = temp->lib_one;
}

/*
   Deallocate resources associated with a thread
*/
void clean_thread(thread term_thread)
{
    if (term_thread->stack != NULL)
    {
        if (munmap(term_thread->stack, term_thread->stacksize) == -1)
        {
            perror("munmap");
            exit(EXIT_FAILURE);
        }
    }

    if (term_thread)
    {
        free(term_thread);
    }
}
/*
   Support function for dealing with return statuses
*/
void lwp_wrap(lwpfun function, void *argument)
{
    int return_val;
    return_val = function(argument);
    lwp_exit(return_val);
}
/*
   Creating new LWP
*/
tid_t lwp_create(lwpfun function, void *argument)
{
    // malloc to size of struct
    thread new_thread = (thread)malloc(sizeof(struct threadinfo_st));
    if (new_thread == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    ssize_t stack_size = 0;
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (page_size == -1)
    {
        perror("sysconf");
        exit(EXIT_FAILURE);
    }
    struct rlimit r_lim_st;
    if (getrlimit(RLIMIT_STACK, &r_lim_st) == -1)
    {
        perror("getrlimit");
        exit(EXIT_FAILURE);
    }

    if (r_lim_st.rlim_cur == RLIM_INFINITY)
    {
        // use a default size if there's no limit or if the limit is infinity
        stack_size = STACK_SIZE;
    }
    else
    {
        // use the limit if it exists
        stack_size = r_lim_st.rlim_cur;
    }

    if (stack_size % page_size != 0)
    {
        // round up to the nearest multiple of the page size
        stack_size = ((stack_size / page_size) + 1) * page_size;
    }
    // map stack size
    new_thread->stack = (unsigned long *)mmap(NULL, (stack_size * sizeof(unsigned long)), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (new_thread == MAP_FAILED)
    {
        perror("mmap");
        free(new_thread);
        return NO_THREAD;
    }

    new_thread->stacksize = stack_size;
    // inc counter for new thread
    thread_count++;
    /*
      Stack math
    */
    new_thread->tid = thread_count;
    new_thread->state.rdi = (unsigned long)function;
    new_thread->state.rsi = (unsigned long)argument;
    new_thread->state.fxsave = FPU_INIT;

    unsigned long *stack_pointer;
    unsigned long *base_pointer;
    stack_pointer = (new_thread->stack + new_thread->stacksize - 1);
    stack_pointer = (unsigned long *)((uintptr_t)stack_pointer & ~0xF);
    base_pointer = stack_pointer;

    stack_pointer--;
    *stack_pointer = (unsigned long)lwp_exit;

    stack_pointer--;
    *stack_pointer = (unsigned long)lwp_wrap;

    stack_pointer--;
    *stack_pointer = (unsigned long)base_pointer;

    new_thread->state.rbp = (unsigned long)(stack_pointer);
    new_thread->state.rsp = (unsigned long)(stack_pointer);

    if (current_scheduler == NULL)
    {
        current_scheduler = &rr_finish;
    }

    new_thread->status = MKTERMSTAT(LWP_LIVE, 0);

    current_scheduler->admit(new_thread);
    add_to_threads(new_thread);

    return new_thread->tid;
}
/*
   Convert calling thread into LWP and yield
*/
void lwp_start(void)
{
    // ensure we have scheduled threads
    if (current_scheduler->qlen <= 0)
    {
        return;
    }
    // allocate new context
    thread new_thread = (thread)malloc(sizeof(struct threadinfo_st));
    if (!new_thread)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    new_thread->stack = NULL;
    thread_count++;
    new_thread->tid = thread_count;

    if (current_scheduler == NULL)
    {
        current_scheduler = &rr_finish;
    }
    // add created thread to scheduler and thread list
    current_scheduler->admit(new_thread);
    add_to_threads(new_thread);

    active_lwp = new_thread;

    lwp_yield();
}

/*
   Yield control to next LWP in scheduler
*/
void lwp_yield(void)
{
    thread next = NULL;
    if ((next = current_scheduler->next()) == NULL)
    {
        // save status before exiting
        unsigned int status = active_lwp->status;
        // free last thread
        clean_thread(active_lwp);
        // exit with last status
        exit(status);
    }
    // temporary thread to swap contexts
    thread current_lwp = active_lwp;
    active_lwp = next;
    swap_rfiles(&current_lwp->state, &active_lwp->state); 
}

/*
   Associate exit status and add to termination queue
*/
void lwp_exit(int exitval)
{
    // status is last 8 bits + term flag
    active_lwp->status = MKTERMSTAT(LWP_TERM, (exitval & 0xFF));
    // remove thread from scheduler and threads list
    current_scheduler->remove(active_lwp);
    pop_from_threads(active_lwp);
    // add to termination queue (for lwp_wait later)
    admit_term_queue(active_lwp);

    if (waiting_queue != NULL)
    {
        waiting_queue->exited = term_queue;
        // readd blocking thread to scheduler
        current_scheduler->admit(waiting_queue);
    }
    // yield to next thread in queue
    lwp_yield();
}

/*
   Wait for a thread to terminate given termination status
   and context switches to next LWP in scheduler
*/
tid_t lwp_wait(int *status)
{
    if (current_scheduler->qlen() <= 1)
    {
        return NO_THREAD;
    }

    // attempt to terminate oldest thread in the queue
    if (term_queue != NULL)
    {
        // remove oldest thread from term queue
        thread terminated = term_queue;
        pop_term_queue(terminated);
        if (status != NULL)
        {
            *status = terminated->status;
        }
        // return tid
        tid_t r_tid = terminated->tid;

        clean_thread(terminated);

        return r_tid;
    }

    // remove from scheduler and place in waiting queue
    current_scheduler->remove(active_lwp);
    add_waiting_queue(active_lwp);
    lwp_yield();
    thread terminated = active_lwp->exited;
    pop_term_queue(terminated);
    pop_waiting_queue(active_lwp);
    if (terminated != NULL)
    {
        if (status != NULL)
        {
            *status = terminated->status;
        }

        tid_t r_tid = terminated->tid;
        clean_thread(terminated);
        return r_tid;
    }
}

/*
   Return the tid_t of the currently active LWP
*/
tid_t lwp_gettid(void)
{
    if (!active_lwp)
    {
        return NO_THREAD;
    }
    return active_lwp->tid;
}

void lwp_set_scheduler(scheduler fun)
{
    thread saved;
    thread new_head;
    scheduler new_sched = fun;

    if (!new_sched)
    {
        if (current_scheduler == &rr_finish)
        {
            return;
        }
        new_sched = &rr_finish;
    }

    if (new_sched->init)
    {
        new_sched->init();
    }

    new_head = active_lwp;

    while ((active_lwp = current_scheduler->next()))
    {
        saved = active_lwp;
        current_scheduler->remove(saved);
        new_sched->admit(saved);
    }

    if (current_scheduler->shutdown)
    {
        current_scheduler->shutdown();
    }
    
    active_lwp = new_head;
    current_scheduler = new_sched;
}

scheduler lwp_get_scheduler(void)
{
    return current_scheduler;
}

/*
   Returns thread object given a tid_t
*/
thread tid2thread(tid_t tid)
{
    thread temp = threads;
    while (temp)
    {
        if (temp->tid == tid)
        {
            return temp;
        }
        temp = temp->lib_two;
    }
    return NULL;
}
