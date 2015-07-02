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

#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#endif

static thread_func start_process NO_RETURN;
static bool load(const char *cmdline, void (**eip)(void), void **esp);

static struct semaphore lock_sema;

/*! Starts a new thread running a user program loaded from FILENAME.  The new
    thread may be scheduled (and may even exit) before process_execute()
    returns.  Returns the new process's thread id, or TID_ERROR if the thread
    cannot be created. */
//解析命令行参数，获取加载文件名称。
tid_t process_execute(const char *file_name) {
    char *fn_copy;
    char *fn_copy2;
    struct thread *curr = thread_current();
    struct thread *child_t;
    struct child_thread *child = palloc_get_page(0);
    tid_t tid;

    /* Make a copy of FILE_NAME.复制文件名
       Otherwise there's a race between the caller and load(). */
    fn_copy = palloc_get_page(0);
    if (fn_copy == NULL)
        return TID_ERROR;
    strlcpy(fn_copy, file_name, PGSIZE);
    
    /* Make a copy of FILE_NAME.
       Otherwise there's a race between the caller and load(). */
    fn_copy2 = palloc_get_page(0);
    if (fn_copy2 == NULL)
        return TID_ERROR;
    strlcpy(fn_copy2, file_name, PGSIZE);

    char *unused;
    char *pname = strtok_r(fn_copy2, " ", &unused);
    
    sema_init(&lock_sema, 1);
    sema_down(&lock_sema);

    /* Create a new thread to execute FILE_NAME. 
    用复制的文件名新建一个线程*/
    tid = thread_create(pname, PRI_DEFAULT, start_process, fn_copy);
    
    sema_down(&lock_sema);
    sema_up(&lock_sema);

    child_t = get_thread_from_tid(tid);
    thread_unblock(child_t);
    if (!child_t->load_success) {
      return -1;
    }
    
    struct file *f = filesys_open(pname);
    if (!f) return -1;
    file_deny_write(f);
    //设置新线程状态，并将新建的线程添加到它的父线程。    
    child->pid = tid;
    child->exited = false;
    child->waiting = false;
    child->exit_status = -1;
    list_push_back(&(curr->child_threads), &(child->elem));
    // Update child thread to know parent thread
    child_t->parent_pid = curr->tid;
    child_t->executable = f;
    
    palloc_free_page(fn_copy2);
    
    if (tid == TID_ERROR)
        palloc_free_page(fn_copy); 
    return tid;
}

/*! A thread function that loads a user process and starts it running. */
static void start_process(void *file_name_) {
    char *file_name = file_name_;
    struct intr_frame if_;
    bool success;

    /* Initialize interrupt frame and load executable. */
    memset(&if_, 0, sizeof(if_));
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;
    success = load(file_name, &if_.eip, &if_.esp);   //开始载入一个程序
    
    thread_current()->load_success = success;
    sema_up(&lock_sema);
    intr_disable ();
    thread_block ();
    intr_enable ();

    palloc_free_page(0);

    /* If load failed, quit. */
    palloc_free_page(file_name);
    if (!success) {
        thread_exit();
    }

    /* Start the user process by simulating a return from an
       interrupt, implemented by intr_exit (in
       threads/intr-stubs.S).  Because intr_exit takes all of its
       arguments on the stack in the form of a `struct intr_frame',
       we just point the stack pointer (%esp) to our stack frame
       and jump to it. */
    asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
    NOT_REACHED();
}

/*! Waits for thread TID to die and returns its exit status.  If it was
    terminated by the kernel (i.e. killed due to an exception), returns -1.
    If TID is invalid or if it was not a child of the calling process, or if
    process_wait() has already been successfully called for the given TID,
    returns -1 immediately, without waiting.

    This function will be implemented in problem 2-2.  For now, it does
    nothing. 
    等待程序运行结束或者它的exit状态。
*/
int process_wait(tid_t child_tid) {
  struct thread *curr = thread_current();
  struct list_elem *e = list_begin(&(curr->child_threads));
  struct child_thread *child;
  /* Check if the input tid is in the child list. */
  bool in_list = false;
  while (e != list_end(&(curr->child_threads))) {
    struct child_thread *ct = list_entry(e, struct child_thread, elem);
    if (ct->pid == child_tid) {
      child = ct;
      in_list = true;
      break;
    }
    e = list_next(e);
  }
  
  if (!in_list)
    return -1;

  /* Check if we're already waiting on the child. */
  if (child->waiting)
    return -1;
  /* Otherwise, we're now waiting on it. */
  else
    child->waiting = true;

  /* Check if child is done. */
  if (child->exited) {
    /* If it's done, remove it from the child list and return its exit status. */
    list_remove(&(child->elem));
    int retval = child->exit_status;
    palloc_free_page(child);
    return retval;
  }
  else {
    intr_disable ();
    thread_block();
    intr_enable ();
  }
  list_remove(&(child->elem));
  return child->exit_status;
}

/*! Free the current process's resources. */
void process_exit(void) {
    struct thread *cur = thread_current();
    uint32_t *pd;

    // Destroy the current process's supplemental page directory
    // and free its frames.
#ifdef VM
    vm_free_spt();
    vm_free_tid_frames(cur->tid);
#endif

    /* Destroy the current process's page directory and switch back
       to the kernel-only page directory. */
    pd = cur->pagedir;
    if (pd != NULL) {
        /* Correct ordering here is crucial.  We must set
           cur->pagedir to NULL before switching page directories,
           so that a timer interrupt can't switch back to the
           process page directory.  We must activate the base page
           directory before destroying the process's page
           directory, or our active page directory will be one
           that's been freed (and cleared). */
        cur->pagedir = NULL;
        pagedir_activate(NULL);
        pagedir_destroy(pd);
    }
}

/*! Sets up the CPU for running user code in the current thread.
    This function is called on every context switch. */
void process_activate(void) {
    struct thread *t = thread_current();

    /* Activate thread's page tables. */
    pagedir_activate(t->pagedir);

    /* Set thread's kernel stack for use in processing interrupts. */
    tss_update();
}

/*! We load ELF binaries.  The following definitions are taken
    from the ELF specification, [ELF1], more-or-less verbatim.  */

/*! ELF types.  See [ELF1] 1-2. @{ */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;
/*! @} */

/*! For use with ELF types in printf(). @{ */
#define PE32Wx PRIx32   /*!< Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /*!< Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /*!< Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /*!< Print Elf32_Half in hexadecimal. */
/*! @} */

/*! Executable header.  See [ELF1] 1-4 to 1-8.
    This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr {
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

/*! Program header.  See [ELF1] 2-2 to 2-4.  There are e_phnum of these,
    starting at file offset e_phoff (see [ELF1] 1-6). */
struct Elf32_Phdr {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};

/*! Values for p_type.  See [ELF1] 2-3. @{ */
#define PT_NULL    0            /*!< Ignore. */
#define PT_LOAD    1            /*!< Loadable segment. */
#define PT_DYNAMIC 2            /*!< Dynamic linking info. */
#define PT_INTERP  3            /*!< Name of dynamic loader. */
#define PT_NOTE    4            /*!< Auxiliary info. */
#define PT_SHLIB   5            /*!< Reserved. */
#define PT_PHDR    6            /*!< Program header table. */
#define PT_STACK   0x6474e551   /*!< Stack segment. */
/*! @} */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. @{ */
#define PF_X 1          /*!< Executable. */
#define PF_W 2          /*!< Writable. */
#define PF_R 4          /*!< Readable. */
/*! @} */

static bool setup_stack(void **esp);
static bool validate_segment(const struct Elf32_Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable);

/*! Loads an ELF executable from FILE_NAME into the current thread.  Stores the
    executable's entry point into *EIP and its initial stack pointer into *ESP.
    Returns true if successful, false otherwise.
载入一个ELF可执行文件，在EIP指针中存储程序进入指针，然后在指针ESP中初始化栈指针
 */
bool load(const char *file_name, void (**eip) (void), void **esp) {
    struct thread *t = thread_current();
    struct Elf32_Ehdr ehdr;
    struct file *file = NULL;
    off_t file_ofs;
    bool success = false;
    int i;

    /* Allocate and activate page directory. */
    t->pagedir = pagedir_create();
    if (t->pagedir == NULL) 
        goto done;
#ifdef VM
    // Also create the supplemental pagedir
    hash_init(&(t->supp_pagedir), spte_hash, spte_less, NULL);
#endif
    process_activate();

    /* Open executable file. */
    /* Hacky fix below... */
    /*打开可执行文件，复制文件名*/
    char *unused;
    char *fn_copy;
    fn_copy = palloc_get_page(0);
    if (fn_copy == NULL)
        return TID_ERROR;
    strlcpy(fn_copy, file_name, PGSIZE);
    char *name = strtok_r(fn_copy, " ", &unused);
    file = filesys_open(name);
    if (file == NULL) {
        printf("load: %s: open failed\n", name);
        goto done; 
    }
    palloc_free_page(fn_copy); 
    

    /* Read and verify executable header. 读取和判断可执行文件的头部*/
    if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr ||
        memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 ||
        ehdr.e_machine != 3 || ehdr.e_version != 1 ||
        ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024) {
        printf("load: %s: error loading executable\n", file_name);
        goto done; 
    }

    /* Read program headers. */
    file_ofs = ehdr.e_phoff;
    for (i = 0; i < ehdr.e_phnum; i++) {
        struct Elf32_Phdr phdr;

        if (file_ofs < 0 || file_ofs > file_length(file))
            goto done;
        file_seek(file, file_ofs);

        if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
            goto done;

        file_ofs += sizeof phdr;

        switch (phdr.p_type) {
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
            if (validate_segment(&phdr, file)) {
                bool writable = (phdr.p_flags & PF_W) != 0;
                uint32_t file_page = phdr.p_offset & ~PGMASK;
                uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
                uint32_t page_offset = phdr.p_vaddr & PGMASK;
                uint32_t read_bytes, zero_bytes;
                if (phdr.p_filesz > 0) {
                    /* Normal segment.
                       Read initial part from disk and zero the rest. */
                    read_bytes = page_offset + phdr.p_filesz;
                    zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) -
                                 read_bytes);
                }
                else {
                    /* Entirely zero.
                       Don't read anything from disk. */
                    read_bytes = 0;
                    zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
                }
                if (!load_segment(file, file_page, (void *) mem_page,
                                  read_bytes, zero_bytes, writable))
                    goto done;
            }
            else {
                goto done;
            }
            break;
        }
    }

    /* Set up stack. */
    if (!setup_stack(esp))
        goto done;
    
    /* Set up the stack. 读取参数*/
    int argc = 0;     // Number of arguments
    void **argv[256]; // Array of argument addresses on the stack
    int offsets[256]; // Array of offsets from the beginning of file_name
    int offset;
    char *sp;
    int len;

    fn_copy = palloc_get_page(0);
    if (fn_copy == NULL)
        return TID_ERROR;
    strlcpy(fn_copy, file_name, PGSIZE);
    /* Remove extra spaces. */
    bool space = false;
    int elem = 0;
    int shift = 0;
    while (fn_copy[elem] != '\0') {
        if (fn_copy[elem] == ' ' && space) {
            shift++;
        }
        else if (fn_copy[elem] == ' ' && !space) {
            space = true;
        }
        else if (space) {
            space = false;
        }
        fn_copy[elem] = fn_copy[elem + shift];
        if (fn_copy[elem] == '\0')
            break;
        elem++;
    }

    char *arg = strtok_r(fn_copy, " ", &sp);
    offsets[0] = 0;
    while (arg != NULL) {
        argc++;
        offset = sp - fn_copy;
        offsets[argc] = offset;
        arg = strtok_r(NULL, " ", &sp);
    }
    for (i = argc-1; i >= 0; i--) {
        len = offsets[i+1] - offsets[i] - 1;
        if (i == argc-1)
            len += 1;
        *esp -= (len + 1);
        argv[i] = *esp;
        memcpy(*esp, fn_copy + offsets[i], len);
        memset(*esp + len, '\0', 1);
    }
    palloc_free_page(fn_copy);
    /* Word-align esp. */
    if (((int) *esp) % 4 != 0) {
        *esp -= (((uint32_t) *esp) % 4);
    }
    /* argv[argc] is set to 0. */
    *esp -= sizeof(char *);
    memset(*esp, 0, sizeof(char *));
    for (i = argc-1; i >= 0; i--) {
        /* argv[i] is set to its address on the stack. */
        *esp -= sizeof(char *);
        *(int *)*esp = argv[i];
    }
    /* Push argv, then argc. */
    *esp -= sizeof(char **);
    *(int *)*esp = *esp + sizeof(char *);
    *esp -= sizeof(int);
    *(int *)*esp = argc;
    /* Push fake return address. */
    *esp -= sizeof(void(*)());
    memset(*esp, 0, sizeof(void(*)()));

    /* Start address. */
    *eip = (void (*)(void)) ehdr.e_entry;

    success = true;

done:
    /* We arrive here whether the load is successful or not. */
    file_close(file);  //关闭文件
    return success;
}

/* load() helpers. */

static bool install_page(void *upage, void *kpage, bool writable);

/*! Checks whether PHDR describes a valid, loadable segment in
    FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Elf32_Phdr *phdr, struct file *file) {
    /* p_offset and p_vaddr must have the same page offset. */
    if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
        return false; 

    /* p_offset must point within FILE. */
    if (phdr->p_offset > (Elf32_Off) file_length(file))
        return false;

    /* p_memsz must be at least as big as p_filesz. */
    if (phdr->p_memsz < phdr->p_filesz)
        return false; 

    /* The segment must not be empty. */
    if (phdr->p_memsz == 0)
        return false;
  
    /* The virtual memory region must both start and end within the
       user address space range. */
    if (!is_user_vaddr((void *) phdr->p_vaddr))
        return false;
    if (!is_user_vaddr((void *) (phdr->p_vaddr + phdr->p_memsz)))
        return false;

    /* The region cannot "wrap around" across the kernel virtual
       address space. */
    if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
        return false;

    /* Disallow mapping page 0.
       Not only is it a bad idea to map page 0, but if we allowed it then user
       code that passed a null pointer to system calls could quite likely panic
       the kernel by way of null pointer assertions in memcpy(), etc. */
    if (phdr->p_vaddr < PGSIZE)
        return false;

    /* It's okay. */
    return true;
}

/*! Loads a segment starting at offset OFS in FILE at address UPAGE.  In total,
    READ_BYTES + ZERO_BYTES bytes of virtual memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

    The pages initialized by this function must be writable by the user process
    if WRITABLE is true, read-only otherwise.

    Return true if successful, false if a memory allocation error or disk read
    error occurs. */
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable) {
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(upage) == 0);
    ASSERT(ofs % PGSIZE == 0);

#ifdef VM
    while (read_bytes > 0 || zero_bytes > 0) {
        /* Calculate how to fill this page.
           We will read PAGE_READ_BYTES bytes from FILE
           and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* Load this page. */
        if(!vm_install_fs_spte(upage, file, ofs, page_read_bytes, page_zero_bytes, 
                              writable)) {
          return false;
        }

        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        ofs += PGSIZE;
        upage += PGSIZE;
    }
#else
    file_seek(file, ofs);
    while (read_bytes > 0 || zero_bytes > 0) {
        /* Calculate how to fill this page.
We will read PAGE_READ_BYTES bytes from FILE
and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* Get a page of memory. */
        uint8_t *kpage = palloc_get_page(PAL_USER);
        if (kpage == NULL)
            return false;

        /* Load this page. */
        if (file_read(file, kpage, page_read_bytes) != (int) page_read_bytes) {
            palloc_free_page(kpage);
            return false;
        }
        memset(kpage + page_read_bytes, 0, page_zero_bytes);

        /* Add the page to the process's address space. */
        if (!install_page(upage, kpage, writable)) {
            palloc_free_page(kpage);
            return false;
        }

        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        upage += PGSIZE;
    }
#endif

  return true;
}

/*! Create a minimal stack by mapping a zeroed page at the top of
    user virtual memory. 在用户的虚拟内存中创建一个栈*/
static bool setup_stack(void **esp) {
    uint8_t *kpage;
    bool success = false;

    uint8_t *upage;
    upage = ((uint8_t *) PHYS_BASE) - PGSIZE;
#ifdef VM
    kpage = vm_frame_alloc(PAL_USER | PAL_ZERO, upage);
#else
    kpage = palloc_get_page(PAL_USER | PAL_ZERO);
#endif
    if (kpage != NULL) {
        success = install_page(upage, kpage, true);
#ifdef VM
        success &= vm_install_swap_spte(upage, 0, true);
        vm_frame_set_done(kpage, true);
#endif
        if (success)
            *esp = PHYS_BASE;
        else
#ifdef VM
            vm_free_frame(kpage);
#else
            palloc_free_page(kpage);
#endif
    }
    return success;
}

/*! Adds a mapping from user virtual address UPAGE to kernel
    virtual address KPAGE to the page table.
    If WRITABLE is true, the user process may modify the page;
    otherwise, it is read-only.
    UPAGE must not already be mapped.
    KPAGE should probably be a page obtained from the user pool
    with palloc_get_page().
    Returns true on success, false if UPAGE is already mapped or
    if memory allocation fails. */
static bool install_page(void *upage, void *kpage, bool writable) {
    struct thread *t = thread_current();

    /* Verify that there's not already a page at that virtual
       address, then map our page there. */
    return (pagedir_get_page(t->pagedir, upage) == NULL &&
            pagedir_set_page(t->pagedir, upage, kpage, writable));
}
