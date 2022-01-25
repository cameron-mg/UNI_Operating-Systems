#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

//Structures
enum load_status{
	LOAD_SUCCESS, //File has been loaded successfully
	LOAD_FAILED, //File has not been loaded successfully
	NOT_LOADED //File is currently waiting to load
};

struct process 
{
	pid_t pid; //Stores process ID
	struct list_elem elem; //List element for child processes
	bool waiting; //True if process if being waited for
	bool exited; //True if process has exited
	enum load_status load_status; //Stores load status of current proc
	int exit_code; //Stores exit code of process
	struct semaphore wait; //Used for synchronization, waiting for exit
	struct semaphore load; //^^, waiting for executing file to load
	const char* cmdline; //Stores given command line from user
	struct thread *parent //Stores parent thread of process
};

//Functions
static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static void push_args (const char *[], int count, void **esp);
void update_load_status (struct thread *child, enum load_status status);
struct process *get_child (struct thread *, tid_t);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */

tid_t
process_execute (const char *cmdline) 
{
  char *cmd_copy;
  tid_t tid;
  char *filename;
  char *save_ptr = NULL;
  struct process *proc = NULL;
  struct thread *th;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */

  cmd_copy = palloc_get_page (0);
  if (cmd_copy == NULL)
	return TID_ERROR;
 
  strlcpy (cmd_copy, cmdline, PGSIZE);

  //Extract filename from the commmand lne
  filename = strtok_r(cmd_copy, " ", &save_ptr);

  //Testing statements
  printf("Filename%s\n", filename);

  //Initialising proc info
  proc->pid = 0;
  proc->parent = thread_current();
  proc->cmdline = cmd_copy;
  proc->waiting = false;
  proc->exited = false;
  proc->exit_code = -1;
  proc->load_status = NOT_LOADED;

  sema_init(&proc->load, 0);
  sema_init(&proc->wait, 0);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (filename, PRI_DEFAULT, start_process, proc);

  if (tid == TID_ERROR)
    palloc_free_page (cmd_copy); 

  if (proc == NULL)
  {
	  palloc_free_page (cmd_copy);
	  return -1;
  }

  th = thread_current();

  //Add process to child list of thread
  list_push_back (&th->children, &proc->elem);

  //Synch for load
  sema_down (&proc->load);

  //Check to confirm process has loaded
  if (proc->load_status == LOAD_FAILED)
	  return -1;

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *proc_)
{
  //Initialize variables and structures
  struct intr_frame if_;
  bool success;
  struct process *proc = proc_; //Copy of process passed to start_process
  struct thread *th = thread_current();
  char *filename = (char*) proc->cmdline;
  char *token; //Stores each arg token used to push args to stack
  char *save_ptr; //Used for strtok_r function
  int count = 0; //Used to store argc

  //Argument tokenization
  const char **argtoks = (const char**) palloc_get_page(0);

  //Check we have memory for the argument array
  if (argtoks == NULL)
  {
	  printf("Command line failed");
	  thread_exit();
  }

  //Looping through arguments and saving into array
  for (token = strtok_r(filename, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr))
  {
	  count++;
	  argtoks[count] = token;
  }

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  //Load the file
  success = load (filename, &if_.eip, &if_.esp);

  //If successfully loaded add arguments to the stack and update stack
  if (success)
  {
	//Update load_status of current thread with SUCCESS or FAILED
	update_load_status(th, success ? LOAD_SUCCESS : LOAD_FAILED);
	
	//Add arguments to the stack passing the array of tokenized arguments,
	//the amount of arguments and the current stack pointer
	push_args (argtoks, count, &if_.esp);
  }
  
  //Free the memory page holding the tokens now they have been passed to stack
  palloc_free_page(argtoks);

  /* If load failed, quit. */
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
    //Structures and variables
    int exit_code;
    struct thread *th = thread_current();
    struct process *proc = get_child (th, child_tid);

    //Set process wait status to true and set its semaphore
    sema_down (&proc->wait);
    proc->waiting = true;

    //Remove the child process from the list element
    list_remove (&proc->elem);

    //Set the processes exit_code and return
    exit_code = proc->exit_code;
    return exit_code;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  /* Cameron Mackay-Grant */
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */

  pd = cur->pagedir;
  if (pd != NULL) 
    {

      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */

      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  //Giving exit code a value of -1 if it is a NULL value and has not been set
  //elsewhere
  if (cur->exit_code == NULL)
	  cur->exit_code = -1;
  
  //Print the name and exit_code of the process that exits
  printf("%s: exit(%d)\n", cur->name, cur->exit_code);

}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false; //Stores true if the executable successfully loads
  int i; //Counter

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);

  //Check file is not a NULL value
  if (file == NULL) 
    {
	printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  //When success is returned the arguments are pushed to the stack in the
  //start_process function.
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success) {
        *esp = PHYS_BASE;
      } else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

//--------------------------------------------------------------------
//Added functions for argument passing:
//Function to update the load status of a specific process
void update_load_status (struct thread *child, enum load_status status)
{
	//Get the child process
	struct process *proc = get_child (child->parent, child->tid);

	//Check process has a value
	if (proc == NULL)
		return;

	//Set the processes load status and update the load semaphore
	proc->load_status = status;
	sema_up (&proc->load);
}

//Structure to return the child of a specific thread
struct process *get_child (struct thread *th, tid_t child_tid)
{
	struct list_elem *el; //List element for child processes
	struct process *proc; //Stores the child process after it is found

	//Check given thread is not null value
	if (th == NULL)
		return NULL;

	//Loop through list to find child
	for (el = list_begin (&th->children); el != list_end (&th->children); el = list_next (el))
	{
		//Set the process to the current entry
		proc = list_entry (el, struct process, elem);

		//Check if the process id matches that of the child
		if (proc->pid == (pid_t) child_tid)
			return proc;

	}

	//If we cannot find the child return NULL
	return NULL;
}

//Argument/stack handling
static void push_args (const char *argtoks[], int argc, void **esp)
{
	//Make sure the argument count given is above zero
	ASSERT(argc > 0);

	//Declare variables
	int i = 0;
	int length = 0;
	void* argvs[argc];

	//Loop through the array of arguments and decrement the stack pointer
	//also adding the pointer locations of each to the argv array
	for (i = 0; i < argc; i++)
	{
		//set length to the length of argument i
		length = strlen(argtoks[i]) + 1;

		//decrement stack pointer by length
		*esp -= length;

		memcpy(*esp, argtoks[i], length);

		//add the pointer location to the argv array
		argvs[i] = *esp;
	}

	//Loop through the argv array backwards and set the stack pointer to the
	//address held in the array
	for (i = argc - 1; i >= 0; i--)
	{
		//Set stack pointer to start of next chunk
		*esp -= 4;

		//Set esp to address of argument held in argvs array
		*((void**) *esp) = argvs[i];
	}

	//Adding argument count to the stack
	//Go to next memory chunk
	*esp -= 4;
	//Set the value at stack pointer to argc
	*((int*) *esp) = argc;

	//Adding return address to stack
	//Go to next chunk
	*esp -= 4;
	//Set the value at the stack pointer to 0 to indicate return
	*((int*) *esp) = 0;
}

