#include <sys/image.h>
#include <sys/mman.h>
#include "kdumper.h"

// This next file is coming from the guts of procnto's memmgr and describes
// the data structures used by the physical allocator.
#include "pa.h"

#define PAGE_MASK	(__PAGESIZE-1)

#if defined(__BIGENDIAN__)
	#define ELFDATA_ENDIAN ELFDATA2MSB
#elif defined(__LITTLEENDIAN__)
	#define ELFDATA_ENDIAN ELFDATA2LSB
#else
	#error Endian not defined
#endif

struct walk_data {
	Elf64_Off				data_offset;
	unsigned				num_segs;
	struct asinfo_entry		*bootable;
	unsigned				dir_size;
	unsigned				procnto_segnum;
	unsigned				colour_mask;
	void					(*write_func)(void *, unsigned);
};


static unsigned	paddr_size;

static paddr_t
vaddr_to_paddr(void *p) {
	paddr64_t	paddr;
	unsigned	len;

	cpu_vaddrinfo(p, &paddr, &len);
	return paddr;
}


static void
write_phdr(paddr_t paddr, paddr_t len, unsigned type, unsigned flags, struct walk_data *data) {
	union {
		Elf32_Phdr			_32;
		Elf64_Phdr			_64;
	}				phdr;

	memset(&phdr, 0, sizeof(phdr));
	if(dip->big) {
		phdr._64.p_type = type;
		phdr._64.p_offset = data->data_offset;
		phdr._64.p_vaddr = 0;
		phdr._64.p_paddr = paddr;
		phdr._64.p_filesz = len;
		phdr._64.p_memsz = len;
		phdr._64.p_flags = flags;
		data->write_func(&phdr._64, sizeof(phdr._64));
	} else {
		phdr._32.p_type = type;
		phdr._32.p_offset = data->data_offset;
		phdr._32.p_vaddr = 0;
		phdr._32.p_paddr = paddr;
		phdr._32.p_filesz = len;
		phdr._32.p_memsz = len;
		phdr._32.p_flags = flags;
		data->write_func(&phdr._32, sizeof(phdr._32));
	}
	data->data_offset += len;
}


static void
write_memory(paddr_t start, paddr_t len, int colour, void *d) {
	struct walk_data	*data = d;
	void				*p;
	unsigned			mapped_size;
	unsigned			amount;

#define MAX_AMOUNT	(1*1024*1024)
	for( ;; ) {
		amount = (len > MAX_AMOUNT) ? MAX_AMOUNT : len;
		p = cpu_map(start, amount, PROT_READ, colour, &mapped_size);
		if(mapped_size < amount) amount = mapped_size;
		data->write_func(p, amount);
		cpu_unmap(p, len);
		len -= amount;
		if(len == 0) break;
		start += amount;
	}
}


static void
pm_count_segs(paddr_t paddr, paddr_t len, int colour, void *d) {
	struct walk_data	*data = d;

	data->num_segs += 1;

	// If we have memory that's too high/big to handle with 32-bit ELF, force
	// use of the 64-bit version.
	if(paddr > ~(Elf32_Addr)0) {
		dip->big = 1;
	}
	if(len > ~(Elf32_Word)0) {
		dip->big = 1;
	}
}


static void
pm_write_segment_header(paddr_t paddr, paddr_t len, int colour, void *d) {
	write_phdr(paddr, len, PT_LOAD, PF_R|PF_W|PF_X, d);
}


static void
pm_write_segment_data(paddr_t paddr, paddr_t len, int colour, void *d) {
	if(colour == PAQ_COLOUR_NONE) colour = -1;
	write_memory(paddr, len, colour, d);
}


#define BH_PADDR(bh, f) \
	((paddr_size == sizeof(paddr32_t)) ? (bh)->f.paddr32 : (bh)->f.paddr64)


// Funky types to make sure we handle runs greater than 4G in length
#define PQ_TO_PADDR(bh, pq)	(BH_PADDR(bh,start) + (((paddr_t)(unsigned)((pq) - (bh)->quanta)) << QUANTUM_BITS))

static void
dump_quanta(struct block_head *bh, struct pa_quantum *start, struct pa_quantum *end, 
			void (*func)(paddr_t, paddr_t, int, void *), void *d) {
	struct walk_data	*data = d;
	paddr_t				paddr;
	struct pa_quantum	*pq;
	unsigned			start_colour;
	unsigned			pq_colour;
	unsigned			expected_colour;
	unsigned			idx;

	if(data->colour_mask != 0) {
		// Can only write out sections whose colours are all in agreement.
		pq = start;
		idx = 0;
		start_colour = PAQ_COLOUR_NONE;
		expected_colour = start_colour;
		for( ;; ) {
			pq = &start[idx];
			if(pq >= end) {
				pq_colour = PAQ_COLOUR_NONE;
			} else {
				pq_colour = PAQ_GET_COLOUR(pq);
			}
			if((start_colour == PAQ_COLOUR_NONE) && (pq_colour != PAQ_COLOUR_NONE)) {
				start_colour = (pq_colour - idx) & data->colour_mask;
				expected_colour = pq_colour;
			}
			if(pq_colour == PAQ_COLOUR_NONE) {
				pq_colour = expected_colour;
			}
			if((pq >= end) || (pq_colour != expected_colour)) {
				paddr = PQ_TO_PADDR(bh, start);
				func(paddr, NQUANTUM_TO_LEN(idx), start_colour, data);
				start = pq;
				idx = 0;
				start_colour = PAQ_COLOUR_NONE;
				expected_colour = start_colour;
			} else if(expected_colour != PAQ_COLOUR_NONE) {
				expected_colour = (expected_colour + 1) & data->colour_mask;
			}
			if(pq >= end) break;
			++idx;
		}
	} else {
		paddr = PQ_TO_PADDR(bh, start);
		func(paddr, NQUANTUM_TO_LEN(end - start), PAQ_COLOUR_NONE, data);
	}
}


static void
walk_pmem(struct block_head **start, void (*func)(paddr_t, paddr_t, int, void *), void *data) {
	struct block_head	*bh;
	struct block_head	**bhp;
	struct pa_quantum	*pq;
	struct pa_quantum	*start_pq;
	struct pa_quantum	*end_pq;

	// Dump out any CPU specific blocks of memory that aren't recorded
	// elsewhere.
	cpu_walk_extra_pmem(func, data);

	if(start != NULL) {
		// Walk the pa structures looking for allocated runs and write
		// them out
		bhp = start;
		for( ;; ) {
			bh = *bhp++;
			if(bh == NULL) break;
			pq = bh->quanta;
			end_pq = &pq[bh->num_quanta];
			start_pq = NULL;
			do {
				if((pq->flags & PAQ_FLAG_INUSE) && 
					((pq->flags & PAQ_FLAG_SYSTEM) || dip->dump_all)) {
					if(start_pq == NULL) start_pq = pq;
				} else if(start_pq != NULL) {
					dump_quanta(bh, start_pq, pq, func, data);
					start_pq = NULL;
				}
				pq += pq->run;
			} while(pq < end_pq);
			if(start_pq != NULL) {
				dump_quanta(bh, start_pq, pq, func, data);
			}
		}
	}
}


static int
as_count_segs(struct asinfo_entry *as, char *name, void *d) {
	struct walk_data	*data = d;
	struct image_header	*ifs;

	data->num_segs += 1;
	ifs = cpu_map(as->start, sizeof(*ifs), PROT_READ, -1, NULL);
	if(ifs->boot_ino[0] != 0) {
		//ALTERNATE: Rather than just writing out segments for procnto,
		// we could write out segments for all the bootstrap executables...
		if(data->bootable == NULL) {
			// We've got a bootable image, so increase the number of
			// segments by 2 (assuming that there will be just a code &
			// data segment in the procnto executable)
			data->num_segs += 2;
		}
		// The last bootable image contains procnto
		data->procnto_segnum = data->num_segs - 2;
		data->bootable = as;
		data->dir_size = ifs->hdr_dir_size;
	}
	cpu_unmap(ifs, sizeof(*ifs));
	return 1;
}


static int
as_write_segment_header(struct asinfo_entry *as, char *name, void *d) {
	struct walk_data	*data = d;
	paddr_t				len;
	struct image_header	*ifs;
	union image_dirent	*dir;
	unsigned			procnto_off = 0;
	paddr_t				procnto_paddr;
	paddr_t				save_offset;
	Elf32_Ehdr			*ehdr;
	Elf32_Phdr			*phdr;
	unsigned			i;
	unsigned			phnum;
	Elf32_Off			phoff;

	len = as->end - as->start + 1;
	write_phdr(as->start, len, PT_LOAD, PF_R|PF_W|PF_X, data);
	if(data->bootable == as) {
		// This is the bootable IFS that contains procnto. Scan the
		// directory for bootstrap executables and the last one is proc.

		ifs = cpu_map(as->start, data->dir_size, PROT_READ, -1, NULL);
		dir = (void *)((uintptr_t)ifs + ifs->dir_offset);
		while(dir->attr.size) {
			if(dir->attr.ino & IFS_INO_BOOTSTRAP_EXE) {
				procnto_off = dir->file.offset;
			}
			dir = (union image_dirent *)((uintptr_t)dir + dir->attr.size);
		}
		cpu_unmap(ifs, data->dir_size);

		procnto_paddr = as->start + procnto_off;
		save_offset = data->data_offset;

		ehdr = cpu_map(procnto_paddr, sizeof(*ehdr), PROT_READ, -1, NULL);
		phnum = ehdr->e_phnum;
		phoff = ehdr->e_phoff;
		cpu_unmap(ehdr, sizeof(*ehdr));

		phdr = cpu_map(procnto_paddr + phoff, sizeof(*phdr) * phnum, PROT_READ, -1, NULL);
		for(i = 0; i < phnum; ++i) {
			if(phdr[i].p_type == PT_LOAD) {
				// since the actual data is being written out as part
				// of the IFS block, there's no need to do it again. Just
				// fiddle the offset to point to the right place
				data->data_offset = save_offset - len + procnto_off + phdr[i].p_offset;
				write_phdr(procnto_paddr + phdr[i].p_offset, 
						phdr[i].p_filesz, PT_LOAD, phdr[i].p_flags, data);
				if(debug_flag > 0) {
					kprintf("procnto %s @ 0x%P:0x%08x\n", 
							(phdr[i].p_flags & PF_W) ? "data" : "code", 
							procnto_paddr + phdr[i].p_offset,
							phdr[i].p_filesz);
				}
			}
		}
		cpu_unmap(phdr, sizeof(*phdr) * phnum);

		// restore original data_offset
		data->data_offset = save_offset;
	}
	return 1;
}


static int
as_write_segment_data(struct asinfo_entry *as, char *name, void *d) {
	write_memory(as->start, as->end - as->start + 1, -1, d);
	return 1;
}


static void
write_note_cpu(unsigned cpu, CPU_REGISTERS *ctx, struct walk_data *data) {
	struct kdump_note_cpu		note_cpu;
	char						*name;
	unsigned					len;
	const struct kdebug_private	*kdbgp;
	const struct kdump_private	*kdumpp;
	void						*tmp;
	void						*pgdir;

	memset(&note_cpu, 0, sizeof(note_cpu));
	note_cpu.cpunum = cpu;
	kdbgp = private->kdebug_info->kdbg_private;
	tmp = kdbgp->actives;
	tmp = *(void **)((uintptr_t)((void **)tmp)[cpu] + kdbgp->th_process_off);
	name = *(char **)((uintptr_t)tmp + kdbgp->pr_debug_name_off);
	if(name == NULL) name = "<no name>";
	if(debug_flag > 0) {
		kprintf("CPU %d process: '%s'\n", cpu, name);		
	}

	kdumpp = private->kdebug_info->kdump_private;
	if(kdumpp != NULL) {
		tmp = kdbgp->aspaces;
		tmp = *(void **)((uintptr_t)((void **)tmp)[cpu] + kdbgp->pr_memory_off);
		pgdir = *(void **)((uintptr_t)tmp + kdumpp->as_pgdir_off);
		note_cpu.pgtbl = vaddr_to_paddr(pgdir);
		if(debug_flag > 0) {
			kprintf("CPU %d page table vaddr=%p, paddr=%P\n", cpu, pgdir, (paddr_t)note_cpu.pgtbl);
		}
	}

	len = strlen(name) + 1;
	if(len > sizeof(note_cpu.process_name)) {
		//If the name's too long, we want to write out the tail end
		name += len - sizeof(note_cpu.process_name);
	}
	strcpy(note_cpu.process_name, name);
	data->write_func(&note_cpu, sizeof(note_cpu));
	data->write_func(ctx, sizeof(*ctx));
}


void
dump_system(unsigned sigcode, void *ctx, void (*write_func)(void *, unsigned)) {
	struct block_head			**bhp;
	struct walk_data			data;
	union {
		Elf32_Ehdr		_32;
		Elf64_Ehdr		_64;
	} 				ehdr;
	struct kdump_note			note;
	const struct kdump_private	*kdp;

	memset(&note, 0, sizeof(note));
	memset(&data, 0, sizeof(data));

	bhp = NULL;
	kdp = private->kdebug_info->kdump_private;
	if(kdp != NULL) {
		bhp = *(struct block_head ***)kdp->pmem_root;
	};

	//initialize write function
	write_func(WRITE_CMD_ADDR, WRITE_INIT);

	if(dip->compress > 0) {
		compress_start(write_func);
		write_func = compress_write;
	}
	data.write_func = write_func;

	walk_pmem(bhp, pm_count_segs, &data);

	walk_asinfo("imagefs", as_count_segs, &data);

	note.procnto_segnum  = data.procnto_segnum;
	note.syspage_segnum  = data.num_segs + 0;
	note.dumpinfo_segnum = data.num_segs + 1;
	// extra segment entries: syspage, dump_info, note
	data.num_segs += 1 + 1 + 1;
	if(debug_flag > 0) {
		kprintf("Number of segments:%d, procnto=%d, syspage=%d, dumpinfo=%d\n",
				data.num_segs, 
				note.procnto_segnum, note.syspage_segnum, note.dumpinfo_segnum);
	}

	memset(&ehdr, 0, sizeof(ehdr));
	if(dip->big) {
		memcpy(&ehdr._64.e_ident, ELFMAG, SELFMAG);
		ehdr._64.e_ident[EI_CLASS] = ELFCLASS64;
		ehdr._64.e_ident[EI_VERSION] = EV_CURRENT;
		ehdr._64.e_ident[EI_DATA] = ELFDATA_ENDIAN;
		ehdr._64.e_type = ET_CORE;
		ehdr._64.e_version = EV_CURRENT;
		ehdr._64.e_phoff = sizeof(ehdr._64);
		ehdr._64.e_ehsize = sizeof(ehdr._64);
		ehdr._64.e_phentsize = sizeof(Elf64_Phdr);
		ehdr._64.e_phnum = data.num_segs;
		cpu_elf_header(&ehdr);
		write_func(&ehdr, sizeof(ehdr._64));
		data.data_offset = ehdr._64.e_phoff + sizeof(Elf64_Phdr) * ehdr._64.e_phnum;
	} else {
		memcpy(&ehdr._32.e_ident, ELFMAG, SELFMAG);
		ehdr._32.e_ident[EI_CLASS] = ELFCLASS32;
		ehdr._32.e_ident[EI_VERSION] = EV_CURRENT;
		ehdr._32.e_ident[EI_DATA] = ELFDATA_ENDIAN;
		ehdr._32.e_type = ET_CORE;
		ehdr._32.e_version = EV_CURRENT;
		ehdr._32.e_phoff = sizeof(ehdr._32);
		ehdr._32.e_ehsize = sizeof(ehdr._32);
		ehdr._32.e_phentsize = sizeof(Elf32_Phdr);
		ehdr._32.e_phnum = data.num_segs;
		cpu_elf_header(&ehdr);
		write_func(&ehdr, sizeof(ehdr._32));
		data.data_offset = ehdr._32.e_phoff + sizeof(Elf32_Phdr) * ehdr._32.e_phnum;
	}

	walk_pmem(bhp, pm_write_segment_header, &data);

	walk_asinfo("imagefs", as_write_segment_header, &data);

	//write syspage segment header 
	write_phdr(vaddr_to_paddr(_syspage_ptr), _syspage_ptr->total_size,
			PT_LOAD, PF_R|PF_W|PF_X, &data);

	//write dump_info header
	write_phdr(private->kdump_info, dip->kp_size + offsetof(struct kdump, kp_buff),
			PT_LOAD, PF_R|PF_W|PF_X, &data);

	//write note header
	//ALTERNATE: write out note_cpu's for each cpu
	write_phdr(0, sizeof(note) + (sizeof(struct kdump_note_cpu)+sizeof(CPU_REGISTERS)) * 1, 
				PT_NOTE, 0, &data);

	// Don't set this any sooner - if you do, it will cause more segments
	// to be output than is needed because the dump_quanta function will
	// break things up based on colours
	data.colour_mask = *kdp->colour_mask;
	paddr_size = kdp->paddr_size;

	walk_pmem(bhp, pm_write_segment_data, &data);

	walk_asinfo("imagefs", as_write_segment_data, &data);
				
	write_func(_syspage_ptr, _syspage_ptr->total_size);

	write_func(dip, offsetof(struct kdump, kp_buff));
	if(dip->kp_buff[dip->kp_idx] == '\0') {
		// haven't wrapped around yet, just write the buffer out
		write_func(dip->kp_buff, dip->kp_size);
	} else {
		// unmangle the ring buffer so the oldest stuff comes out first
		write_func(&dip->kp_buff[dip->kp_idx], 
					dip->kp_size - dip->kp_idx);
		write_func(dip->kp_buff, dip->kp_idx);
	}


	//write note segment data 
	note.note_version = NOTE_VERSION_CURRENT;
	note.sig_num   = SIGCODE_SIGNO(sigcode);
	note.sig_code  = SIGCODE_CODE(sigcode);
	note.fault_num = SIGCODE_FAULT(sigcode);

	//ALTERNATE: write out note_cpu's for each CPU
	note.num_cpus = 1;
	note.faulting_cpu = current_cpunum();
	note.regset_size = sizeof(CPU_REGISTERS);
	cpu_note(&note);
	write_func(&note, sizeof(note));
	//ALTERNATE: for(i = 0; i < _syspage_ptr->num_cpu; ++i) 
	write_note_cpu(current_cpunum(), ctx, &data);

	//finalize write function
	write_func(WRITE_CMD_ADDR, WRITE_FINI);
}
