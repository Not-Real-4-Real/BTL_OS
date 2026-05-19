/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#if defined(MM64)

/* Number of entries per paging level in the 64-bit MMU model */
#define MM64_PGTBL_ENTRIES (1 << 9)

/*
 * mm64_get_pte_ptr - Find or create the target PTE for a given page number
 *
 * When @create is non-zero, missing intermediate paging levels are allocated
 * on demand and linked into the tree.
 */
static addr_t *mm64_get_pte_ptr(struct pcb_t *caller, addr_t pgn, int create)
{
  struct krnl_t *krnl = caller->krnl;
  addr_t i_pgd = 0, i_p4d = 0, i_pud = 0, i_pmd = 0, i_pt = 0;

  get_pd_from_pagenum(pgn, &i_pgd, &i_p4d, &i_pud, &i_pmd, &i_pt);

  if (krnl == NULL || krnl->mm == NULL || krnl->mm->pgd == NULL)
    return NULL;

  addr_t *p4d_tbl = (addr_t *)krnl->mm->pgd[i_pgd];
  if (p4d_tbl == NULL)
  {
    if (!create)
      return NULL;
    p4d_tbl = calloc(MM64_PGTBL_ENTRIES, sizeof(addr_t));
    if (p4d_tbl == NULL)
      return NULL;
    krnl->mm->pgd[i_pgd] = (addr_t)p4d_tbl;
  }

  addr_t *pud_tbl = (addr_t *)p4d_tbl[i_p4d];
  if (pud_tbl == NULL)
  {
    if (!create)
      return NULL;
    pud_tbl = calloc(MM64_PGTBL_ENTRIES, sizeof(addr_t));
    if (pud_tbl == NULL)
      return NULL;
    p4d_tbl[i_p4d] = (addr_t)pud_tbl;
  }

  addr_t *pmd_tbl = (addr_t *)pud_tbl[i_pud];
  if (pmd_tbl == NULL)
  {
    if (!create)
      return NULL;
    pmd_tbl = calloc(MM64_PGTBL_ENTRIES, sizeof(addr_t));
    if (pmd_tbl == NULL)
      return NULL;
    pud_tbl[i_pud] = (addr_t)pmd_tbl;
  }

  addr_t *pt_tbl = (addr_t *)pmd_tbl[i_pmd];
  if (pt_tbl == NULL)
  {
    if (!create)
      return NULL;
    pt_tbl = calloc(MM64_PGTBL_ENTRIES, sizeof(addr_t));
    if (pt_tbl == NULL)
      return NULL;
    pmd_tbl[i_pmd] = (addr_t)pt_tbl;
  }

  return &pt_tbl[i_pt];
}

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,       // present
             addr_t fpn,    // FPN
             int drt,       // dirty
             int swp,       // swap
             int swptyp,    // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0)
  {
    if (swp == 0)
    { // Non swap ~ page online
      if (fpn == 0)
        return -1; // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}

/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table
 */
int get_pd_from_address(addr_t addr, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt)
{
  /* Extract page direactories */
  *pgd = (addr & PAGING64_ADDR_PGD_MASK) >> PAGING64_ADDR_PGD_LOBIT;
  *p4d = (addr & PAGING64_ADDR_P4D_MASK) >> PAGING64_ADDR_P4D_LOBIT;
  *pud = (addr & PAGING64_ADDR_PUD_MASK) >> PAGING64_ADDR_PUD_LOBIT;
  *pmd = (addr & PAGING64_ADDR_PMD_MASK) >> PAGING64_ADDR_PMD_LOBIT;
  *pt = (addr & PAGING64_ADDR_PT_MASK) >> PAGING64_ADDR_PT_LOBIT;

  /* Page directory indices are already extracted above. */

  return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table
 */
int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt)
{
  /* Shift the address to get page num and perform the mapping*/
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                             pgd, p4d, pud, pmd, pt);
}

/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  // struct krnl_t *krnl = caller->krnl;
  addr_t *pte_ptr = NULL;

#ifdef MM64
  pte_ptr = mm64_get_pte_ptr(caller, pgn, 1);
#else
  pte_ptr = &krnl->mm->pgd[pgn];
#endif

  if (pte_ptr == NULL)
    return -1;

  CLRBIT(*pte_ptr, PAGING_PTE_PRESENT_MASK);

  SETBIT(*pte_ptr, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte_ptr, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte_ptr, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  // struct krnl_t *krnl = caller->krnl;
  addr_t *pte = NULL;

#ifdef MM64
  /* Perform multi-level page mapping */
  pte = mm64_get_pte_ptr(caller, pgn, 1);
#else
  pte = &krnl->mm->pgd[pgn];
#endif

  if (pte == NULL)
    return -1;

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}

/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  uint32_t pte = 0;

#ifdef MM64
  addr_t *pte_ptr = mm64_get_pte_ptr(caller, pgn, 0);
  if (pte_ptr != NULL)
    pte = (uint32_t)(*pte_ptr);
#else
  pte = (uint32_t)caller->krnl->mm->pgd[pgn];
#endif

  return pte;
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
  // struct krnl_t *krnl = caller->krnl;

#ifdef MM64
  addr_t *pte_ptr = mm64_get_pte_ptr(caller, pgn, 1);
  if (pte_ptr == NULL)
    return -1;
  *pte_ptr = (addr_t)pte_val;
#else
  krnl->mm->pgd[pgn] = pte_val;
#endif

  return 0;
}

/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller, // process call
                    addr_t addr,          // start address which is aligned to pagesz
                    int pgnum)            // num of mapping page
{
  int pgit = 0;
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;

  for (pgit = 0; pgit < pgnum; pgit++)
  {
    pte_set_entry(caller, pgn + pgit, 0);
  }

  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                       addr_t addr,                    // start address which is aligned to pagesz
                       int pgnum,                      // num of mapping page
                       struct framephy_struct *frames, // list of the mapped frames
                       struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                      // no guarantee all given pages are mapped
  struct framephy_struct *fpit = frames;
  int pgit = 0;
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;

  /* Update the returned region */
  if (ret_rg != NULL)
  {
    ret_rg->rg_start = addr;
    ret_rg->rg_end = addr + (addr_t)pgnum * PAGING_PAGESZ;
#ifdef MM64
    ret_rg->vmaid = 0;
#endif
  }

  /* Map the frames to the address space */
  for (pgit = 0; pgit < pgnum && fpit != NULL; pgit++, fpit = fpit->fp_next)
  {
    if (pte_set_fpn(caller, pgn + pgit, fpit->fpn) != 0)
      return (addr_t)-1;

    /* Tracking for later page replacement activities (if needed) */
    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn + pgit);
  }

  return addr;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  addr_t fpn = 0;
  int pgit = 0;
  struct framephy_struct *newfp_str = NULL;
  struct framephy_struct *tail = NULL;

  if (frm_lst == NULL)
    return -1;

  *frm_lst = NULL;

  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    newfp_str = malloc(sizeof(struct framephy_struct));
    if (newfp_str == NULL)
      return -1;

    newfp_str->fp_next = NULL;

    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) == 0)
    {
      newfp_str->fpn = fpn;
      if (*frm_lst == NULL)
      {
        *frm_lst = newfp_str;
        tail = newfp_str;
      }
      else
      {
        tail->fp_next = newfp_str;
        tail = newfp_str;
      }
    }
    else
    {
      free(newfp_str);
      return -3000;
    }
  }

  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;
  // int pgnum = incpgnum;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  // ret_alloc = alloc_pages_range(caller, pgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  /* Out of memory */
  if (ret_alloc == -3000)
  {
    return -1;
  }

  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  /* Initialize page table directory */
  mm->pgd = calloc(1 << 9, sizeof(addr_t));
  mm->p4d = calloc(1 << 9, sizeof(addr_t));
  mm->pud = calloc(1 << 9, sizeof(addr_t));
  mm->pmd = calloc(1 << 9, sizeof(addr_t));
  mm->pt = calloc(1 << 9, sizeof(addr_t));

  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  vma0->vm_next = NULL;

  vma0->vm_mm = mm;

  mm->mmap = vma0;

  mm->fifo_pgn = NULL;

  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rg = malloc(sizeof(struct vm_rg_struct));
  rg->rg_start = rg_start;
  rg->rg_end = rg_end;
  rg->rg_next = NULL;
  return rg;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  if (*rglist == NULL)
  {
    *rglist = rgnode;
  }
  else
  {
    struct vm_rg_struct *cur = *rglist;
    while (cur->rg_next != NULL)
    {
      cur = cur->rg_next;
    }
    cur->rg_next = rgnode;
  }
  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL)
  {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL)
  {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL)
  {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL)
  {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  // addr_t pgn_start;//, pgn_end;
  // addr_t pgit;
  struct krnl_t *krnl = caller->krnl;

  addr_t pgd = 0;
  addr_t p4d = 0;
  addr_t pud = 0;
  addr_t pmd = 0;
  addr_t pt = 0;

  get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt);

  /* Print page table hierarchy addresses */
  printf("print_pgtbl:\n");
  printf(" PDG=%p P4g=%p PUD=%p PMD=%p\n",
         (void *)krnl->mm->pgd,
         (void *)krnl->mm->p4d,
         (void *)krnl->mm->pud,
         (void *)krnl->mm->pmd);

  return 0;
}

#endif // def MM64
