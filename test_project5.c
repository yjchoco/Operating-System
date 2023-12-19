#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "proc.h"
#include "syscall.h"


// Simple private file backed mapping test
void file_private_test() {
  printf(1, "file backed private mapping test\n");
  int fd = open("README", O_RDWR);
  if (fd == -1) {
    printf(1, "file backed private mapping test failed: at open\n");
    exit();
  }
  int size = 1000;
  char buf[1000];
  int n = read(fd, buf, size);
  if (n != size) {
    printf(1, "file backed private mapping test failed: at read\n");
    exit();
  }
  printf(1, "before mmap: %d\n", freemem());
  int ret = mmap(0, size, PROT_READ | PROT_WRITE, MAP_POPULATE, fd, 0);
  if (ret == 0) {
    printf(1, "file backed private mapping test failed1\n");
    exit();
  }
  printf(1, "after mmap, before munmap: %d\n", freemem());
  int res = munmap(ret);
  printf(1, "after munmap: %d\n", freemem());
  if (res == -1) {
    printf(1, "file backed private mapping test failed4\n");
    exit();
  }
  close(fd);
  printf(1, "file backed private mapping test ok\n");
}

void file_private_test_modified() {
  printf(1, "file backed private mapping test\n");
  int fd = open("README", O_RDWR);
  if (fd == -1) {
    printf(1, "file backed private mapping test failed: at open\n");
    exit();
  }
  int size = 4096;
  int addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_POPULATE, fd, 0);
  if (addr == -1) {
    printf(1, "file backed private mapping test failed: at mmap\n");
    close(fd);
    exit();
  }
  // Test print from the image example
  char *buf = (char *)addr;
  printf(1, "- fd data: %c %c %c\n", buf[0], buf[1], buf[2]);

  int res = munmap(addr);
  if (res == -1) {
    printf(1, "file backed private mapping test failed: at munmap\n");
    close(fd);
    exit();
  }
  close(fd);
  printf(1, "file backed private mapping test ok\n");
}




// Simple private anonymous mapping test with maping having both read and write
// permission and size greater than two pages
void anon_private_test() {
  printf(1, "anonymous private mapping test\n");
  int size = 10000;
  int ret = mmap(0, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  if (ret == 0) {
    printf(1, "anonymous private mapping test failed\n");
    exit();
  }
  int res = munmap(ret);
  if (res == -1) {
    printf(1, "anonymous private mapping test failed\n");
    exit();
  }
  printf(1, "anonymous private mapping test ok\n");
}

// private file backed mapping with fork test
void file_private_with_fork_test() {
  printf(1, "file backed private mapping with fork test\n");
  int size = 4096;
  int fd = open("README", O_RDWR);
  if (fd == -1) {
    printf(1, "file backed private mapping with fork test failed\n");
    exit();
  }
  printf(1, "before mmap: %d\n", freemem());
  int ret = mmap(0, size, PROT_READ | PROT_WRITE, MAP_POPULATE, fd, 0);
  printf(1, "after mmap before fork: %d\n", freemem());
  int pid = fork();
  printf(1, "after fork: %d\n", freemem());
  if (pid == 0) {
    exit();
  } else {
    wait();
    printf(1, "before munmap: %d\n", freemem());
    int res = munmap(ret);
    printf(1, "after munmap: %d\n", freemem());
    if (res == -1) {
      printf(1, "file backed private mapping with fork test failed\n");
      exit();
    }
    printf(1, "file backed private mapping with fork test ok\n");
  }
}

// Utility strcmp function
int my_strcmp(const char *a, const char *b, int n) {
  for (int i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return 1;
    }
  }
  return 0;
}

// Private mapping with fork
void anon_private_fork_test() {
  printf(1, "anonymous private mapping with fork test\n");
  int size = 4096;
  printf(1, "before mmap: %d\n", freemem());
  int ret = mmap(0, size, PROT_READ | PROT_WRITE, MAP_POPULATE | MAP_ANONYMOUS, -1, 0); // Shared mapping
  
  printf(1, "after mmap: %d\n", freemem());
  if (ret == 0) {
    printf(1, "anonymous private mapping with fork test failed\n");
    exit();
  }
  printf(1, "before fork: %d\n", freemem());
  int pid = fork();
  printf(1, "after fork: %d\n", freemem());
  if (pid == 0) {
    exit();
  } else {
    printf(1, "before wait: %d\n", freemem());
    wait();
    printf(1, "after wait: %d\n", freemem());
    printf(1, "anonymous private mapping with fork test ok\n");
    printf(1, "before munmap: %d\n", freemem());
    munmap(ret);
    printf(1, "after munmap: %d\n", freemem());
  }
}




int main()
{
//    file_private_test();
file_private_with_fork_test();
//printf(1, "%d\n", freemem());

}
/*

#ifndef NELEN
#define NELEN(x)(sizeof(x)/sizeof(x[0]))
#endif

#ifndef BSIZE
#define BSIZE 512
#endif

#ifndef PGSIZE 
#define PGSIZE 4096
#endif

#define NTEST 5

void printer(char * buf, int len)
{
	char c;
	for (int i = 0; i < len; i++) 
	{
		if (i % PGSIZE == 0)
			printf(1, "\t");
		else if (i % PGSIZE == PGSIZE-1)
			printf(1, "\n");
		c = buf[i];
		printf(1, "%c", c);
	}

}

int 
main(int argc, char* argv[])
{

	int fd, pid, print; 
	char * alloc[NTEST]; 
  int addr;
	print = 0;
	if (argc > 1)
		print = 1;
	fd = open("README", O_RDWR); 

	int len[NTEST] = {8*PGSIZE, 16*PGSIZE,   2*PGSIZE, 4*PGSIZE, 16*PGSIZE};
	int idx[NTEST] = {0};
	for (int i = 1; i < (int)NELEN(idx); i++)
	{
		idx[i] = idx[i-1] + len[i-1];
	}
	//= {0,        2*PGSIZE, 3*PGSIZE, 8*PGSIZE, 11*PGSIZE};   
	// fd, anon, fd&pop, anon&pop, fd&pop
	int flag[NTEST] = {0, MAP_ANONYMOUS, MAP_POPULATE, MAP_ANONYMOUS|MAP_POPULATE, MAP_POPULATE};
	int prot[NTEST] = {PROT_READ|PROT_WRITE, PROT_READ|PROT_WRITE, PROT_READ|PROT_WRITE, PROT_READ|PROT_WRITE, PROT_READ|PROT_WRITE};
	int fdarr[NTEST] = {fd, -1, fd, -1, fd};
for (int i = 0;i < (int)NELEN(idx); i++){
		printf(1, "\n");
		printf(1, "before mmap: freemem is %d\n", freemem());
		printf(1, "%d: mmap:(M[%d*PGSIZE], %d*PGSIZE, %s, %s)->", i, idx[i]/PGSIZE, len[i]/PGSIZE, flag[i]&MAP_POPULATE ? "pop":"unpop", flag[i]&MAP_ANONYMOUS?"anon":"fd");

		addr = mmap(idx[i], len[i], prot[i], flag[i], fdarr[i], 512);
    alloc[i] = (char*) addr;
		printf(1, "Mem[%x]\n", alloc[i]);
	 	//mmap(idx[i], len[i], prot[i], flag[i], fdarr[i], 0);


		if (!(flag[i] & MAP_POPULATE)) {
			for (int p=PGSIZE; p <= len[i]; p+=PGSIZE)
				alloc[i][p-1] = '\0'; // null character
			printf(1, "\n\tcontents\n%s\n", alloc[i]); 
		}
		

		printf(1, "after mmap: freemem is %d\n", freemem());
		printf(1, "\n");
	}
if (print) {
	printf(1, "\n");
	alloc[0][4*PGSIZE-1] = '\0';
	printf(1, "%s\n", &alloc[0][3*PGSIZE]);
}
alloc[3][0]  = 'A';
alloc[3][1]  = 'B';
alloc[3][2]  = '\0';
	printf(1, "before fork: freemem is %d\n", freemem());
	pid = fork();

	if (pid) {
		wait();
		printf(1, "\nafter child exit, freemem: %d\n\n", freemem());
		printf(1, "Parent (pid:%d) process\n", getpid()); 
		printf(1, "after fork: freemem is %d \t", freemem());
		printf(1, "\n");
	} else {
		// since the real entry of pgdir of child is made on demand 
		alloc[3][2]  = 'C';
		alloc[3][3]  = '\0';
		printf(1, "%s\n", alloc[3]);
		printf(1, "Child (pid:%d) process\n", getpid()); 
		printf(1, "after fork: freemem is %d \t", freemem());
		printf(1, "\n");
	}

	for (int i = 0;i < (int)NELEN(idx); i++){
		printf(1, "\n");
		printf(1, "%d: munmap:(M[M2V(%d * PGSIZE)], %d * PGSIZE, %s, %s)\n",\
				i, idx[i]/PGSIZE, len[i]/PGSIZE, flag[i]&MAP_POPULATE ?\
				"pop":"unpop", flag[i]&MAP_ANONYMOUS?"anon":"fd");
		printf(1, "before munmap: freemem is %d\n", freemem());
		if (print) {
		  printf(1, "\n%s\n\n", alloc[i]); 
		  printf(1, "\n");
		}
		printf(1, "unmap_result: %d\n", munmap(addr));
		printf(1, "after munmap: freemem is %d\n", freemem());
	}
	if (pid)	
		printf(1, "FINAL freemem: %d\n", freemem());
    close(fd);
	exit();
	return 0;
}
*/