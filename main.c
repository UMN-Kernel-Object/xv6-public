#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void startothers(void);
static void mpmain(void)  __attribute__((noreturn));
extern pde_t *kpgdir;
extern char end[]; // first address after kernel loaded from ELF file

// Bootstrap processor starts running C code here.
// Allocate a real stack and switch to it, first
// doing some setup required for memory allocator to work.
int
main(void)
{
  /*
   * Breaks physcial memory into PAGESIZE (4096) chunks and adds each chunk
   * to the kmem (kernel memory) structure.
   */
  kinit1(end, P2V(4*1024*1024)); // phys page allocator

  /*
   * Looks at previously allocated pages and creates the kernel's virtual
   * memory space.
   */
  kvmalloc();      // kernel page table

  /*
   * Look at CPU topology and setup processor table.
   */
  mpinit();        // detect other processors

  /*
   * Set up interrupt scheduling and controller.
   */
  lapicinit();     // interrupt controller
  seginit();       // segment descriptors
  picinit();       // disable pic
  ioapicinit();    // another interrupt controller

  /*
   * Set up console i/o.
   */
  consoleinit();   // console hardware

  /*
   * Initialize the serial port with special vendor instructions.
   */
  uartinit();      // serial port

  /*
   * Set up lock for process table.
   */
  pinit();         // process table

  /*
   * Set up trap/interrupt events and vectors to handle hardware events and
   * system calls.
   */
  tvinit();        // trap vectors

  /*
   * Initializes a buffer array for use when you want fast access to an object
   * at a later time. This is basically a place to cache objects.
   */
  binit();         // buffer cache

  /*
   * Creates a lock for the file table of 100 files.
   */
  fileinit();      // file table

  /*
   * Initializes interrupts for disks (HDDs, SSDs).
   */
  ideinit();       // disk 

  /*
   * Up until this point, we've been running on 1 processor.
   * Start up other available processors that were found in mpinit().
   */
  startothers();   // start other processors

  /*
   * Set up page chunks... Not really sure why this is here.
   */
  kinit2(P2V(4*1024*1024), P2V(PHYSTOP)); // must come after startothers()

  /*
   * Start up init process. This is the parent of each user-space process in
   * the system.
   */
  userinit();      // first user process

  /*
   * Finish processor setup and go to scheduler.
   */
  mpmain();        // finish this processor's setup
}

// Other CPUs jump here from entryother.S.
static void
mpenter(void)
{
  switchkvm();
  seginit();
  lapicinit();
  mpmain();
}

// Common CPU setup code.
static void
mpmain(void)
{
  cprintf("cpu%d: starting %d\n", cpuid(), cpuid());
  idtinit();       // load idt register
  xchg(&(mycpu()->started), 1); // tell startothers() we're up
  scheduler();     // start running processes
}

pde_t entrypgdir[];  // For entry.S

// Start the non-boot (AP) processors.
static void
startothers(void)
{
  extern uchar _binary_entryother_start[], _binary_entryother_size[];
  uchar *code;
  struct cpu *c;
  char *stack;

  // Write entry code to unused memory at 0x7000.
  // The linker has placed the image of entryother.S in
  // _binary_entryother_start.
  code = P2V(0x7000);
  memmove(code, _binary_entryother_start, (uint)_binary_entryother_size);

  for(c = cpus; c < cpus+ncpu; c++){
    if(c == mycpu())  // We've started already.
      continue;

    // Tell entryother.S what stack to use, where to enter, and what
    // pgdir to use. We cannot use kpgdir yet, because the AP processor
    // is running in low  memory, so we use entrypgdir for the APs too.
    stack = kalloc();
    *(void**)(code-4) = stack + KSTACKSIZE;
    *(void(**)(void))(code-8) = mpenter;
    *(int**)(code-12) = (void *) V2P(entrypgdir);

    lapicstartap(c->apicid, V2P(code));

    // wait for cpu to finish mpmain()
    while(c->started == 0)
      ;
  }
}

// The boot page table used in entry.S and entryother.S.
// Page directories (and page tables) must start on page boundaries,
// hence the __aligned__ attribute.
// PTE_PS in a page directory entry enables 4Mbyte pages.

__attribute__((__aligned__(PGSIZE)))
pde_t entrypgdir[NPDENTRIES] = {
  // Map VA's [0, 4MB) to PA's [0, 4MB)
  [0] = (0) | PTE_P | PTE_W | PTE_PS,
  // Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
  [KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
};

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

