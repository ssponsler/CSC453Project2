#include "rr.h"
#include "lwp.h"

thread head = NULL;
thread next = NULL;
/*
   Using supplied sched_one and sched_two pointers as linked list pointers
   sched_one = prev
   sched_two = next
*/

void rr_admit(thread new_thread)
{
    thread temp = head;
    if (head == NULL)
    {
        head = new_thread;
        head->sched_one = NULL;
        head->sched_two = NULL;
    }
    //traverse to end of the linked list and add
    else
    {
        while (temp->sched_one != NULL)
        {
            temp = temp->sched_one;
        }
        new_thread->sched_one = NULL;
        new_thread->sched_two = temp;
        temp->sched_one = new_thread;
    }
}

void rr_remove(thread victim)
{
    // traverse through list
    thread temp = head;

    if (temp == NULL || victim == NULL)
        return;

    if (temp == victim)
    {
        head = victim->sched_one;
    }

    if (victim->sched_two != NULL)
        victim->sched_two->sched_one = victim->sched_one;
    if (victim->sched_one != NULL)
        victim->sched_one->sched_two = victim->sched_two;
}

/*
   Return the next thread in the scheduler following RR priority
*/
thread rr_next()
{
    if (next == NULL)
    {
        next = head;
    }
    else
    {
        next = next->sched_one;
        // once we reach the end, return to head
        if (next == NULL)
            next = head;
    }
    return next;
}

int rr_qlen()
{
    int len = 0;
    thread temp = head;
    while (temp)
    {
        len++;
        temp = temp->sched_one;
    }
    return len;
}