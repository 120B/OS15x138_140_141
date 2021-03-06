			+--------------------+
			|      CS 101OS      |
			| PROJECT 3: THREADS |
			|   DESIGN DOCUMENT  |
			+--------------------+
				   
---- GROUP ----

>> Fill in the names and email addresses of your group members.

qiaowei chen 740538396@qq.com
jingbin yang 657459219@qq.com
wenwen  qu   1481439920@qq.com
>> Specify how many late tokens you are using on this assignment: 

1, but I'm requesting an extension from Dean Green because I've 
been sick with my chronic illness for the past week. It's worth 
noting that by team members' parts of the assignments were done on 
time.

>> What is the Git repository and commit hash for your submission?

   Repository URL: https://github.com/ilyanep/pintos-awesome 
   commit ...

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

			     ALARM CLOCK
			     ===========

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

Inside struct thread, there are two new elements:

     struct list_elem sleep_elem;        /*!< List element for sleeping threads list. */
     int64_t sleep_end;                  /*!< For sleeping thread, time of sleep end. */

As explained in the comment, the first is the element for the sleeping threads list (as
required by that list), and the second is the time that the thread should stop sleeping.

I also created two new functions (thread_sleep and thread_wake) and changed thread_tick 
to take the current total number of ticks. The thread_wake function loops through all the 
threads on the sleeping threads list and wakes them if the global ticks is greater than the 
threads sleep_end variable. The thread_sleep function pushes the thread onto the sleeping
threads list and sets the thread's sleep_end variable.

---- ALGORITHMS ----

>> A2: Briefly describe what happens in a call to timer_sleep(),
>> including the effects of the timer interrupt handler.

When timer_sleep() is called, interrupts are disabled. Then, thread_sleep() is called
with the end time (which is current time + number of ticks to sleep). That function
sets the thread's sleep_end variable and pushes it onto the sleeping threads list.
Then, timer_sleep() calls thread_block(). After it returns (i.e. the thread is
unblocked) the interrupt level is set to what it was before.

>> A3: What steps are taken to minimize the amount of time spent in
>> the timer interrupt handler?

Looping over only the sleeping threads as opposed to all threads. Only doing some 
basic list/unblocking operations.

---- SYNCHRONIZATION ----

>> A4: How are race conditions avoided when multiple threads call
>> timer_sleep() simultaneously?

Interrupts are disabled before anything is done with the sleeping threads list (including
asserting that they are before the list is directly touched in thread_sleep). 

>> A5: How are race conditions avoided when a timer interrupt occurs
>> during a call to timer_sleep()?

Interrupts are disabled right before thread_sleep is called. Therefore, the only time an
interrupt can be called inside timer_sleep() is right after we load the current 
timer_ticks(). However, this is okay, because we'd like to know the timer_ticks from
when timer_sleep() is called, not the current time (otherwise we might sleep too long!)

---- RATIONALE ----

>> A6: Why did you choose this design?  In what ways is it superior to
>> another design you considered?

This seems the most straightforward design for this. The only simple way that I could think
of changing this is looping through all threads instead of the sleeping threads, but that
makes it take longer inside the timer interrupt handler. 

			 PRIORITY SCHEDULING
			 ===================

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

For the thread struct:
    
    int original_priority;              /*!< Priority before donation. */
    struct list locks;                  /*!< List of locks the thread holds. */
    struct lock *desired_lock;          /*!< The lock the thread is waiting on. */

original_priority is so the thread knows its priority before donation 
    so it can revert back to it after the donating threads finish.
locks is so the thread knows what threads have donated their priorities
    to it, which is used to keep track of multiple donations to a single
    thread.
desired_lock is so the thread keeps track of the resource it's blocked
    on. This forms a linked list with the holder member of semaphores
    to keep track of nested donations.
===
For the lock struct:
    
    struct list_elem lockelem;  /*!< List element for the holding thread's locks list. */
    int priority;               /*!< Current priority of the lock. */

lockelem is just there so the lock can be put in a thread's locks list.
priority is the priority of the thread holding the lock. This is used to keep 
    track of multiple donations.
===
For the semaphore struct:

    int priority;               /*!< Priority of semaphore (used in condvar). */

priority is the priority of the semaphore. This is used to order the 
    semaphores in a conditional variable.

>> B2: Explain the data structure used to track priority donation.
>> Use ASCII art to diagram a nested donation.  (Alternately, submit a
>> .png file.)
I use a linked list to track priority donation. The head of the list 
is the thread requesting the original lock, and the tail of the list 
is the last thread in the donation chain. Thus, if we have threads 
1,2,...,N, with priority(thread i) = i for all i, and each thread j 
holds a lock j' that the j+1th thread is trying to access, we get the 
following linked list:

THREAD N -(waiting on)-> LOCK N-1 -(held by)-> THREAD N-1 -(waiting on) ->
...
THREAD 2 -(waiting on)-> LOCK 1 -(held by) -> THREAD 1

Thus, the priority donation chain would start with the currently running 
thread, in this case, thread N with priority N, trying to acquire lock N-1. 

THREAD N (priority = N) -(LOCK)-> THREAD N-1 (priority = N-1) ...

It would donate its priority to thread N-1, making thread N-1's priority N.

THREAD N (priority = N) -(LOCK) -> THREAD N-1 (priority = N) ...

This continues for all consecutive pairs of threads i and i-1 until the 
priority of thread i-1 is greater than the priority of thread N or we reach 
a thread that isn't waiting on any locks.

---- ALGORITHMS ----

>> B3: How do you ensure that the highest priority thread waiting for
>> a lock, semaphore, or condition variable wakes up first?
Whenever I insert into the waiting list for the desired resource, I insert 
based on the thread's priority, thus turning the waiting list into a priority 
queue. Thus, the highest priority thread will always be at the front of the 
queue. Then, whenever the resource is available, it pops off the thread at 
the front, which will always be the highest priority thread. To ensure this, 
I had to make sure to re-sort the list every time a thread's priority changes 
because that could potentially un-order the list. An alternative method would 
be to re-sort the thread before you want to retreive the desired thread.

>> B4: Describe the sequence of events when a call to lock_acquire()
>> causes a priority donation.  How is nested donation handled?
When a thread wants to acquire a lock and calls lock_acquire(), it first 
sets desired_lock to that lock, meaning it's waiting on that lock. So, 
curr points to lock through its desired_lock member, and lock points to 
its holder through its holder member. Then, a priority donation chain starts.
As described in B2, as long as a next thread in the donation chain exists 
and the current thread's priority is greater than the next thread's priority, 
the next thread's priority is set to the current thread's, and the priority 
of the lock that the current thread wants and the next thread holds is 
updated. Then, curr_lock is updated to be the next lock and lock_holder is
updated to be the holder of the next lock. Then, the semaphore is downed 
and the thread waits until the lock is available. When it finally acquires 
the lock, it no longer is waiting for the lock, so its desired_lock member 
is set to NULL. We also have to make sure to keep track of that lock in the 
thread's locks list (list of the locks that thread currently holds).

>> B5: Describe the sequence of events when lock_release() is called
>> on a lock that a higher-priority thread is waiting for.
When a thread releases a lock, it sets lock->holder to NULL because that 
lock is no longer being held by that thread. The semaphore is upped, and 
the next thread is chosen. Then, the thread has to remove that lock from 
its list of locks it currently holds and check if it needs to reset its 
priority. If its locks list is empty, it holds no resources, so no threads 
are donating priorities to it, so its priority is reset to its original 
priority. If the locks list isn't empty, there could still possibly be 
donating threads, so we set its priority to the priority of the highest 
priority donating thread.

---- SYNCHRONIZATION ----

>> B6: Describe a potential race in thread_set_priority() and explain
>> how your implementation avoids it.  Can you use a lock to avoid
>> this race?
A race condition could occur if you are interrupted by another thread 
setting your priority while you are trying to set your priority. For 
example, if thread B has just set thread A's priority but thread A tries 
to reset its own priority before thread B is done re-ordering the ready 
list, the ready list will possibly end up out of order because the list 
will be sorted with an unexpected value for the priority of B. I "avoid" 
this by re-ordering the thread list before doing anything with it. The 
easy way to avoid this would be to disable interrupts before calling 
thread_set_priority() and re-enable them afterwards. I can use a lock 
to avoid this race by forcing threads to acquire a lock to the thread 
priority before setting it and only relinquishing it after all of the 
priority setting is complete.

---- RATIONALE ----

>> B7: Why did you choose this design?  In what ways is it superior to
>> another design you considered?
I chose this design because it seemed simple and straightfoward, and it 
worked. Also, I didn't consider any other designs that seemed feasible. 
At first, to implement multiple donations, I tried keeping a list of 
threads instead of a list of locks. The list of threads would contain 
all of the threads donating a priority to that thread. I forgot why I 
gave up on that method, but I think it's because I couldn't get the code 
to compile when I tried to put the list into the thread struct.

			  ADVANCED SCHEDULER
			  ==================

---- DATA STRUCTURES ----

>> C1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

Oh my goodness...

In thread.h, inside the thread struct:

    int mlfq_priority;                  /*!< Priority for mlfq -- separated
                                             variable to avoid possible
                                             conflicts. */
    int nice;                           /*!< Thread niceness for mlfq. */
    int recent_cpu;                     /*!< Thread recent_cpu in 17.14
                                             fixed point arithmetic */

These are to keep the values as needed by the priority calculation.

In thread.c, in the global space:

int f = 1 << 14; // For fixed point arithmetic
// System load average in 17.14 fixed point number representation.
static int load_avg;

static bool thread_inited = false;

The last one of these is to make sure calculations don't happen until
the threading system is initialized.

---- ALGORITHMS ----

>> C2: Suppose threads A, B, and C have nice values 0, 1, and 2.  Each
>> has a recent_cpu value of 0.  Fill in the table below showing the
>> scheduling decision and the priority and recent_cpu values for each
>> thread after each given number of timer ticks:

Since 36 < 100, none of the once-per-second stuff occurs.

timer  recent_cpu    priority   thread  ready_list
ticks   A   B   C   A   B   C   to run    state   (before scheduling occurs)
-----  --  --  --  --  --  --   ------  ---------
 0     0   0   0   63  61  59      A      A->B->C
 4     4   0   0   62  61  59      A      B->C->A
 8     8   0   0   61  61  59      B      B->C->A
12     8   4   0   61  60  59      A      C->A->B
16     12  4   0   60  60  59      B      C->B->A
20     12  8   0   60  59  59      A      C->A->B
24     16  8   0   59  59  59      B      C->B->A
28     16  12  0   59  58  59      C      C->A->B
32     16  12  4   59  58  58      A      A->B->C
36     20  12  4   58  58  58      B      B->C->A

>> C3: Did any ambiguities in the scheduler specification make values
>> in the table uncertain?  If so, what rule did you use to resolve
>> them?  Does this match the behavior of your scheduler?

This doesn't specify when the scheduler was called. My code will
run the scheduler when a thread uses up its time slice or
yields/blocks/etc. In the table above, I rescheduled the currently
running thread whenever a different thread got a higher priority. 
In practice, I think this uses a lot of time overhead though (checking
for higher priority threads) so I just let the thread use up its
time slice.

>> C4: How is the way you divided the cost of scheduling between code
>> inside and outside interrupt context likely to affect performance?

There is a lot of updating of every-thread stuff going on with interrupts
disabled, so this could cause performance issues.

---- RATIONALE ----

>> C5: Briefly critique your design, pointing out advantages and
>> disadvantages in your design choices.  If you were to have extra
>> time to work on this part of the project, how might you choose to
>> refine or improve your design?

I guess my design works. That is a pretty big advantage. It is also
pretty much the most straightforward implementation, so I imagine
it must be pretty easy to understand what it does.

I guess if I had more time I could work a little more on doing fewer
things with interrupts disabled and inside interrupt contexts.

>> C6: The assignment explains arithmetic for fixed-point math in
>> detail, but it leaves it open to you to implement it.  Why did you
>> decide to implement it the way you did?  If you created an
>> abstraction layer for fixed-point math, that is, an abstract data
>> type and/or a set of functions or macros to manipulate fixed-point
>> numbers, why did you do so?  If not, why not?

I just did it directly, because it was the most straightforward way
to do it. Implementing a library might have made the code more readable
but I only had to use fixed-point arithmetic in like three functions
so it didn't seem that worth it. By the time the thought even came
to me to do it the other way, I had already written those functions :O

			   SURVEY QUESTIONS
			   ================

Answering these questions is optional, but it will help us improve the
course in future quarters.  Feel free to tell us anything you
want--these questions are just to spur your thoughts.  You may also
choose to respond anonymously in the feedback survey on the course
website.

>> In your opinion, was this assignment, or any one of the three problems
>> in it, too easy or too hard?  Did it take too long or too little time?

alarm_clock is fine ~Ilya

mlfq is also fine if I'm not being dumb ~Ilya

>> Did you find that working on a particular part of the assignment gave
>> you greater insight into some aspect of OS design?

>> Is there some particular fact or hint we should give students in
>> future quarters to help them solve the problems?  Conversely, did you
>> find any of our guidance to be misleading?

Maybe include more documentation on how the lists work, since I ran into a few
nasty bugs while coding alarm_clock. ~Ilya

>> Do you have any suggestions for the TAs to more effectively assist
>> students, either for future quarters or the remaining projects?

>> Any other comments?