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

addr_t *get_pte_ptr(struct mm_struct *mm, addr_t vaddr, int alloc);

#if defined(MM64)

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

  /* TODO: implement the page direactories mapping */

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
  struct krnl_t *krnl = caller->krnl;
  addr_t *pte_ptr;

  addr_t i_pgd, i_p4d, i_pud, i_pmd, i_pt;

#ifdef MM64
  get_pd_from_pagenum(pgn, &i_pgd, &i_p4d, &i_pud, &i_pmd, &i_pt);

  addr_t *p4d_tbl = (addr_t *)krnl->mm->pgd[i_pgd];
  addr_t *pud_tbl = (addr_t *)p4d_tbl[i_p4d];
  addr_t *pmd_tbl = (addr_t *)pud_tbl[i_pud];
  addr_t *pt_tbl = (addr_t *)pmd_tbl[i_pmd];

  pte_ptr = &pt_tbl[i_pt];
#else
  pte_ptr = &krnl->mm->pgd[pgn];
#endif
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
  addr_t vaddr = pgn << PAGING64_ADDR_PT_SHIFT;
  addr_t *pte = get_pte_ptr(caller->krnl->mm, vaddr, 1); // 1 = alloc if not exist

  if (!pte)
    return -1;

  /* Write FPN to PTE */
  *pte = 0; // Reset
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
  addr_t vaddr = pgn << PAGING64_ADDR_PT_SHIFT;
  addr_t *pte = get_pte_ptr(caller->krnl->mm, vaddr, 0); // 0 = don't alloc

  return (pte != NULL) ? (uint32_t)*pte : 0;
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
  struct krnl_t *krnl = caller->krnl;
  krnl->mm->pgd[pgn] = pte_val;

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
  uint64_t pattern = 0xdeadbeef;

  /* TODO memset the page table with given pattern
   */
  for (pgit = 0; pgit < pgnum; pgit++)
  {
    addr_t cur_addr = addr + (addr_t)pgit * PAGING64_PAGESZ;

    /* Walk/allocate the 5-level page table tree,
     * then write the dummy pattern into the PTE.
     * alloc=1 so intermediate tables are created on demand. */
    addr_t *pte = get_pte_ptr(caller->krnl->mm, cur_addr, 1);

    if (pte == NULL)
      return -1;

    *pte = (addr_t)pattern;
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
                                                       // struct framephy_struct *fpit;
  // int pgit = 0;
  // addr_t pgn;

  /* TODO: update the rg_end and rg_start of ret_rg
  //ret_rg->rg_end =  ....
  //ret_rg->rg_start = ...
  //ret_rg->vmaid = ...
  */

  /* TODO map range of frame to address space
   *      [addr to addr + pgnum*PAGING_PAGESZ
   *      in page table caller->krnl->mm->pgd,
   *                    caller->krnl->mm->pud...
   *                    ...
   */

  /* Tracking for later page replacement activities (if needed)
   * Enqueue new usage page */
  // enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn64 + pgit);

  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  // addr_t fpn;
  // int pgit;
  // struct framephy_struct *newfp_str = NULL;

  /* TODO: allocate the page
  //caller-> ...
  //frm_lst-> ...
  */

  /*
    for (pgit = 0; pgit < req_pgnum; pgit++)
    {
      // TODO: allocate the page
      if (MEMPHY_get_freefp(caller->mram, &fpn) == 0)
      {
        newfp_str->fpn = fpn;
      }
      else
      { // TODO: ERROR CODE of obtaining somes but not enough frames
      }
    }
  */

  /* End TODO */

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

  /* TODO init page table directory */
  mm->pgd = calloc(1 << 9, sizeof(addr_t));
  // mm->p4d = ...
  // mm->pud = ...
  // mm->pmd = ...
  // mm->pt = ...

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
  addr_t vaddr;

  printf("----------------------------------------------------------\n");
  printf("Page table dump from " FORMAT_ADDR " to " FORMAT_ADDR "\n", start, end);

  // Duyệt qua từng trang (mỗi bước nhảy 1 PAGESZ)
  for (vaddr = start; vaddr < end; vaddr += PAGING64_PAGESZ)
  {
    /* Dùng helper để lấy PTE, alloc = 0 vì ta chỉ muốn xem, không muốn tạo thêm bảng */
    addr_t *pte = get_pte_ptr(caller->krnl->mm, vaddr, 0);

    if (pte != NULL && *pte != 0) // Nếu trang này đã từng được map
    {
      printf("VA: " FORMAT_ADDR " -> ", vaddr);

      // Kiểm tra bit Present (bit 31 hoặc theo định nghĩa mask của bạn)
      if (PAGING_PAGE_PRESENT(*pte))
      {
        addr_t fpn = (*pte & PAGING_PTE_FPN_MASK) >> PAGING_PTE_FPN_LOBIT;
        printf("PTE: %08x | [RAM] FPN: " FORMAT_ADDR "\n", (uint32_t)*pte, fpn);
      }
      else if (PAGING_PTE_SWP(*pte))
      {
        addr_t swpoff = (*pte & PAGING_PTE_SWPOFF_MASK) >> PAGING_PTE_SWPOFF_LOBIT;
        printf("PTE: %08x | [SWAP] Offset: " FORMAT_ADDR "\n", (uint32_t)*pte, swpoff);
      }
    }
  }
  printf("----------------------------------------------------------\n");

  return 0;
}
/* Helper */
addr_t *get_pte_ptr(struct mm_struct *mm, addr_t vaddr, int alloc)
{
  addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
  get_pd_from_address(vaddr, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  // 1. PGD -> P4D
  addr_t **p4d_table = (addr_t **)mm->pgd[pgd_idx];
  if (!p4d_table)
  {
    if (!alloc)
      return NULL;
    p4d_table = calloc(1 << 9, sizeof(addr_t *));
    mm->pgd[pgd_idx] = (addr_t)p4d_table;
  }

  // 2. P4D -> PUD
  addr_t **pud_table = (addr_t **)p4d_table[p4d_idx];
  if (!pud_table)
  {
    if (!alloc)
      return NULL;
    pud_table = calloc(512, sizeof(addr_t *));
    p4d_table[p4d_idx] = (addr_t *)pud_table;
  }

  // 3. PUD -> PMD
  addr_t **pmd_table = (addr_t **)pud_table[pud_idx];
  if (!pmd_table)
  {
    if (!alloc)
      return NULL;
    pmd_table = calloc(512, sizeof(addr_t *));
    pud_table[pud_idx] = (addr_t *)pmd_table;
  }

  // 4. PMD -> PT
  addr_t *pt_table = (addr_t *)pmd_table[pmd_idx];
  if (!pt_table)
  {
    if (!alloc)
      return NULL;
    pt_table = calloc(512, sizeof(addr_t));
    pmd_table[pmd_idx] = (addr_t *)pt_table;
  }

  return &pt_table[pt_idx];
}

#endif // def MM64
