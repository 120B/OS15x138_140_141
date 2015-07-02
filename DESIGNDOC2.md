		     +--------------------------+
       	       	     |		CS 140	|
		     | PROJECT 2: USER PROGRAMS |
		     | 	   DESIGN DOCUMENT       |
		     +--------------------------+


---- GROUP ----

657459219@qq.com	10132510140	jingbin yang
740538396@qq.com	10132510138	qiaowei chen
1481439920@qq.com	10132510141 wenwen  qu

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.
>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

			   ARGUMENT PASSING
			   ================

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

We did not rely on new struct, global of static variables here to implement argument passing. However, we do design separate functions for the task,
These are in process.c:
/* push 4 bytes of data on top of stack at *p_stack, adding safety check
   to ensure the push does not overflow stack page */
static bool push_4byte (char** p_stack, void* val, void** esp);

/* separates the program file name from command line  */  
static void get_prog_file_name (const char* cmd_line, char* prog_file_name);

/* parse cmd_line, separate program file name and following arguments
   and push to stack exactly as illustrated in pintos document 3.5.1. */
static bool argument_passing (const char *cmd_line, void **esp);


---- ALGORITHMS ----

>> A2: Briefly describe how you implemented argument parsing.  How do
>> you arrange for the elements of argv[] to be in the right order?
>> How do you avoid overflowing the stack page?

To implement argument parsing, we first scan in reverse order (from right
to left) to find each argument within the input command line using pointer char *curr, and for each argument encountered, we push it on top of user stack beginning from PHYS_BASE - 1; after successfully pushing all the arguments, we insert word-alignment segement for better accessing speed; after that, we scan the stack for the address of each argument, and push it on stack. The elements of argv[] is ensured to be in right order by push the arguments into stack in reverse order. 

We also implement method to avoid overflowing the stack page. Firstly, when we push any argument to the the stack, we ensure that the argument length is not longer than available space; secondly, we add a utility function push_4byte to push argv addresses, argv, argc and fake return address to the top of user stack, and by performing boundary check to make sure each push does not overflow the stack page.

---- RATIONALE ----

>> A3: Why does Pintos implement strtok_r() but not strtok()?

strtok() typically uses a static pointer to store the states (the position in

>> A4: In Pintos, the kernel separates commands into a executable name
>> and arguments.  In Unix-like systems, the shell does this
>> separation.  Identify at least two advantages of the Unix approach.

The first advantage of Unix approach is that it is much safer and simpler to 
use shell-based parsing operations. This way shell could help check any unsafe command line before they arrive at kernel directly, and thus reduce the complexity of kernel operations.

Moreover, combined with PATH environment variable, Unix-like shell gives more flexibility in looking for executable files. Revolving pathname and looking for files tend to be expensive, and it is best left to external programs such as shell to finish the task as Unix-like systems did. With Pintos' approach,the kernel would have to undertake the task to look for a file during the initialization of a process.


			     SYSTEM CALLS
			     ============

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

process*/
    struct semaphore sema_wait;		/* Sema to ensure wait order between child process
					   and parent process */
    bool already_waited;		/* Whether the process has already been 
					   waited by its parent */
    bool parent_alive;			/* Whether the parent process is 
                                           alive*/
    bool is_alive;			/* Whether the process is alive */
    int exit_status;			/* Record exit status */
    int pid;				/* Record the pid */
    struct list_elem elem;		/* Element in child_list of its parent 
					   thread */
  };


>> B2: Describe how file descriptors are associated with open files.
>> Are file descriptors unique within the entire OS or just within a
>> single process?

In our implementation, each process maintains its own list of open files in 
an array of length 128. While the first 2 elements are intentionally left
NULL for STDIN_FILENO and STDOUT_FILENO, each subsequent array index is used
as a file descriptor corresponding to the open file stored in this array 
element.

When opening a file we search in the array to find a vacancy, store the pointer to file_info structure there, and return the index as file descriptor.And when closing a file we can go directly according to file descriptor 
(index in array) to find the file_info structure and close it. This way, each process has an independent set of file descriptors, and the file 
descriptors are not inherited by child processes. In each process, current 
opened files has a unique id to distinguish from each other, which achieves 
the requirements of the project.

In our implementation, file descriptors are unique just within a single 
process. Different processes may have the same file descriptors.


---- ALGORITHMS ----

>> B3: Describe your code for reading and writing user data from the
>> kernel.

For the next step, we check whether the address provided by user is valid.
For every page in the range of memory that can be reached from user supplied
address (e.g. from buffer to buffer + size in read() and write()), we first
verify a user virtual address is below PHYS_BASE, and then we verify that the
particular page is mapped. In doing so, we ensure the system calls all the
conditions required to perform the data transfer tasks. If there is any other
situation that will cause unexpected termination of the process, the page_fault
handler will take in position.

Finally we deal with the typical case where actual file I/O is requested. The
list of open files recorded in thread structure is queried to find the file
structure according to file descriptor. Then this file structure is used to
call file system methods file_read_at() or file_write_at() to finish the task.
Of course, all these file system calls are protected by a global lock to avoid
race conditions. 


>> B4: Suppose a system call causes a full page (4,096 bytes) of data
>> (e.g. calls to pagedir_get_page()) that might result?  What about
>> for a system call that only copies 2 bytes of data?  Is there room
>> for improvement in these numbers, and how much?

In our current implementation approach, if a system call causes a full page of
data to be copied, the greatest possible number of inspection could be 2, and 
within one page, we only check once.

When copying 2 bytes of data, the situation in is similar. The number of 
inspections depends largely on how many pages the data actually spans. But
ofcourse, the probability of checking two pages in this case is far lower than
the above case. 

A different mean is not to check the address for read and write, but let 
the operations continue until there is a pagefault occuring. Then we can 
implement handler to deal with the user pagefault situation properly. In this 
way, if the address is valid, then 0 inspection is needed. For the reading
of 2 bytes of data, which would happen much more frequently, significant amount
of time will be saved. And if an invalid span is attempted, the system would 
write data to the valid address until the invalid address appears. It is clear
that reading and writing data are much faster than calls to the time-consuming
pagedir_get_page for inspections. 


>> B5: Briefly describe your implementation of the "wait" system call
>> and how it interacts with process termination.

Whenever a "wait" system call is called, the function operates as explained
below:

First, current thread would search among his child list to find if there 
is one process of his child matches the given pid. If there is one, the child 
process's metadata is retrieved. 

Then we will examine if the parent process has already waited for the child,
by checking the already_waited member. If the parent has already waited 
once for the child, then it immediately returns -1 as required by the 
assignment. If the process has not been waited for this child before, then 
we set the already_waited information to true, preventing further waits in
the future.

its parent's sema_down in case the parent not waiting for this child 
process. 


>> B6: Any access to user program memory at a user-specified address
>> can fail due to a bad pointer value.  Such accesses must cause the
>> process to be terminated.  System calls are fraught with such
>> accesses, e.g. a "write" system call requires reading the system
>> call number from the user stack, then each of the call's three
>> arguments, then an arbitrary amount of user memory, and any of
>> these can fail at any point.  This poses a design and
>> error-handling problem: how do you best avoid obscuring the primary
>> function of code in a morass of error-handling?  Furthermore, when
>> an error is detected, how do you ensure that all temporarily
>> allocated resources (locks, buffers, etc.) are freed?  In a few
>> paragraphs, describe the strategy or strategies you adopted for
>> managing these issues.  Give an example.

There are two shields in our program making sure that the system call
successfully deal with fail. The first one is the function checkvaddr
as mentioned in above questions. To avoid obscuring the primary function
of code by error handling, we ensured a proper work environment for each
of the calls before it actually starts to work. The error handling function is
summarized together into one function to eliminate the possible causes that
may induce fail. In this layer of shield, we focus on prevention before the
fail actually occurs, rather than to handle all the errors after it actually
happens. If we detect any insufficiency for the process to execute 
successfully, we will not start execution but kill the process with 
thread_exit. Another shield is the page_fault handler when a fail indeed 
occurs. This will rarely happen in our design, but if it indeed happens, it 
will also follow the thread_exit routine.


---- SYNCHRONIZATION ----

>> B7: The "exec" system call returns -1 if loading the new executable
>> fails, so it cannot return before the new executable has completed
>> loading.  How does your code ensure this?  How is the load
>> success/failure status passed back to the thread that calls "exec"?


>> B8: Consider parent process P with child process C.  How do you
>> ensure proper synchronization and avoid race conditions when P
>> calls wait(C) before C exits?  After C exits?  How do you ensure
>> that all resources are freed in each case?  How about when P
>> terminates without waiting, before C exits?  After C exits?  Are
>> there any special cases?

In our implementation, for every process, we create a struct process_info to 
record the metadata information of the process, so even when the current 
process exits, its parent process is still able to retrieve its exit_status, 
pid, etc. Each thread has a pointer pointing to its metadata, and has a 
member to record the struct representing its parent thread.

Let's denote parent process for P and child process for C for simplicity.

For synchronization, we use a sema_wait semaphore, a member of C's metadata
"process_info", to ensure the synchronization. When P calls wait syscall, it
The wait semaphore cannot be a member of P, because if this is the case, the
semaphore is shared between all its children processes. Since there is no way
for the child process at the time of exit to know if its parent is going to
wait for it or not (even P does not know, because you can't predit the 
future), it may falsely sema_up or not sema_up. For example, if C sema_up and
P doesn't actually wait for it, the sema-value is falsely increased by one,
causing P's next wait(sema_down) useless; on the other hand, if C does not 
sema_up and C actually waits for it some time in the future, then P will be 
blocked and perhaps never wake up again. 

Particularly, if P calls wait before C exits, then P will sema_down semaphore
sema_wait, and goes to sleep; when C finishes executing, C sema_up sema_wait
and do not free its metadata, because its parent is still alive (parent_alive
== true); then P wakes up, it continues executing, and at the time of exit,
it frees C's metadata, because by then C is a dead child process.

If P calls wait after C exits, then before C exits, it will sema_up and do
not free its metadata; the P then calls wait and sema_down, since the
semaphore is sema_up by C once, so sema_down does not put P to sleep, so P
will continue and at the time of exit, free C's metadata. Note in this case
and above case, whether or not to free P's metadata is depending on the
status of P's parent.

If P terminates without waiting, before C exits, then when P exits, it
informs C that it is dead; when C exits, it sema_up(though no use here), and
free its own metadata, because P is dead.

If P terminates without waiting, after C exits, then when C exits, it
sema_up, and when P exits, it frees C's metadata, as expected.  


---- RATIONALE ----

>> B9: Why did you choose to implement access to user memory from the
>> kernel in the way that you did?

>> B10: What advantages or disadvantages can you see to your design
>> for file descriptors?

In our implementation, each process maintains its own list of open files in 
an array of length 128. While the first 2 elements are intentionally left
NULL for STDIN_FILENO and STDOUT_FILENO, each subsequent array index is used
as a file descriptor corresponding to the open file stored in this array element. When opening a file we search in the array to find a vacancy, store the pointer to file_info structure there, and return the index as file descriptor. And when closing a file we can go directly according to file descriptor (index in array) to find the file_info structure and close it.

The main advantages of our design for file descriptors lie in the overall speed performance. Our implementation gives O(n) time complexity for 
allocating file descriptors when opening files, while O(1) time complexity for all subsequent query operations in any syscall involving file descriptors.
The disadvantages of our design could be that the number of open files allowed for each process is limited, or that when the number of open files is small, it wastes memory space. We have considered these short-comings and understood we can solve them by using dynamic structures such as list. It is
our choice to favor speed that we choose the design with array and file 
descriptor as index in the array.


>> B11: The default tid_t to pid_t mapping is the identity mapping.
>> If you changed it, what advantages are there to your approach?

We did not change this part. We use the default implementation that is provided with Pintos.



			   SURVEY QUESTIONS
			   ================

Answering these questions is optional, but it will help us improve the
course in future quarters.  Feel free to tell us anything you
want--these questions are just to spur your thoughts.  You may also
choose to respond anonymously in the course evaluations at the end of
the quarter.

>> In your opinion, was this assignment, or any one of the three problems
>> in it, too easy or too hard?  Did it take too long or too little time?

>> Did you find that working on a particular part of the assignment gave
>> you greater insight into some aspect of OS design?

>> Is there some particular fact or hint we should give students in
>> future quarters to help them solve the problems?  Conversely, did you
>> find any of our guidance to be misleading?

>> Do you have any suggestions for the TAs to more effectively assist
>> students, either for future quarters or the remaining projects?
No
>> Any other comments?