/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/* Add a free object address to a cache pool's free list */
int enlist_kmem_free_obj(struct kcache_pool_struct *pool, addr_t obj_addr)
{
  struct vm_rg_struct *node = malloc(sizeof(struct vm_rg_struct));
  if (!node)
    return -1;

  node->rg_start = obj_addr;
  node->rg_end = obj_addr + pool->size;
  node->rg_next = pool->free_obj_list;
  pool->free_obj_list = node;

  return 0;
}

/* Get a free object from a cache pool's free list */
int get_free_obj_from_pool(struct kcache_pool_struct *pool, addr_t *retaddr)
{
  if (pool->free_obj_list == NULL)
    return -1;

  struct vm_rg_struct *node = pool->free_obj_list;
  *retaddr = node->rg_start;
  pool->free_obj_list = node->rg_next;
  free(node);

  return 0;
}

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  // int inc_sz = 0;
  if (cur_vma == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->krnl->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->krnl->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;

    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  int old_sbrk = cur_vma->sbrk;

  if (old_sbrk + size > cur_vma->vm_end)
  {
    struct sc_regs regs;
    regs.a1 = SYSMEM_INC_OP;
    regs.a2 = vmaid;
    regs.a3 = size;

    int ret = _syscall(caller->krnl, caller->pid, 17, &regs);

    if (ret != 0)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
  }

  /*Successful increase limit */
  caller->krnl->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->krnl->mm->symrgtbl[rgid].rg_end = old_sbrk + size;

  cur_vma->sbrk = old_sbrk + size;

  *alloc_addr = old_sbrk;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->krnl->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->krnl->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    return -1;
  }
  printf("%s:%d\n", __func__, __LINE__);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return 0; // val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  uint32_t pte = pte_get_entry(caller, pgn);
  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    addr_t tgtfpn;

    /* CASE A: Check if MEMRAM has free frames */
    if (MEMPHY_get_freefp(caller->krnl->mram, &tgtfpn) == 0)
    {
      /* RAM still has free space - just swap in without replacement */
      addr_t swpfpn = PAGING_SWP(pte); /* Get swap offset from current PTE */

      /* Copy target page from MEMSWP to MEMRAM */
      __swap_cp_page(caller->krnl->active_mswp, swpfpn, caller->krnl->mram, tgtfpn);

      /* Update target page PTE - mark as present in MEMRAM with frame number */
      pte_set_fpn(caller, pgn, tgtfpn);

      /* Add target page to FIFO list for future victim selection */
      enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
    }
    else
    {
      /* CASE B: MEMRAM is full - need page replacement */
      addr_t vicpgn, swpfpn, vicfpn;
      uint32_t vicpte;

      /* Find victim page using FIFO */
      if (find_victim_page(caller->krnl->mm, &vicpgn) == -1)
      {
        return -1;
      }

      /* Get victim page PTE and extract its frame number */
      vicpte = pte_get_entry(caller, vicpgn);
      vicfpn = PAGING_FPN(vicpte);

      /* Get free frame in MEMSWP for victim page */
      if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
      {
        return -1;
      }

      /* Copy victim frame from MEMRAM to MEMSWP */
      __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);

      /* Update victim page PTE - mark as swapped (swap type = 1) */
      pte_set_swap(caller, vicpgn, 1, swpfpn);

      /* Now the victim's frame in MEMRAM is free, reuse it for target page */
      tgtfpn = vicfpn;

      /* Get swap offset of target page (already in MEMSWP from previous access) */
      swpfpn = PAGING_SWP(pte_get_entry(caller, pgn));

      /* Copy target page from MEMSWP to MEMRAM (using freed frame) */
      __swap_cp_page(caller->krnl->active_mswp, swpfpn, caller->krnl->mram, tgtfpn);

      /* Update target page PTE - mark as present in MEMRAM with frame number */
      pte_set_fpn(caller, pgn, tgtfpn);

      /* Add target page to FIFO list for future victim selection */
      enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
    }
  }
  *fpn = PAGING_FPN(pte_get_entry(caller, pgn));

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  if (mm == NULL || caller == NULL || data == NULL)
    return -1;

  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  addr_t phyaddr = ((addr_t)fpn << PAGING_ADDR_FPN_LOBIT) + off;
  if (MEMPHY_read(caller->krnl->mram, phyaddr, data) != 0)
    return -1;

  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  if (mm == NULL || caller == NULL)
    return -1;

  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  addr_t phyaddr = ((addr_t)fpn << PAGING_ADDR_FPN_LOBIT) + off;
  if (MEMPHY_write(caller->krnl->mram, phyaddr, value) != 0)
    return -1;

  uint32_t pte = pte_get_entry(caller, pgn);
  SETBIT(pte, PAGING_PTE_DIRTY_MASK);
  if (pte_set_entry(caller, pgn, pte) != 0)
    return -1;

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  if (currg == NULL || (currg->rg_start == 0 && currg->rg_end == 0))
  {
    return -1;
  }

  if (currg->rg_start + offset >= currg->rg_end)
  {
    return -1;
  }

  int val = pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller);

  return val;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,      // Source address = [source] + [offset]
    uint32_t *destination)
{
  BYTE data;
  printf("%s:%d\n", __func__, __LINE__);
  int val = __read(proc, 0, source, offset, &data);

  *destination = data;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}

/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t *caller, uint32_t size, uint32_t reg_index)
{
  addr_t addr;
  /* passing vmaid = -1 which then pass it to get_vma_by_num() will return NULL and fail*/
  // int val = __kmalloc(caller, -1, reg_index, size, &addr);
  int val = __kmalloc(caller, 0, reg_index, size, &addr);
  if (val != 0)
  {
    return -1;
  }

  return 0;
}

/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  struct krnl_t *krnl = caller->krnl;
  struct mm_struct *kmm = krnl->mm;

  /* Calculate number of contiguous frames needed */
  int num_frames = (size + PAGING_PAGESZ - 1) / PAGING_PAGESZ;

  /* Get contiguous physical frames — already implemented in mm-memphy.c */
  addr_t first_fpn;
  if (MEMPHY_get_contiguous_freefp(krnl->mram, num_frames, &first_fpn) != 0)
    return -1;

  /* Use sbrk of vma0 as the virtual base for this allocation */
  struct vm_area_struct *cur_vma = get_vma_by_num(kmm, 0);
  if (!cur_vma)
    return -1;

  addr_t vaddr = cur_vma->sbrk;

  /* Extend VMA if needed */
  if (vaddr + size > cur_vma->vm_end)
    cur_vma->vm_end += PAGING_PAGE_ALIGNSZ(size);

  /* Wire each contiguous frame into the page table
   * using pte_set_fpn() — already implemented in mm64.c */
  for (int i = 0; i < num_frames; i++)
  {
    addr_t page_vaddr = vaddr + (addr_t)i * PAGING_PAGESZ;
    addr_t fpn = first_fpn + i;

    if (pte_set_fpn(caller, PAGING_PGN(page_vaddr), fpn) != 0)
      return -1;
  }

  /* Record in symbol table — same pattern as __alloc() */
  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ)
  {
    kmm->symrgtbl[rgid].rg_start = vaddr;
    kmm->symrgtbl[rgid].rg_end = vaddr + size;
  }

  cur_vma->sbrk = vaddr + PAGING_PAGE_ALIGNSZ(size);
  *alloc_addr = vaddr;

  return 0;
}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
  struct mm_struct *kmm = caller->krnl->mm;

  uint32_t num_objs = 1024;
  uint32_t total_size = size * num_objs;

  addr_t alloc_addr;
  /* passes vmaid = -1, which will return null and fail*/
  // if (__kmalloc(caller, -1, cache_pool_id, total_size, &alloc_addr) != 0)
  if (__kmalloc(caller, 0, cache_pool_id, total_size, &alloc_addr) != 0)
  {
    return -1;
  }

  kmm->kcpooltbl[cache_pool_id].size = size;
  kmm->kcpooltbl[cache_pool_id].align = align;
  kmm->kcpooltbl[cache_pool_id].storage = alloc_addr;

  addr_t current_obj = alloc_addr;
  for (uint32_t i = 0; i < num_objs; i++)
  {
    enlist_kmem_free_obj(&kmm->kcpooltbl[cache_pool_id], current_obj);

    current_obj += size;
  }

#ifdef MM_PAGING
  // update_kernel_page_table(caller->krnl, alloc_addr, total_size);
#endif

  return 0;
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
  addr_t addr;

  int val = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr);

  if (val != 0)
  {
    return -1;
  }

  struct mm_struct *kmm = proc->krnl->mm;
  uint32_t obj_size = kmm->kcpooltbl[cache_pool_id].size;

  kmm->symrgtbl[reg_index].rg_start = addr;
  kmm->symrgtbl[reg_index].rg_end = addr + obj_size;

  return 0;
}

/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{
  struct krnl_t *krnl = caller->krnl;
  struct mm_struct *kmm = krnl->mm;

  if (cache_pool_id < 0 || cache_pool_id >= PAGING_MAX_SYMTBL_SZ)
  {
    return -1;
  }

  struct kcache_pool_struct *pool = &kmm->kcpooltbl[cache_pool_id];

  if (pool->storage == 0)
  {
    return -1;
  }

  addr_t free_obj_addr = 0;
  if (get_free_obj_from_pool(pool, &free_obj_addr) != 0)
  {
    return -1;
  }

  kmm->symrgtbl[rgid].rg_start = free_obj_addr;
  kmm->symrgtbl[rgid].rg_end = free_obj_addr + pool->size;

  *alloc_addr = free_obj_addr;
#ifdef MM_PAGING
  // update_kernel_page_table(krnl, free_obj_addr, pool->size);
#endif

  return 0;
}

int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  BYTE data;
  for (uint32_t i = 0; i < size; i++)
  {
    if (__read_user_mem(caller, -1, source, offset + i, &data) != 0)
      return -1;

    if (__write_kernel_mem(caller, -1, destination, i, data) != 0)
      return -1;
  }
  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  BYTE data_byte;
  int res;

  for (uint32_t i = 0; i < size; i++)
  {
    res = __read_kernel_mem(caller, -1, source, i, &data_byte);
    if (res != 0)
      return -1;

    res = __write_user_mem(caller, -1, destination, offset + i, data_byte);
    if (res != 0)
      return -1;
  }

  return 0;

  return 1;
}

/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  struct mm_struct *kmm = caller->krnl->mm;
  struct vm_rg_struct *currg = get_symrg_byid(kmm, rgid);

  if (!currg)
    return -1;

  return pg_getval(kmm, currg->rg_start + offset, data, caller);
}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  struct mm_struct *kmm = caller->krnl->mm;
  struct vm_rg_struct *currg = get_symrg_byid(kmm, rgid);

  if (!currg)
    return -1;

  return pg_setval(kmm, currg->rg_start + offset, value, caller);
}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  struct mm_struct *kmm = caller->krnl->mm;
  struct vm_rg_struct *currg = get_symrg_byid(kmm, rgid);

  if (!currg)
    return -1;

  return pg_getval(kmm, currg->rg_start + offset, data, caller);
}

/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  struct krnl_t *krnl = caller->krnl;
  struct mm_struct *kmm = krnl->mm;

  struct vm_rg_struct *currg = get_symrg_byid(kmm, rgid);

  if (currg == NULL)
  {
    return -1;
  }

  if (currg->rg_start + offset >= currg->rg_end)
  {
    return -1;
  }

  addr_t v_addr = currg->rg_start + offset;
  int res = pg_setval(kmm, v_addr, value, caller);

  return res;
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->krnl->mm->pgd[pagenum];

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
  {
    return -1;
  }
  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  prev->pg_next = NULL;

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  if (newrg == NULL)
    return -1;

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_vma == NULL)
    return -1;

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

// #endif
