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
#define ARG_CODE 0
#define ARG_0 4
#define ARG_1 8
#define ARG_2 12

//declaration of the loadStack function
static uint32_t loadStack (struct intr_frame *itrf, int);

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


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// Controls the system calls cases 
static void
syscall_handler (struct intr_frame *itrf)
{
  int protocol = (int) loadStack (itrf, ARG_CODE);

  switch (protocol)
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
	}
  thread_exit ();
}

static uint32_t loadStack(struct intr_frame *itrf, int store)
{
	//itrf points to stack pointer	
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
