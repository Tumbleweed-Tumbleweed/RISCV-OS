// memory.c - Physical and virtual memory manager
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef MEMORY_TRACE
#define TRACE
#endif

#ifdef MEMORY_DEBUG
#define DEBUG
#endif

#include "memory.h"
#include "conf.h"
#include "board-conf.h"
#include "console.h"
#include "error.h"
#include "heap.h"
#include "misc.h"
#include "process.h"
#include "riscv.h"
#include "string.h"
#include "thread.h"

// ---COMPILE-TIME CONFIGURATION--- //

// Minimum amount of memory in the initial heap block.

#ifndef HEAP_INIT_MIN
#define HEAP_INIT_MIN 256
#endif

// ---INTERNAL CONSTANT DEFINITIONS--- //

#define PTE_ORDER 3
#define PTE_CNT (1U << (PAGE_ORDER - PTE_ORDER))

#ifndef PAGING_MODE
#define PAGING_MODE RISCV_SATP_MODE_Sv39
#endif

#ifndef ROOT_LEVEL
#define ROOT_LEVEL 2
#endif

// ---IMPORTED GLOBAL SYMBOLS--- //

// linker-provided (kernel.ld)
extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// ---EXPORTED GLOBAL VARIABLES--- //
char memory_initialized = 0;

// ---INTERNAL TYPE DEFINITIONS--- //

// We keep free physical pages in a linked list of _chunks_, where each chunk
// consists of several consecutive pages of memory. Initially, all free pages
// are in a single large chunk. To allocate a block of pages, we break up the
// smallest chunk on the list.

struct page_chunk {
    struct page_chunk * next;   // next page chunk in list
    unsigned long pagecnt;      // number of pages in chunk
};

struct pte {
    uint64_t flags : 8;         // 0-7
    uint64_t rsw : 2;           // 8-9
    uint64_t ppn : 44;          // 10-53
    uint64_t reserved : 7;      // 54-60
    uint64_t pbmt : 2;          // 61-62
    uint64_t n : 1;             // 63
};

// ---INTERNAL MACRO DEFINITIONS--- //
#define VPN(vma) ((vma) / PAGE_SIZE)
#define VPN2(vma) ((VPN(vma) >> (2 * 9)) % PTE_CNT)
#define VPN1(vma) ((VPN(vma) >> (1 * 9)) % PTE_CNT)
#define VPN0(vma) ((VPN(vma) >> (0 * 9)) % PTE_CNT)

// The following macros test is a PTE is valid, global, or a leaf. The argument
// is a struct pte (*not* a pointer to a struct pte).

#define PTE_VALID(pte) (((pte).flags & PTE_V) != 0)
#define PTE_GLOBAL(pte) (((pte).flags & PTE_G) != 0)
#define PTE_LEAF(pte) (((pte).flags & (PTE_R | PTE_W | PTE_X)) != 0)

#define PT_INDEX(lvl, vpn) \
    (((vpn) & (0x1FF << (lvl * (PAGE_ORDER - PTE_ORDER)))) >> (lvl * (PAGE_ORDER - PTE_ORDER)))

// ---CONSTANTS--//
#define CAT_SIZE 15
const char * art = 
" _\0"
" \\`*-.\0"
"  )  _`-.\0"
" .  : `. .\0"
" : _   '  \\ \0"
" ; *` _.   `*-._\0"
" `-.-'          `-.\0"
"   ;       `       `.\0"
"   :.       .        \\ \0"
"   . \\  .   :   .-'   .\0"
"   '  `+.;  ;  '      :\0"
"   :  '  |    ;       ;-.\0"
"   ; '   : :`-:     _.`* ;\0"
".*' /  .*' ; .*`- +'  `*'\0"
"`*-*   `*-*  `*-*'\0";


// ---INTERNAL FUNCTION DECLARATIONS--- //

static inline mtag_t active_space_mtag(void);
static inline mtag_t ptab_to_mtag(struct pte * root, unsigned int asid);
static inline struct pte * mtag_to_ptab(mtag_t mtag);
static inline struct pte * active_space_ptab(void);

static inline void * pageptr(uintptr_t n);
static inline uintptr_t pagenum(const void * p);
static inline int wellformed(uintptr_t vma);

static inline struct pte leaf_pte(const void * pp, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte(const struct pte * pt, uint_fast8_t g_flag);
static inline struct pte null_pte(void);

static int print_memory_info(struct mregion * mmio_imm, unsigned long mmiocnt,
                             struct mregion * ram_imm, unsigned long ramcnt,
                             struct mregion * resv_imm, unsigned long resvcnt);

// ---INTERNAL GLOBAL VARIABLES--- //

static mtag_t main_mtag;

static struct pte main_pt2[PTE_CNT] __attribute__((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt1_0x80000[PTE_CNT]
__attribute__((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt0_0x80000[PTE_CNT]
__attribute__((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt0_0x80001[PTE_CNT]
__attribute__((section(".bss.pagetable"), aligned(4096)));

static struct page_chunk * free_chunk_list;

// ---EXPORTED FUNCTION DECLARATIONS--- //

/*
INPUTS: const struct mregion * ram_og - pointer to RAM memory region
        unsigned long ramcnt - amount of RAM
        const struct mregion * mmio_og - pointer to Memory Mapped IO Region
        unsigned long mmiocnt - amoung of MEMIO
        const struct mregion * resv_og - pointer to reserved memory
        unsigned long resvcnt - amount of reserved memory
OUTPUTS: no outputs
DESCRIPTION: initalizeds kernel memory management, free chunk list and heap allocator
SIDE EFFECTS: sets memory initalized to 1, allows for mem allocation
*/
void memory_init (const struct mregion * ram_og, unsigned long ramcnt, const struct mregion * mmio_og, unsigned long mmiocnt, const struct mregion * resv_og, unsigned long resvcnt) {
    const void * const text_start = _kimg_text_start;
    const void * const text_end = _kimg_text_end;
    const void * const rodata_start = _kimg_rodata_start;
    const void * const rodata_end = _kimg_rodata_end;
    const void * const data_start = _kimg_data_start;

    // All parameters are used for running on real hardware and printing memory info; 
    // you can ignore them in this simplified memory_init() implementation

    struct mregion mmio[mmiocnt];
    struct mregion ram[ramcnt];
    struct mregion resv[resvcnt+1];

    memcpy(mmio, mmio_og, mmiocnt * sizeof(struct mregion));
    memcpy(ram, ram_og, ramcnt * sizeof(struct mregion));
    memcpy(resv, resv_og, resvcnt * sizeof(struct mregion));

    //The memory print function expects the kernel to be mapped as a reserved
    //region and the array to be sorted. For the qvirt target, the second 
    //reserved region is in high memory, so we move it right one and insert our
    //kernel as the second to last entry.
    resvcnt++;
    resv[resvcnt-1].pma = resv[resvcnt-2].pma;
    resv[resvcnt-1].size = resv[resvcnt-2].size;
    resv[resvcnt-2].pma = 
        ROUND_DOWN((uintptr_t)(void *)_kimg_start, PAGE_SIZE);
    resv[resvcnt-2].size = 
        ROUND_UP((uintptr_t)(void *)_kimg_end, PAGE_SIZE) - 
        ROUND_DOWN((uintptr_t)(void *)_kimg_start, PAGE_SIZE);

    kprintfluffy(40, &art, "");   
    kprintfluffy(40, &art, "System Memory Map:");
    kprintfluffy(40, &art, "");

    int fluffy = print_memory_info(mmio, mmiocnt, ram, ramcnt, resv, resvcnt);

    for(int i = 0; i < CAT_SIZE - 5 - fluffy; i++){ 
        kprintfluffy(40, &art, "");
    }

    void * heap_start;
    void * heap_end;

    uintptr_t pma;
    const void * pp;


    // Kernel must fit inside 2MB megapage (one level 1 PTE)

    if (MEGA_SIZE < _kimg_end - _kimg_start) panic(NULL);

    // Initialize main page table with the following direct mapping:
    //
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // _kimg_start to _kimg_end:         RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB

    // Identity mapping of MMIO region as two gigapage mappings
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        main_pt2[VPN2(pma)] = leaf_pte((void * )pma, PTE_R | PTE_W | PTE_G);

    // Third gigarange has a second-level subtable
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);

    // First two physical megaranges of RAM are mapped as individual pages with
    // permissions based on kernel image region.
    //
    // This also means that the kernel must be smaller than 4 MB

    main_pt1_0x80000[VPN1(RAM_START_PMA)] = ptab_pte(main_pt0_0x80000, PTE_G);
    main_pt1_0x80000[VPN1(RAM_START_PMA + MEGA_SIZE)] = ptab_pte(main_pt0_0x80001, PTE_G);

    //Map the reserved region before the kernel
    for (pp = RAM_START; pp < text_start; pp+=PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE) {
        assert(PTE_VALID(main_pt1_0x80000[VPN1((uintptr_t)pp)])); //Kernel too big.
        struct pte * pt1 = pageptr(main_pt1_0x80000[VPN1((uintptr_t)pp)].ppn);
        pt1[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE) {
        assert(PTE_VALID(main_pt1_0x80000[VPN1((uintptr_t)pp)]));
        struct pte * pt1 = pageptr(main_pt1_0x80000[VPN1((uintptr_t)pp)].ppn);
        pt1[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE) {
        assert(PTE_VALID(main_pt1_0x80000[VPN1((uintptr_t)pp)]));
        struct pte * pt1 = pageptr(main_pt1_0x80000[VPN1((uintptr_t)pp)].ppn);
        pt1[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages - dtb area for QEMU

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END - MEGA_SIZE; pp += MEGA_SIZE) {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    //Map DTB as read only
    main_pt1_0x80000[VPN1((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_G);

    // Enable paging; this part always makes me nervous.

    main_mtag = ptab_to_mtag(main_pt2, 0);
    csrw_satp(main_mtag);
    kprintf("Enabled Address Translation.\n");
    sfence_vma();

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end;
    heap_end = (void * )ROUND_UP((uintptr_t)heap_start, PAGE_SIZE);

    if (heap_end - heap_start < HEAP_INIT_MIN) {
        heap_end += ROUND_UP(HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE);
    }

    if (RAM_END < heap_end)
        panic("out of memory");

    // Initialize heap memory manager

    heap_init(heap_start, heap_end - heap_start);

    debug("Heap allocator: [%p,%p): %zu KB free", heap_start, heap_end,
            (heap_end - heap_start) / 1024);

    debug("Heap allocator: [%p,%p): %zu KB free",
            heap_start, heap_end, (heap_end - heap_start) / 1024);

    // Initialize free chunk list
    free_chunk_list = (struct page_chunk*)heap_end;
    free_chunk_list->next = NULL;
    free_chunk_list->pagecnt = ((uintptr_t)RAM_END - (uintptr_t)heap_end) / PAGE_SIZE;      // page_count is determined by taking total RAM - all other memory all ready in use
    
    kprintfluffy(40, &art, "Free Chunk List Initialized!");
    kprintfluffy(40, &art, "%d Free Pages.", free_phys_page_count());
    kprintfluffy(40, &art, "");

    debug("Page allocator: [%p,%p): %u pages free",
            heap_end, RAM_END, free_chunk_list->pagecnt);

    // Allow supervisor to access user memory. We could be more precise by only
    // enabling supervisor access to user memory when we are explicitly trying
    // to access user memory, and disable it at other times. This would catch
    // bugs that cause inadvertent access to user memory (due to bugs).

    csrs_sstatus(RISCV_SSTATUS_SUM);

    memory_initialized = 1;
}

/*
INPUTS: no inputs
OUTPUTS: returns the mtag_t of the current memory space
DESCRIPTION: gives the satp value which labels current memory space
SIDE EFFECTS: no side effects
*/
mtag_t active_mspace(void) {
    return csrr_satp();
}

/*
INPUTS: mtag_t mtag - memory space to be switched to
OUTPUTS: returns mtag or previous memory space
DESCRIPTION: switches the current memory space to the one passed in
SIDE EFFECTS: flushes TLB via sfence
*/
mtag_t switch_mspace(mtag_t mtag) {
    mtag_t swap = csrr_satp();
    csrw_satp(mtag);
    sfence_vma();                   // must be used after csrw
    return swap;
}

/*
INPUTS: no inputs
OUTPUTS: returns mtag of cloned memory space
DESCRIPTION: creates a copy of current memory space
SIDE EFFECTS: allocates memory
*/
mtag_t clone_active_mspace(void) {
    // clone the active mspace shallow copy globals, deep copy non-globals
    // do we need to worry about ASID here or is 0 fine?
    struct pte * src_pt2 = active_space_ptab();                                                
    struct pte * new_pt2 = alloc_phys_page();   // grab a fresh root pt for the new mspace 
    memset(new_pt2, 0, PAGE_SIZE);

    for (int i2 = 0; i2 < PTE_CNT; i2++) {  
        struct pte pte2 = src_pt2[i2];                                                           
                  
        if (!PTE_VALID(pte2)) {       //basically just 0 and move on                                               
            new_pt2[i2] = null_pte();
            continue;                                                                            
        }       

        if (PTE_GLOBAL(pte2)) {//globals get shallow copied, dont touch the data
            new_pt2[i2] = pte2;
            continue;
        }                                                                                        
   
        if (PTE_LEAF(pte2)) panic("bad gigapage");//we shouldn't ever clon bad ones
        
        //if its a table ptr we need to recurse and build a new pt1
        struct pte * src_pt1 = pageptr(pte2.ppn); //og pt1
        struct pte * new_pt1 = alloc_phys_page();   //new pt1
        memset(new_pt1, 0, PAGE_SIZE);                                             
   
        for (int i1 = 0; i1 < PTE_CNT; i1++) {  //walk pt1 entries
            struct pte pte1 = src_pt1[i1];
                                                                                                   
            if (!PTE_VALID(pte1)) {//blank entry
                new_pt1[i1] = null_pte();
                continue;
            }

            if (PTE_GLOBAL(pte1)) {//shallow copy
                new_pt1[i1] = pte1;
                continue;                                                                        
            }
                                                                                                   
            if (PTE_LEAF(pte1)) panic("megapage shouldnt be here");
                   
            // @TODO do i need to check A/D bits on intermediate PTEs? 
            // riscv spec says they should be 0 but idk if we always zero them
            struct pte * src_pt0 = pageptr(pte1.ppn);//old pt0
            struct pte * new_pt0 = alloc_phys_page();   //new pt0 
            memset(new_pt0, 0, PAGE_SIZE);                                         
                  
            for (int i0 = 0; i0 < PTE_CNT; i0++) {    //walk its entries                                           
                struct pte pte0 = src_pt0[i0];
                                                                                                   
                if (!PTE_VALID(pte0)) {
                    new_pt0[i0] = null_pte();//empty slot
                    continue;
                }
                                                                                                   
                if (PTE_GLOBAL(pte0)) {
                    new_pt0[i0] = pte0;   //global leaf so share page                                                       
                } else {
                    // finally a real leaf we need to deep copy so alloc new page and memcpy old data
                    void * new_page = alloc_phys_page();
                    memcpy(new_page, pageptr(pte0.ppn), PAGE_SIZE);                              
                    struct pte new_pte = pte0;
                    new_pte.ppn = pagenum(new_page);                                             
                    new_pt0[i0] = new_pte;
                }                                                                                
            }
                                                                                                   
            struct pte new_pte1 = pte1;
            new_pte1.ppn = pagenum(new_pt0);//hook new pto to new pt1
            new_pt1[i1] = new_pte1;
        }                                                                                        
   
        struct pte new_pte2 = pte2;                                                              
        new_pte2.ppn = pagenum(new_pt1);//hook new pt1 to new pt2
        new_pt2[i2] = new_pte2;
    }                                                                                            
   
    return ptab_to_mtag(new_pt2, 0);// asid 0 for now, ...
}

/*
INPUTS: no inputs
OUTPUTS: no outputs
DESCRIPTION: resets all non global memory pages in active space
SIDE EFFECTS: frees physical memory; flushes TLB vis sfence
*/
void reset_active_mspace(void) {
    struct pte *pt2 = active_space_ptab();                                                                                              
                                                                                                                                          
    for (int i2 = 0; i2 < PTE_CNT; i2++) {                                                                                              
        struct pte pte2 = pt2[i2];      

        if (!PTE_VALID(pte2)) continue;
        if (PTE_LEAF(pte2)) {
            if (!PTE_GLOBAL(pte2)) pt2[i2] = null_pte();
            continue;
        }                            
                  
        struct pte *pt1 = pageptr(pte2.ppn);
        int pt1_empty = 1;
                                                                                                                                          
        for (int i1 = 0; i1 < PTE_CNT; i1++) {
            struct pte pte1 = pt1[i1];                                                                                                  
                  
            if (!PTE_VALID(pte1)) continue;
            if (PTE_LEAF(pte1)) {
                if (PTE_GLOBAL(pte1)) {
                    pt1_empty = 0;
                } else {
                    pt1[i1] = null_pte();
                }
                continue;
            }
                                                                                                                                          
            struct pte *pt0 = pageptr(pte1.ppn);    
            int pt0_empty = 1;                                                                                                  
                  
            for (int i0 = 0; i0 < PTE_CNT; i0++) {                                                                                      
                struct pte pte0 = pt0[i0];
                                                                                                                                          
                if (!PTE_VALID(pte0)) continue;
                if (PTE_GLOBAL(pte0)) {
                    pt0_empty = 0;
                    continue;
                }

                free_phys_page(pageptr(pte0.ppn));
                pt0[i0] = null_pte();
            }
            if (pt0_empty == 1 && !PTE_GLOBAL(pte1)) {
                free_phys_page(pt0);
                pt1[i1] = null_pte();   
            } else {
                pt1_empty = 0;
            }                                                                                                                                                                               
        } 
        if (pt1_empty == 1 && !PTE_GLOBAL(pte2)) {
            free_phys_page(pt1);
            pt2[i2] = null_pte();   
        }                                                                                                                                                                                     
    }
    sfence_vma();
}

/*
INPUTS: no inputs
OUTPUTS: returns mtag
DESCRIPTION: frees all page / page table data in current memory space and switches to main memory 
SIDE EFFECTS: switches active mem space; flusshes TLB via sfence; updates memory
*/
mtag_t discard_active_mspace(void) {
    // grab the root before we switch --x-- once we swap to main, active_space_ptab returns mains root not ours
    struct pte *pt2 = active_space_ptab();
    switch_mspace(main_mtag);       // back to main first so we arent walking the live mspace

    for (int i2 = 0; i2 < PTE_CNT; i2++) {
        struct pte pte2 = pt2[i2];

        // skip empty slots and globals --x-- globals are shared with main, dont touch
        if (!PTE_VALID(pte2) || PTE_GLOBAL(pte2)) continue;
        if (PTE_LEAF(pte2)) panic("bad gigapage");

        struct pte *pt1 = pageptr(pte2.ppn);

        for (int i1 = 0; i1 < PTE_CNT; i1++) {
            struct pte pte1 = pt1[i1];

            if (!PTE_VALID(pte1) || PTE_GLOBAL(pte1)) continue;
            if (PTE_LEAF(pte1)) panic("megapage shouldnt be here");

            struct pte *pt0 = pageptr(pte1.ppn);

            for (int i0 = 0; i0 < PTE_CNT; i0++) {
                struct pte pte0 = pt0[i0];
                if (!PTE_VALID(pte0) || PTE_GLOBAL(pte0)) continue;
                // non global leaf: this physical page belongs only to our mspace so free it
                free_phys_page(pageptr(pte0.ppn));
            }

            free_phys_page(pt0);    // pt0 was allocated by clone specifically for our mspace
        }
        
        free_phys_page(pt1);        // same deal with pt1 --x-- always unique per mspace
    }

    free_phys_page(pt2);            // finally free the root
    return main_mtag;               // we're on main now, return main's mtag per spec
}

// The map_page() function maps a single page into the active address space at
// the specified address. The map_range() function maps a range of contiguous
// pages into the active address space. Note that map_page() is a special case
// of map_range(), so it can be implemented by calling map_range(). Or
// map_range() can be implemented by calling map_page() for each page in the
// range. The current implementation does the latter.

// We currently map 4K pages only. At some point it may be desirable to support
// mapping megapages and gigapages.

/*
INPUTS: uintptr_t vma - virtual memory adress to begin mapping to
        void * pp - pointer to the page to be mapped
        int rwxug_flags - flags to be mapped with
OUTPUTS: no output
DESCRIPTION: maps one single page
SIDE EFFECTS: page table updated, memory updated
*/
void * map_page(uintptr_t vma, void * pp, int rwxug_flags) {
    return map_range(vma, PAGE_SIZE, pp, rwxug_flags);
}

/*
INPUTS: uintptr_t vma - starting virtual address
        size_t size - range of length given in bytes (byte count)
        void * pp - starting physical address
        int rwxug_flags - flags
OUTPUTS: vma on sucess NULL on failure
DESCRIPTION: map the range of physical memory at pp into the virtual memory at vma
SIDE EFFECTS: calles sfence_vma(), makes new page tables, writes into page tables.
*/
void * map_range(uintptr_t vma, size_t size, void * pp, int rwxug_flags) {
    /*check the permission flags in memory.h

    This function maps a contiguous range of physical memory beginning at pp into the active memory
    space beginning at vma. The range length is given in bytes as size and may span multiple pages. This
    function must meet the requirements below.

    Must create mappings covering the entire requested range on success.
    Must return vma on success.

    See the header file memory.h for permission flag

    */
    size_t map_size; // size rounded up
    size_t offset = 0; // offset
    uintptr_t new_vma; // current vma as we move
    void * new_pp; // current pp as we move

    struct pte * root; // page table root
    struct pte * pt1; // page table 1
    struct pte * pt0; // page table 0


    // just return sucess
    if (size == 0) {
        return (void *)vma;
    }

    // check if vma and pp are allinged with the pages
    if (vma % PAGE_SIZE != 0) {
        return NULL;
    }
    
    // check if it is wellformed
    if (!(wellformed(vma)) || !(wellformed(vma + size - 1))) return NULL;


    //if ((char *)pp % PAGE_SIZE != 0) return NULL; confused by this and it throws an error so im just going to comment it out for now @TODO
    
    
    // round size up
    map_size = ((size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    
    // get the root page
    root = active_space_ptab();
    
    // map each page one at a time
    while (offset < map_size) {
        // find new vma and pp
        new_vma = vma + offset;
        new_pp = (void *)((char *)pp + offset);

        // if the root isnt there make a page table 1
        if (!PTE_VALID(root[VPN2(new_vma)])) {
            // allocate one page, clear it and install pointer
            pt1 = alloc_phys_page();
            memset(pt1, 0, PAGE_SIZE);
            root[VPN2(new_vma)] = ptab_pte(pt1, rwxug_flags & PTE_G);
        }
        // if the root is a leaf then error
        else if (PTE_LEAF(root[VPN2(new_vma)])) {
            return NULL;
        }

        // get page table 1
        pt1 = pageptr(root[VPN2(new_vma)].ppn);

        // if the page table 1 isnt there make a page table 0
        if (!PTE_VALID(pt1[VPN1(new_vma)])) {
            // allocate one page, clear it and install pointer
            pt0 = alloc_phys_page();
            memset(pt0, 0, PAGE_SIZE);
            pt1[VPN1(new_vma)] = ptab_pte(pt0, rwxug_flags & PTE_G);
        }
        // if the page table 1 is a leaf then error
        else if (PTE_LEAF(pt1[VPN1(new_vma)])) {
            return NULL;
        }
        
        // get the page table 0
        pt0 = pageptr(pt1[VPN1(new_vma)].ppn);

        // map final leaf
        pt0[VPN0(new_vma)] = leaf_pte(new_pp, rwxug_flags & (PTE_R | PTE_W | PTE_X | PTE_U | PTE_G));

        // go to next page
        offset += PAGE_SIZE;
    }
    // flush old translations
    sfence_vma();

    // on sucess return vma
    return (void *)vma;
}

/*
INPUTS: uintptr_t vma - virtual memory adress to begin mapping to
        size_t size - amount of memory to eb be allocated and mapped
        int rwxug_flags - flags to be mapped with
OUTPUTS: no outputs
DESCRIPTION: allocates and maps memory within a range
SIDE EFFECTS: page table updated, memory updated
*/
void * alloc_and_map_range(uintptr_t vma, size_t size, int rwxug_flags) {
    if (vma % PAGE_SIZE != 0) panic("vma error");           // check if vma is allinged
    if (size == 0) return (void *)vma;                      // doesn't need to do anything

    uintptr_t curr_addr = vma;                              // map them into virtual memory and free if failed
    uintptr_t final_addr = vma + ROUND_UP(size, PAGE_SIZE);

    while (curr_addr < final_addr) {                        // allocate and map 1 page at a time 
        void *new_page = alloc_phys_page();
        if (new_page == NULL) return NULL;
        memset(new_page, 0, PAGE_SIZE);
        if (map_page(curr_addr, new_page, rwxug_flags) == NULL) { // map new paage to virtual mem
            free_phys_page(new_page);
            return NULL;                                 // if can not be mapped free page, throw error
        }
        curr_addr += PAGE_SIZE;
    }

    return (void *)vma;                                     // return vma on sucess
}

/*
INPUTS: const void * vp - pointer to starting vitrual adress
        size_t size - size of virtual adressess to update through
        int rwxug_flags - flags to be updated
OUTPUTS: no outputs
DESCRIPTION: updates the flags for all the virtual adressess passed within the range
SIDE EFFECTS: sfence called; virtual flags updated
*/
void set_range_flags(const void * vp, size_t size, int rwxug_flags) {
    uintptr_t curr = (uintptr_t)vp;                             // points to the current virtual adress
    uintptr_t end = (uintptr_t)(vp + size);

    // loop through all page tables to look for current adress
    while (curr < end) {
        struct pte * pt2 = active_space_ptab();                 // root page table
        if (!PTE_VALID(pt2[VPN2(curr)])) {                      // determine if mapped        
            curr += PAGE_SIZE;
            continue;
        }

        struct pte * pt1 = pageptr(pt2[VPN2(curr)].ppn);        // level 1 page table
        if (!PTE_VALID(pt1[VPN1(curr)])) {                      // determine if mapped        
            curr += PAGE_SIZE;
            continue;
        }

        struct pte * pt0 = pageptr(pt1[VPN1(curr)].ppn);
        if (!PTE_VALID(pt0[VPN0(curr)])) {                      // determine if mapped        
            curr += PAGE_SIZE;
            continue;
        }

        pt0[VPN0(curr)].flags = rwxug_flags | PTE_V| PTE_A | PTE_D;             // when leaf is found update the flags
        curr += PAGE_SIZE;
    }
    sfence_vma();
}

/*
INPUTS: uintptr_t vma - starting virtual address
        size_t size - range of length given in bytes (byte count)
OUTPUTS: None
DESCRIPTION: unmap the range of virtual memory at vma and size
SIDE EFFECTS: calles sfence_vma(), clears page table entries, frees pages.
*/
void unmap_and_free_range(void * vp, size_t size) {
    size_t map_size;                    // size rounded up
    size_t offset = 0;                  // offset
    uintptr_t vma;                      // starting vma
    uintptr_t new_vma;                  // current vma as we move
    struct pte * root;                  // page table root
    struct pte * pt1;                   // page table 1
    struct pte * pt0;                   // page table 0

    if (size == 0) return;

    vma = (uintptr_t)vp;                // set the vma

    if (vma % PAGE_SIZE != 0) return;   // check if vma and pp are allinged with the pages
    
    if (!(wellformed(vma)) || !(wellformed(vma + size - 1))) return;    
    
    map_size = ROUND_UP(size, PAGE_SIZE);
    root = active_space_ptab();
    
    while (offset < map_size) {                         // map each page one at a time
        new_vma = vma + offset;                         // find new vma and pp

        if (!PTE_VALID(root[VPN2(new_vma)])) {          // if the root isnt there skip it
            offset += PAGE_SIZE;                        // allocate one page, clear it and install pointer
            continue;
        } else if (PTE_LEAF(root[VPN2(new_vma)])) {     // if the root is a leaf also skip it
            offset += PAGE_SIZE;
            continue;
        }
        
        pt1 = pageptr(root[VPN2(new_vma)].ppn);         // get page table 1

        if (!PTE_VALID(pt1[VPN1(new_vma)])) {           // if the page table 1 isnt there skip it
            offset += PAGE_SIZE;
            continue;
        } else if (PTE_LEAF(pt1[VPN1(new_vma)])) {      // if the page table 1 is a leaf skip it
            offset += PAGE_SIZE;
            continue;
        }
        
        pt0 = pageptr(pt1[VPN1(new_vma)].ppn);          // get the page table 0

        if (PTE_VALID(pt0[VPN0(new_vma)])) {            // if theres a final leaf free the page and clear
            free_phys_page(pageptr(pt0[VPN0(new_vma)].ppn));
            pt0[VPN0(new_vma)] = null_pte();
        }
        offset += PAGE_SIZE;                            // go to next page
    }
    sfence_vma();                                       // flush
}

/*
INPUTS: const void * vp - pointer to starting virtual memory adress
        size_t size - total range we will want to enforce the flags of
        int rwxug_flags - flags we are enforcing
OUTPUTS: returns 0 on success; negative error code on failure
DESCRIPTION: ensures that all pages within the given range match the expected flags passed through
SIDE EFFECTS: may lazily alter/format memory when mapping unmapped pages
*/
int enforce_vptr(const void * vp, size_t size, int rwxug_flags) {
    uintptr_t start_addr = (uintptr_t)vp;                   // these are refering to the virtual adress not physicals
    uintptr_t final_addr = start_addr + size;

    if (vp == NULL) return -EINVAL;                         // general checks ensuring we are accessing legal memory only
    if (start_addr > final_addr) return -EINVAL;
    if (!wellformed(start_addr) ||!wellformed(final_addr - 1)) return -EINVAL;
    if (start_addr < UMEM_START_VMA || final_addr > UMEM_END_VMA) return -EINVAL;
    if ((rwxug_flags & PTE_X) != 0) return -EINVAL;

    uintptr_t curr_addr = ROUND_DOWN(start_addr, PAGE_SIZE);        // flags are tracked via page so must be on page boundry to track

    while (curr_addr < final_addr) {                        // loop through all adresses in this range ensuring the have the proper flags set
        struct pte *pt2 = active_space_ptab();
        struct pte *pt1 = NULL;
        struct pte *pt0 = NULL;
        int addr_mapped = 1;                            // we need to enforce that all level of the table are mapped; assume yes mark no if unmapped

        if (!PTE_VALID(pt2[VPN2(curr_addr)])) {         // ensure each level of mapping exists
            addr_mapped = 0;                            // if any level does not exist mark un mapped
        } else {
            pt1 = pageptr(pt2[VPN2(curr_addr)].ppn);
            if (!PTE_VALID(pt1[VPN1(curr_addr)])) {
                addr_mapped = 0;
            } else {
                pt0 = pageptr(pt1[VPN1(curr_addr)].ppn);
                if (!PTE_VALID(pt0[VPN0(curr_addr)])) {
                    addr_mapped = 0;
                }
            }
        }
        
        if (addr_mapped == 0) {                         // if unmapped at this adress map and allocated as well as set proper flags
            void *new_page = alloc_phys_page();
            if (new_page == NULL) return -ENOMEM;
            memset(new_page, 0, PAGE_SIZE);
            if (map_page(curr_addr, new_page, rwxug_flags) == NULL) {        // map new_page to curr_addr free page and return error if failed
                free_phys_page(new_page);
                return -ENOMEM;
            }
        } else {                                        // if mapped, ensure flags are properly set, if not return error
            if ((pt0[VPN0(curr_addr)].flags & rwxug_flags & (PTE_R | PTE_W  | PTE_X | PTE_U | PTE_G)) != (rwxug_flags & (PTE_R | PTE_W  |PTE_X | PTE_U | PTE_G))) {
                return -EFAULT;
            }
        }
        curr_addr += PAGE_SIZE;                         // move on to next page
    }

    return 0;       // on success
}

/*
INPUTS: const char * vs - pointer to the virutual adress
        int rwxug_flags - flags we need to validate
OUTPUTS: returns 0 on success; negative error code on failure
DESCRIPTION: begins on the adress passed through and validates that the passed string is null terminated in both legal memory and pages
SIDE EFFECTS: no side effects
*/
int validate_vstr(const char * vs, int rwxug_flags) {
    if (vs == NULL) return -EINVAL;
    uintptr_t virt_ad = (uintptr_t)vs;

    while (1 == 1) {                                            // loop through continiuos pages until null terminated is found
        uintptr_t curr_addr = ROUND_DOWN(virt_ad, PAGE_SIZE);

        if(!wellformed(curr_addr)) return -EINVAL;

        struct pte *pt2 = active_space_ptab();                  // root
        if (!PTE_VALID(pt2[VPN2(curr_addr)])) return -EFAULT;

        struct pte *pt1 = pageptr(pt2[VPN2(curr_addr)].ppn);    // pt1
        if (!PTE_VALID(pt1[VPN1(curr_addr)])) return -EFAULT;

        struct pte *pt0 = pageptr(pt1[VPN1(curr_addr)].ppn);    // pt0
        if (!PTE_VALID(pt0[VPN0(curr_addr)])) return -EFAULT;

        // ensure found page is valid and has the correct flags
        if ((pt0[VPN0(curr_addr)].flags  & rwxug_flags & (PTE_R | PTE_W | PTE_X | PTE_U | PTE_G)) != (rwxug_flags & (PTE_R | PTE_W | PTE_X | PTE_U | PTE_G))) return -EFAULT;

        // loop through page looking for null terminator or move to next page if we hit end of page
        uintptr_t page_end = curr_addr + PAGE_SIZE;
        while (virt_ad < page_end) {                        // loop through all remaining adresses in this page
            if ( *(char *)virt_ad == '\0') return 0;        // once the null terminator is found we return
            virt_ad++;
        }

    }
    return -EACCESS;        // should never reach here
}

/*
INPUTS: no inputs
OUTPUTS: returns a pointer to the newly allocated page
DESCRIPTION: creates a single new physical page; not mapped to virtual memorys
SIDE EFFECTS: allocates new memory
*/
void * alloc_phys_page(void) {
    return alloc_phys_pages(1);
}

/*
INPUTS: void * pp - pointer to the physical page to be freed
OUTPUTS: no outputs
DESCRIPTION: frees the physical page being pointed to
SIDE EFFECTS: updates chunk list
*/
void free_phys_page(void * pp) {
    // Return a single physical page to the physical memory allocator.
    // Must return the page pp to the free_chunk_list and make it available for future allocations.
    free_phys_pages(pp, 1);
    return;
}

/*
INPUTS: unsigned int cnt - number of continious pages to be allocated
OUTPUTS: no outputs
DESCRIPTION: allocates cnt number contininous pages of memory
SIDE EFFECTS: allocates new memory; chunk list updated
*/
void * alloc_phys_pages(unsigned int cnt) {
    struct page_chunk* next = free_chunk_list; // temp to traverse linked list
    struct page_chunk* previous = NULL;
    if (cnt == 0) {
        panic("cnt == 0");
    }

    struct page_chunk * smallest = NULL;                    // tracks the smallest chunk we can fit
    struct page_chunk * lagging = NULL;                     // tracks the previous entry of smallest
    while (next != NULL) {                                  // loop through free chunk list to find smallest usable chunk
        if (next->pagecnt >= cnt) {
            if (smallest == NULL || next->pagecnt < smallest->pagecnt) {
                smallest = next;
                lagging = previous;
            }
        }
        previous = next;
        next = next->next;
    }
    assert (smallest != NULL);
    next = smallest;
    previous = lagging;
    
    // if the page count is exactly cnt
    if (next->pagecnt == cnt) {
        // if its the first item in free_chunk_list set the list to the next one
        if (previous == NULL) {
            free_chunk_list = next->next;
        }
        else {
            previous->next = next->next;
        }
    }
    // if page cnt is bigger than split the chunk
    else {
        // make a new chunk for the leftover chunk
        struct page_chunk* new_chunk = (struct page_chunk *)(pageptr(pagenum(next) + cnt));
        // set size to remaining size and point to the next chunk
        new_chunk->pagecnt = next->pagecnt - cnt;
        new_chunk->next = next->next;
        if (previous == NULL) {
            free_chunk_list = new_chunk;
        }
        else {
            previous->next = new_chunk;
        }
    }

    return (void *)next; 
}

/*
INPUTS: void * pp - pointer to the physical page
        unsigned int cnt - number of pages to free
OUTPUTS: no outputs
DESCRIPTION: adds continious chunks starting from pp back to the free chunk list
SIDE EFFECTS: free chun list updated
*/
void free_phys_pages(void * pp, unsigned int cnt) {
    if (cnt == 0) return;

    struct page_chunk *new_chunk = (struct page_chunk *)pp;         // make a new chunk at pp

    // make the new chunk the size of memory to free and add it to the head of the linked list
    new_chunk->pagecnt = cnt;
    new_chunk->next = free_chunk_list;
    free_chunk_list = new_chunk;
    return;
}

/*
INPUTS: no inputs
OUTPUTS: returns total number of free physical pages
DESCRIPTION: adds up all total free pages in the chunk list
SIDE EFFECTS: no side effects
*/
unsigned long free_phys_page_count(void) {
    unsigned long page_cnt = 0;
    struct page_chunk *next = free_chunk_list; // temp to traverse linked list
    
    while (next != NULL) { // search through all 
        page_cnt += next->pagecnt;
        next = next->next;
    }
    return page_cnt;
}

/*
INPUTS: struct trap_frame * tfr - pointer to the trap frame
        intptr_t vma - virtual address
OUTPUTS: returns 1 on success; 0 on failure
DESCRIPTION: handles a page fault caused by a user-mode memory access to the virtual address vma
SIDE EFFECTS: might allocate physical page and create a page mapping
*/
int handle_umode_page_fault(struct trap_frame * tfr, uintptr_t vma) {
    if (vma < UMEM_START_VMA || vma >= UMEM_END_VMA) return 0;

    uintptr_t new_vma = ROUND_DOWN(vma, PAGE_SIZE);             // rounded down vma
    void * new_pp = alloc_phys_page();
    if (new_pp == NULL) return 0;                               // we always return 0 to indicate fatal fault error
    
    memset(new_pp, 0, PAGE_SIZE);                               // clear and map new page memory, completeing lazy allocation
    map_page(new_vma, new_pp, PTE_R | PTE_W | PTE_U);
    return 1;
}

// ---INTERNAL FUNCTION DEFINITIONS--- //

mtag_t active_space_mtag(void) {
    return csrr_satp();
}

static inline mtag_t ptab_to_mtag(struct pte * ptab, unsigned int asid) {
    return ( 
            ((unsigned long)PAGING_MODE << RISCV_SATP_MODE_shift) |
            ((unsigned long)asid << RISCV_SATP_ASID_shift) |
            pagenum(ptab) << RISCV_SATP_PPN_shift);
}

static inline struct pte * mtag_to_ptab(mtag_t mtag) {
    return (struct pte * )((mtag << 20) >> 8);
}

static inline struct pte * active_space_ptab(void) {
    return mtag_to_ptab(active_space_mtag());
}

static inline void * pageptr(uintptr_t n) {
    return (void *)(n << PAGE_ORDER);
}

static inline unsigned long pagenum(const void * p) {
    return (unsigned long)p >> PAGE_ORDER;
}

static inline int wellformed(uintptr_t vma) {
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (intptr_t)vma >> 38;
    return (!bits || !(bits+1));
}

static inline struct pte leaf_pte(const void * pp, uint_fast8_t rwxug_flags) {
    return (struct pte) {
        .flags = rwxug_flags | PTE_A | PTE_D | PTE_V,
            .ppn = pagenum(pp)
    };
}

static inline struct pte ptab_pte(const struct pte * pt, uint_fast8_t g_flag) {
    return (struct pte) {
        .flags = g_flag | PTE_V,
            .ppn = pagenum(pt)
    };
}

static inline struct pte null_pte(void) {
    return (struct pte) { };
}

//This function assumes:
//- resv strictly overlaps with RAM
//- MMIO and RAM do not overlap
//- Arrays are sorted
//- 0 size entries are invalid
//- That I'm very sorry to whoever has to read this.
//
// * THIS FUNCTION IS DESTRUCTIVE TO MREGION ARRAYS.
//
//...returns how many times kprintfluffy was called :innocent:
static int print_memory_info(struct mregion * mmio_imm, unsigned long mmiocnt, struct mregion * ram_imm, unsigned long ramcnt, struct mregion * resv_imm, unsigned long resvcnt) {
    unsigned long mmio_idx = 0;
    unsigned long ram_idx = 0;
    unsigned long resv_idx = 0;

    int fluffy = 0;

    //The first thing we do is copy out the arguments to prevent destruction of
    //the arguments.

    struct mregion mmio[mmiocnt];
    struct mregion ram[ramcnt];
    struct mregion resv[resvcnt];

    memcpy(mmio, mmio_imm, mmiocnt * sizeof(struct mregion));
    memcpy(ram, ram_imm, ramcnt * sizeof(struct mregion));
    memcpy(resv, resv_imm, resvcnt * sizeof(struct mregion));

    while (mmio_idx < mmiocnt || ram_idx < ramcnt) {
        uintptr_t mmio_loc = mmio[mmio_idx].pma;
        uintptr_t ram_loc = ram[ram_idx].pma;
        uintptr_t resv_loc = resv[resv_idx].pma;
        
        if (mmio_loc < ram_loc && mmio_idx < mmiocnt) {
            kprintfluffy(40, &art, "%08p - %08p - MMIO", 
                         mmio_loc, 
                         mmio_loc + mmio[mmio_idx].size - 1);
            fluffy++;
            mmio_idx++;
        } else {

            if (ram_loc <= resv_loc && 
               ram_loc + ram[ram_idx].size > resv_loc && resv_idx < resvcnt) {

                if (ram_loc < resv_loc) {
                    kprintfluffy(40, 
                                 &art, 
                                 "%p - %p - Free RAM", 
                                 ram_loc, 
                                 resv_loc - 1);
                    ram[ram_idx].size -= resv_loc - ram_loc;
                    ram[ram_idx].pma = resv_loc;
                    fluffy++;
                } else {

                    if (resv_loc <= (uintptr_t)_kimg_start && 
                        resv_loc + resv[resv_idx].size > (uintptr_t)_kimg_start) {

                        if (resv_loc < (uintptr_t)_kimg_start) {
                            kprintfluffy(40, 
                                         &art, 
                                         "%p - %p - SBI Reserved", 
                                         resv_loc, 
                                         (uintptr_t)_kimg_start - 1);

                            resv[resv_idx].size -= 
                                (uintptr_t)_kimg_start - resv_loc;
                            resv[resv_idx].pma = (uintptr_t)_kimg_start;
                            ram[ram_idx].pma = (uintptr_t)_kimg_start;
                            ram[ram_idx].size -= 
                                (uintptr_t)_kimg_start - resv_loc;
                            fluffy++;
                        } else {
                            kprintfluffy(40, 
                                         &art, 
                                         "%p - %p - Kernel TEXT", 
                                         _kimg_text_start, 
                                         ROUND_UP((uintptr_t)_kimg_text_end, 
                                                  PAGE_SIZE) - 1);

                            kprintfluffy(40, 
                                         &art, 
                                         "%p - %p - Kernel RODATA", 
                                         _kimg_rodata_start, 
                                         ROUND_UP((uintptr_t)_kimg_rodata_end, 
                                                  PAGE_SIZE) - 1);

                            kprintfluffy(40, 
                                         &art, 
                                         "%p - %p - Kernel DATA", 
                                         _kimg_data_start, 
                                         ROUND_UP((uintptr_t)_kimg_data_end, 
                                                  PAGE_SIZE) - 1);

                            resv[resv_idx].pma = 
                                ROUND_UP((uintptr_t)_kimg_end, PAGE_SIZE);
                            resv[resv_idx].size -= 
                                ROUND_UP((uintptr_t)_kimg_end, PAGE_SIZE) - resv_loc;
                            ram[ram_idx].pma = 
                                ROUND_UP((uintptr_t)_kimg_end, PAGE_SIZE);
                            ram[ram_idx].size -= 
                                ROUND_UP((uintptr_t)_kimg_end, PAGE_SIZE) - resv_loc;
                            fluffy+=3;
                        }
                    } else {
                        kprintfluffy(40, 
                                     &art, 
                                     "%p - %p - SBI Reserved", 
                                     resv_loc, 
                                     resv_loc + resv[resv_idx].size - 1);
                        ram[ram_idx].pma = resv_loc + resv[resv_idx].size;
                        ram[ram_idx].size -= resv[resv_idx].size;
                        resv_idx++;
                        fluffy++;
                    }
                }
            } else {
                kprintfluffy(40, 
                             &art, 
                             "%p - %p - Free RAM", 
                             ram_loc, 
                             ram_loc + ram[ram_idx].size - 1);
                fluffy++;
                ram_idx++;
            }
        }

        //Ignore NULL entries (don't need to do this first because first entries 
        //are guaranteed to be populated)
        while(mmio[mmio_idx].size == 0 && mmio_idx < mmiocnt)
                mmio_idx++;
        while(ram[ram_idx].size == 0 && ram_idx < ramcnt)
                ram_idx++;
        while(resv[resv_idx].size == 0 && resv_idx < resvcnt)
                resv_idx++;

    }
    return fluffy;
}