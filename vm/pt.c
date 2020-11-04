#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include "pt.h"

paddr_t page_is_resident(vaddr_t vaddr){
    int i;
    struct addrspace *as = proc_getas();
    for(i = 0; i < N_FRAME; i++){
        if(vaddr == as->page_table[i].vaddr)
            return as->page_table[i].paddr;
    }
    return 0;
}

paddr_t get_proc_frame(vaddr_t vaddr){
    struct addrspace *as = proc_getas();
    int i;
    for(i = 0; i < N_FRAME; i++){
        if(!as->page_table[i].resident){
            as->page_table[i].vaddr = vaddr;
            as->page_table[i].resident = 1;
            return as->page_table[i].paddr;
        }
    }
    /* TODO: Page replacement */
    return -1;
}

