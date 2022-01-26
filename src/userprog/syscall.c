#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"

//definitions for argument codes
//stores the value that will be added to the stack pointer depending on which
//system call is used
#define ARG_CODE 0
#define ARG_0 4
#define ARG_1 8
#define ARG_2 12

//declaration of the loadStack function
static uint32_t loadStack (struct intr_frame *itrf, int);

static struct semaphore *fileLock;

//declaration of the syscall handler
static void syscall_handler (struct intr_frame *itrf);

void sys_exit (int status);
// sys_exit declaration

int sys_wait (pid_t pid);
// sys_wait declaration

void sys_halt (void);
// sys_halt declaration

pid_t sys_exec (const char *cmd_line);
// sys_exec declaration

bool sys_remove (const char *f);
// sys_remove declaration

bool sys_create (const char *n, unsigned int size);
//  sys_create declaration


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  sema_init (&fileLock, 1);
  //initialises the semaphore. 
}

// Controls the system calls cases 
static void
syscall_handler (struct intr_frame *itrf)
{

  int syscode = (int) loadStack (itrf, ARG_CODE); //Holds syscall code

  switch (syscode)
  {
	//creation of SYS_EXIT sytem call case state  
	case SYS_EXIT:
		sys_exit ((int) loadStack (itrf, ARG_0));
		printf("System Exit Call!");
		break;


	//creation of SYS_WAIT system call case state | itrf points to eax
	//register which stores return of function	
	case SYS_WAIT:
		itrf->eax = sys_wait ((pid_t) loadStack (itrf, ARG_0));
		printf("System Wait Call!");
		break;


	//creation of SYS_HALT system call case state	
	case SYS_HALT:
		sys_halt();
		printf("System Halt Call!");
		break;


	//creation of SYS_EXEC system call case state
	case SYS_EXEC:
		itrf->eax = sys_exec ((const char *) loadStack (itrf, ARG_0));
		printf("System Execute Call!");
		break;


	//creation of SYS_REMOVE system call case state
	case SYS_REMOVE:
		itrf->eax = sys_remove ((const char *) loadStack (itrf, ARG_0));
		break;
	

	//creation of SYS_CREATE system call case state
	case SYS_CREATE:
		itrf->eax = sys_create ((const char *) loadStack (itrf	, ARG_0),
		(unsigned int) loadStack (itrf, ARG_1));
		break;
	}

  thread_exit ();

}

static uint32_t loadStack(struct intr_frame *itrf, int store)
{
	//returns a 32-bit stack pointer plus the ARG_CODE value (0,4,8,12)
	//pointer to by the interrupt frame	
	return *((uint32_t *) (itrf->esp + store));
}

// The function for sys_exit which terminates the current process being executed
void sys_exit (int status)
{
	struct thread *th = thread_current ();
	th->exit_code = status; //sets exit_code of current thread to status
	thread_exit();
}

// The function for sys_wait which can halt processes if other ones need to have
// a higher priority
int sys_wait (pid_t pid)
{
	return process_wait (pid);
}

// The function for sys_halt terminates the pintOS
void sys_halt (void)
{
	shutdown_power_off();
}

// The function for sys_exec allows you to run the executable given in the
// command line
pid_t sys_exec (const char *cmd_line)
{
	return process_execute (cmd_line);
}

// The function for sys_remove removes a name from the file system
bool
sys_remove (const char *filename)
{
	bool complete; //Stores true if completed
	sema_down (&fileLock); 
	//when sema down is called it locks the file so other system call cannot access it
	complete = filesys_remove (filename); //removes the given file
	sema_up (&fileLock); //sema_up on fileLock indicates the file is avaliable again
	return complete;
}

// the function for sys_create creates new files and returns whether they are
// true or false

bool
sys_create (const char *filename, unsigned int size)
{
	bool complete; //Stores true if completed
	sema_down (&fileLock); //Locks the file
	complete = filesys_create (filename, size); //creates the file with a name and size
	sema_up (&fileLock); //Unlocks the file
	return complete;
}
