#include "vm_tlb.h"
#include <types.h>
#include <mips/tlb.h>
#include <vm.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>

#include "vm_stats.h"
#include "segments.h"
#include "pt.h"
#include "swapfile.h"
/* under dumbvm, always have 72k of user stack */
/* (this must be > 64K so argument blocks of size ARG_MAX will fit) */
#define STACKPAGES    18

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int tlb_get_rr_victim(void) {
	int victim;
	static unsigned int next_victim = 0; 
	victim = next_victim;
	next_victim = (next_victim + 1) % NUM_TLB; 
	return victim;
}
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i, result;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl/*  page */;
	char is_text_segment = 0;

	faultaddress &= PAGE_FRAME;

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* TODO: kill current process */
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;
	(void)stacktop ;
	(void)stackbase;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		is_text_segment = 1;
		//page = (faultaddress - vbase1) / PAGE_SIZE;
	}
	/*
	else if (faultaddress >= vbase2 && faultaddress < vtop2){
		page = (faultaddress - vbase2) / PAGE_SIZE;
		page += as->as_npages1;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop){
		page = (faultaddress - stackbase) / PAGE_SIZE;
		page += as->as_npages1;
		page += as->as_npages2;
	}
	else 
		return EFAULT;
	*/
	
	increment_tlb_faults();

	/* Get physical address from coremap */
	paddr = get_paddr(faultaddress);

	if(!paddr){
		/* Get a new physical page */
		paddr = as_prepare_load();
		if(faultaddress < vtop2 ? 1 : 0){
			// Page belongs to the ELF file
			result = load_from_elf(as->v, faultaddress, paddr);
			if(result)
				return result;
		}
		//TODO: MANAGING OF PAGES WHICH DO NOT BELONG TO ELF 
		update_table(paddr, 1, faultaddress);
	}
	else
		increment_tlb_reloads();

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		if(is_text_segment)
			elo = paddr | TLBLO_VALID;
		else
			elo = paddr | TLBLO_DIRTY | TLBLO_VALID;	
		tlb_write(ehi, elo, i);
		increment_tlb_faults_free();
		splx(spl);
		return 0;
	}
	/* TLB Replacement */
	ehi = faultaddress;
	if(is_text_segment)
		elo = paddr | TLBLO_VALID;
	else
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	tlb_write(ehi, elo, tlb_get_rr_victim());
	increment_tlb_faults_replace();
	splx(spl);
	return 0;
}