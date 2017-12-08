#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

#include "devices/timer.h"

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

//#define P 17
//#define Q 14
#define F 16384

struct list child_info_list;

static int gl_load_avg;

//static struct list mlfqs_queues[PRI_MAX+1];

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */

void
thread_init (void) 
{
	//int i;
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

	if(thread_mlfqs){
		gl_load_avg = 0;
	}
  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

	if(thread_mlfqs){
		initial_thread->nice = 0;
		initial_thread->recent_cpu = 0;
	}

#ifdef USERPROG
	list_init(&child_info_list);
#endif
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);

}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;


  /* Enforce preemption. */
 	if (++thread_ticks >= TIME_SLICE)
  //++thread_ticks;
		intr_yield_on_return ();

	if (thread_mlfqs)
	{
		inc_recent_cpu();		// at each interrupt, inc recent_cpu 
		if(timer_ticks() % TIMER_FREQ == 0)
		{
			recalc_cpu();
			recalc_load();
		}
		if(timer_ticks() % 4 == 0)
		{
			recalc_pri();
		}
	}
}


void checkCurrentThreadPriority()
{	
//#ifdef USERPROG
//	thread_yield();
//#else
	if(!list_empty(&ready_list)){
		
		if(list_begin(&ready_list) != list_end(&ready_list)){
			enum intr_level old_level = intr_disable();
			struct thread *frontT = list_entry(list_front(&ready_list),struct thread,elem);
//#ifdef USERPROG
//			if(thread_get_priority() <= frontT->priority)
//#else 
			if(thread_get_priority() < frontT->priority)
//#endif
			{
				if(intr_context())
					intr_yield_on_return();
				else
					thread_yield();
			}
			intr_set_level(old_level);
		}
	}
//#endif
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
	{
    return TID_ERROR;
	}

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

	if(thread_mlfqs){
		t->recent_cpu = thread_current()->recent_cpu;
		t->nice = thread_current()->nice;
	}
  /* Add to run queue. */

	thread_unblock (t);
	
	checkCurrentThreadPriority();
	
	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */

	
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
	
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;
	
  ASSERT (is_thread (t));

  old_level = intr_disable ();
  
	ASSERT (t->status == THREAD_BLOCKED);

	list_insert_ordered(&ready_list,&t->elem,compare_pri,(void*)NULL);
	
	t->status = THREAD_READY;

	intr_set_level (old_level);

}



bool 
compare_pri(const struct list_elem *e1, const struct list_elem *e2, void *aux UNUSED)
{
	return list_entry(e1,struct thread, elem)->priority	
		   > list_entry(e2,struct thread, elem)->priority;
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
		process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it call schedule_tail(). */
  intr_disable ();
//#ifdef USERPROG
// 	struct thread* cur = thread_current();
//	printf("%s: exit(%d)\n",cur->name,getCIFromTid(cur->tid)->exitCode);
//#endif
	list_remove (&thread_current()->allelem);
	thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
	struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
	{
		list_insert_ordered(&ready_list,&cur->elem,compare_pri,(void*)NULL);
	}
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

void
inc_recent_cpu()
{
	struct thread *t = thread_current();	
	if(t != idle_thread)
			t->recent_cpu = addxn(t->recent_cpu,1);
}

void
recalc_pri()
{
	unsigned int i;
	if(!thread_mlfqs){
	for (i = 0; i < list_size(&all_list); i++){
		list_sort(&all_list,compare_pri,(void*)NULL);
		struct list_elem *ae = list_begin(&all_list);
		for (;ae != list_end(&all_list); ae = list_next(ae))
		{
			struct thread *t = list_entry(ae,struct thread,allelem);
			struct list *dl = &t->donate_list;
			int final_priority = t->priority;

			if(list_begin(dl) != list_end(dl))
			{
				struct list_elem *e;
				for (e = list_begin(dl); e != list_end(dl); e = list_next(e))
				{
		
				int max_donate_priority = list_entry(e,struct donate,elem)->donator->priority;
				if (max_donate_priority > final_priority)
					final_priority = max_donate_priority;
				}

			}
			t->priority = final_priority;
		}
	}
	} else {	// mlfqs
			struct list_elem *e = list_begin(&all_list);
			for(;e!=list_end(&all_list);e = list_next(e))
			{
				struct thread *t = list_entry(e,struct thread, allelem);

				if (t == idle_thread) continue;

				int oPriority = t->priority;
				int nPriority = mlfqs_calc_pri(t);
				if (oPriority != nPriority){
					t->priority = nPriority;
					list_sort(&ready_list,compare_pri,(void*)NULL);
				}
			}
			checkCurrentThreadPriority();
		}
}


void 
recalc_cpu(){
  struct list_elem *e = list_begin(&all_list);
	struct thread *t;
	for(;e != list_end(&all_list);e = list_next(e))
	{
		t = list_entry(e,struct thread, allelem);
		if(t != idle_thread)
		t->recent_cpu = addxn(mulxy(divxy(mulxy(con_ntof(2),gl_load_avg),addxn(mulxy(con_ntof(2),gl_load_avg),1)),t->recent_cpu),t->nice);
	}
}

int 
getReadyThread(){
	int num = list_size(&ready_list);
	
	if (thread_current() != idle_thread) return num+1;
	else return num;
}

void
recalc_load(){
	int readyNum = getReadyThread();
	int load_avg = addxy(mulxy(divxn(con_ntof(59),60),gl_load_avg),mulxn(divxn(con_ntof(1),60),readyNum));
	gl_load_avg = load_avg;
}

int
search_best_donator(struct list *dl)
{
	struct list_elem *begin = list_begin(dl);
	int max = PRI_MIN;
	int pri;
	
	if (list_begin(dl) != list_end(dl))	// not empty
	{
		struct list_elem *e;
		struct donate *de;
			
		for (e = begin; e != list_end(dl); e = list_next(e))
		{
			de = list_entry(e,struct donate,elem);
			
			pri = de->donator->priority;
		if( max < pri)
			max = pri;
		}
	}
	return max;
}


/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
	ASSERT(!thread_mlfqs);
	struct thread *cur = thread_current();
	struct list *dl = &cur->donate_list;
	
	if(list_begin(dl) != list_end(dl))
	{
		//int max_donate_priority = list_entry(list_front(al),struct donate,elem)->dPriority;
		int max_donate_priority = search_best_donator(dl);
		if (max_donate_priority > new_priority)
		{	
			cur->priority = max_donate_priority;				
			cur->oPriority = new_priority;	
		}	
	} else {
		cur->oPriority = cur->priority = new_priority;
	}
	checkCurrentThreadPriority();

}

int
mlfqs_calc_pri(struct thread* t)
{
	int mid = con_xton_near(divxn(t->recent_cpu,4));
	int back = (t->nice)*2;
	int pri = PRI_MAX - mid - back;
	return pri;
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
 	struct thread *cur = thread_current();

	if(!thread_mlfqs){
	struct list *dl = &cur->donate_list;
	
	int final_priority = cur->oPriority;

	if(list_begin(dl) != list_end(dl))
	{
		int max_donate_priority = search_best_donator(dl);
		if (max_donate_priority > final_priority)
		{
			final_priority = max_donate_priority;
		}
	}
	
	cur->priority = final_priority;
	return final_priority;
	}
	else {
		cur->priority = mlfqs_calc_pri(cur);
		return cur->priority;
	}
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
	struct thread *t = thread_current();
	ASSERT(thread_mlfqs);
	t->nice = nice;
	t->priority = mlfqs_calc_pri(t);
	checkCurrentThreadPriority();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
 	ASSERT(thread_mlfqs);
	return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  ASSERT(thread_mlfqs);

	return con_xton_near(mulxn(gl_load_avg,100));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
	ASSERT(thread_mlfqs);


	return con_xton_near(mulxn(thread_current()->recent_cpu,100));
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = t->oPriority = priority;		// modified
  t->magic = THREAD_MAGIC;
	list_init(&t->donate_list);
  list_insert_ordered (&all_list, &t->allelem,compare_pri,(void *)NULL);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
	if (list_empty (&ready_list))	return idle_thread;
 	else return list_entry(list_pop_front(&ready_list),struct thread,elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev) 
{
  struct thread *cur = running_thread ();
 
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until schedule_tail() has
   completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev); 
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

struct thread*
getThreadFromTid(tid_t tid)

{
	struct list_elem *e = list_begin(&all_list);
	struct thread *result = NULL;
	for(;e!=list_end(&all_list);e=list_next(e))
	{
		struct thread *t = list_entry(e,struct thread,allelem);
		if(tid == t->tid)
		{
			result = t;
			break;
		}
	}
	return result;
}

// -- fixed point functions
int con_ntof(int n)
{
	return n*F;
}

int con_xton_zero(int x)
{
	return x/F;
}

int con_xton_near(int x)
{
	if (x > 0) return (x+F/2)/F;
  else return (x-F/2)/F;

}

int addxy(int x, int y)
{
	return x+y;
}

int subxy(int x, int y)
{
	return x-y;
}

int addxn(int x, int n)
{
	return x+n*F;
}

int subxn(int x, int n)
{
	return x-n*F;
}

int mulxy(int x, int y)
{
	return ((int64_t)x)*y/F;
}

int mulxn(int x, int n)
{
	return x*n;
}

int divxy(int x, int y)
{
	return ((int64_t)x)*F/y;
}

int divxn(int x, int n)
{
	return x/n;
}

struct child_info*  
getCIFromTid(tid_t tid)
{
	struct list_elem *e;
	struct child_info *ci;
	for(e = list_begin(&child_info_list);e != list_end(&child_info_list);e = list_next(e))
	{
		ci = list_entry(e,struct child_info,elem);
		if (ci->tid == tid)
			return ci;
	}
	return NULL;
}

bool checkIsThread(char* filename)
{
	struct list_elem *e;
	struct thread *t;
	for(e = list_begin(&all_list);e != list_end(&all_list); e = list_next(e))
	{
		t = list_entry(e,struct thread,allelem);
		if (!strcmp(t->name,filename))
			return true;
	}
	return false;
}
