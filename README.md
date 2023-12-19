# Operating-System
OS Projects on xv6

## Project 1
#### Booting xv6 operating system

xv6 is a Unix-like teaching operating system developed by MIT.
It is based on multiprocessor x86 system
(using vim, grep, ctags, cscope)


## Project 2
#### Making system calls

**Goal : make new three system calls(getnice, setnice, ps)**
The getnice function obtains the nice value of a process.
The setnice function sets the nice value of a process.
The ps system call prints out process(s)’s information, which includes name, pid, state and priority(nice value) of each process.

steps:
1. Add your syscall to usys.S
2. Add syscall number to syscall.h
3. Add extern and syscall element in syscall.c
4. Add a sys_function to sysproc.c
5. Add a function that performs a real action to proc.c
6. Add a definition to defs.h and user.h


## Project 3
#### Implement CFS on xv6

CFS (Completely Fair Scheduling) is the Linux default scheduler. 
Before implementing CFS, the xv6 chooses the process to be scheduled in round-robin manner.
To make a fair scheduler, CFS is used. The CPU is allocated to the process in proportion to its weight. 

steps:
1. A task with minimum virtual runtime is scheduled
2. Scheduled task gets time slice proportional to its {weight / total weight}
3. While the task is running, virtual runtime is updated
4. After task runs more than time slice, go back to 1


## Project 4
#### Virtual Memory
Project 4 implements 3 system calls and page fault handler
: mmap() syscall, page fault hander, munmap() syscall, freemem() syscall


**mmap() syscall** can be given flags with the following combinations
: if MAP_ANONYMOUS is given, it is anonymous mapping, if not given, it is file mapping.
: if MAP_POPULATE is given, allocate physical page & make page table for whole mapping area, if not given, just record its mapping area.

**Page Fault Handler** is for dealing with access on mapping region with physical page when page table is not allocated.
When succeeded, physical pages and page table entries are created normally, and the process works without any problems. When failed, the process is terminated.

**munmap() syscall** unmaps corresponding mapping area.

**freemem() syscall** is to return current number of free memory pages.


## Project 5
#### Page Replacement
Project 5 implements page-level swapping. 

Swap-in: move the victim page from backing store to main memory
Swap-out: move the victim page from main memory to backing store

Manage swappable pages with LRU list using clock algorithm. 