/* ld script to make ARM Linux kernel
 * taken from the i386 version by Russell King
 * Written by Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/memory.h>
#include <asm/page.h>
	
OUTPUT_ARCH(arm)
ENTRY(_start)

#ifndef __ARMEB__
jiffies = jiffies_64;
#else
jiffies = jiffies_64 + 4;
#endif

SECTIONS
{
#ifdef CONFIG_XIP_KERNEL
	. = XIP_VIRT_ADDR(CONFIG_XIP_PHYS_ADDR);
#else
	. = PAGE_OFFSET + TEXT_OFFSET;
#endif
	PROVIDE(_prog_img_start = .);
	.init : {			/* Init code and data		*/
		_stext = .;
		_sinittext = .;
			HEAD_TEXT
			INIT_TEXT
		_einittext = .;
		__proc_info_begin = .;
			*(.proc.info.init)
		__proc_info_end = .;
		__arch_info_begin = .;
			*(.arch.info.init)
		__arch_info_end = .;
		__tagtable_begin = .;
			*(.taglist.init)
		__tagtable_end = .;

		INIT_SETUP(16)

		INIT_CALLS
		CON_INITCALL
		SECURITY_INITCALL
		INIT_RAM_FS

#ifndef CONFIG_XIP_KERNEL
		__init_begin = _stext;
		INIT_DATA
#endif
	}

	PERCPU(PAGE_SIZE)

#ifndef CONFIG_XIP_KERNEL
	. = ALIGN(PAGE_SIZE);
	__init_end = .;
#endif

	/*
	 * unwind exit sections must be discarded before the rest of the
	 * unwind sections get included.
	 */
	/DISCARD/ : {
		*(.ARM.exidx.exit.text)
		*(.ARM.extab.exit.text)
#ifndef CONFIG_HOTPLUG_CPU
		*(.ARM.exidx.cpuexit.text)
		*(.ARM.extab.cpuexit.text)
#endif
#ifndef CONFIG_HOTPLUG
		*(.ARM.exidx.devexit.text)
		*(.ARM.extab.devexit.text)
#endif
#ifndef CONFIG_MMU
		*(.fixup)
		*(__ex_table)
#endif
	}

	. = ALIGN(4096);
	_upage_start = .;
	.upage : { *(.upage) }
	. = ALIGN(4096);

	.text : {			/* Real text segment		*/
		_text = .;		/* Text and read-only data	*/
			__exception_text_start = .;
			*(.exception.text)
			__exception_text_end = .;
			TEXT_TEXT
			SCHED_TEXT
			LOCK_TEXT
			KPROBES_TEXT
#ifdef CONFIG_MMU
			*(.fixup)
#endif
			*(.gnu.warning)
			*(.rodata)
			*(.rodata.*)
			*(.glue_7)
			*(.glue_7t)
	}

        .rel.plt       : { *(.rel.plt) }

	NOTES

	RO_DATA(PAGE_SIZE)

	_etext = .;			/* End of text and rodata section */

#ifdef CONFIG_ARM_UNWIND
	/*
	 * Stack unwinding tables
	 */
	. = ALIGN(8);
	.ARM.unwind_idx : {
		__start_unwind_idx = .;
		*(.ARM.exidx*)
		__stop_unwind_idx = .;
	}
	.ARM.unwind_tab : {
		__start_unwind_tab = .;
		*(.ARM.extab*)
		__stop_unwind_tab = .;
	}
#endif

#ifdef CONFIG_XIP_KERNEL
	__data_loc = ALIGN(4);		/* location in binary */
	. = PAGE_OFFSET + TEXT_OFFSET;
#else
	. = ALIGN(THREAD_SIZE);
	__data_loc = .;
#endif


	_sdata = .;
	.data : AT(__data_loc) {
		_data = .;		/* address in memory */

		/*
		 * first, the init task union, aligned
		 * to an 8192 byte boundary.
		 */
		INIT_TASK_DATA(THREAD_SIZE)

#ifdef CONFIG_XIP_KERNEL
		. = ALIGN(PAGE_SIZE);
		__init_begin = .;
		INIT_DATA
		. = ALIGN(PAGE_SIZE);
		__init_end = .;
#endif

		NOSAVE_DATA
		CACHELINE_ALIGNED_DATA(32)

		/*
		 * The exception fixup table (might need resorting at runtime)
		 */
		. = ALIGN(32);
		__start___ex_table = .;
#ifdef CONFIG_MMU
		*(__ex_table)
#endif
		__stop___ex_table = .;

		/*
		 * and the usual data section
		 */
		DATA_DATA
		CONSTRUCTORS

		_edata = .;
	}
	_edata_loc = __data_loc + SIZEOF(.data);

#ifdef CONFIG_HAVE_TCM
        /*
	 * We align everything to a page boundary so we can
	 * free it after init has commenced and TCM contents have
	 * been copied to its destination.
	 */
	.tcm_start : {
		. = ALIGN(PAGE_SIZE);
		__tcm_start = .;
		__itcm_start = .;
	}

	/*
	 * Link these to the ITCM RAM
	 * Put VMA to the TCM address and LMA to the common RAM
	 * and we'll upload the contents from RAM to TCM and free
	 * the used RAM after that.
	 */
	.text_itcm ITCM_OFFSET : AT(__itcm_start)
	{
		__sitcm_text = .;
		*(.tcm.text)
		*(.tcm.rodata)
		. = ALIGN(4);
		__eitcm_text = .;
	}

	/*
	 * Reset the dot pointer, this is needed to create the
	 * relative __dtcm_start below (to be used as extern in code).
	 */
	. = ADDR(.tcm_start) + SIZEOF(.tcm_start) + SIZEOF(.text_itcm);

	.dtcm_start : {
		__dtcm_start = .;
	}

	/* TODO: add remainder of ITCM as well, that can be used for data! */
	.data_dtcm DTCM_OFFSET : AT(__dtcm_start)
	{
		. = ALIGN(4);
		__sdtcm_data = .;
		*(.tcm.data)
		. = ALIGN(4);
		__edtcm_data = .;
	}

	/* Reset the dot pointer or the linker gets confused */
	. = ADDR(.dtcm_start) + SIZEOF(.data_dtcm);

	/* End marker for freeing TCM copy in linked object */
	.tcm_end : AT(ADDR(.dtcm_start) + SIZEOF(.data_dtcm)){
		. = ALIGN(PAGE_SIZE);
		__tcm_end = .;
	}
#endif

	/* This is needed for L4Env --
	 *   copy out of l4/pkg/crtx/lib/src/ARCH-x86/main_stat.ld */
	.ctors :
	{
	  LONG(0)
	  /* traverse this list backwards (see __attribute__((init_priority)) */
	  KEEP (*(.mark_beg_ctors))
	  KEEP (*(.ctors))
	  KEEP (*(SORT(.ctors.*)))
	  KEEP (*(.mark_end_ctors))
	  /* traverse this list forwards */
	  KEEP (*(.mark_beg_c_ctors))
	  #define QM1c() .c_ctors.?
	  #define QM2c() .c_ctors.??
	  #define QM3c() .c_ctors.???
	  #define QM4c() .c_ctors.????
	  #define QM5c() .c_ctors.?????
	  KEEP (*(SORT(QM1c())))
	  KEEP (*(SORT(QM2c())))
	  KEEP (*(SORT(QM3c())))
	  KEEP (*(SORT(QM4c())))
	  KEEP (*(SORT(QM5c())))
	  KEEP (*(.c_ctors))
	  KEEP (*(.mark_end_c_ctors))
	}
	.dtors :
	{
	  LONG(0)
	  /* traverse this list backwards (see __attribute__((init_priority)) */
	  KEEP (*(.mark_beg_dtors))
	  KEEP (*(.dtors))
	  KEEP (*(SORT(.dtors.*)))
	  KEEP (*(.mark_end_dtors))
	  /* traverse this list forwards */
	  KEEP (*(.mark_beg_c_dtors))
	  #define QM1d() .c_dtors.?
	  #define QM2d() .c_dtors.??
	  #define QM3d() .c_dtors.???
	  #define QM4d() .c_dtors.????
	  #define QM5d() .c_dtors.?????
	  KEEP (*(SORT(QM1d())))
	  KEEP (*(SORT(QM2d())))
	  KEEP (*(SORT(QM3d())))
	  KEEP (*(SORT(QM4d())))
	  KEEP (*(SORT(QM5d())))
	  KEEP (*(.c_dtors))
	  KEEP (*(.mark_end_c_dtors))
	}

	/* BSS_SECTION(0, 0, 0) */
	/* The '. = ALIGN(stop_align);' somehow aligns down to a really small
	 * address (with GNU ld (Sourcery G++ Lite 2009q3-67)
	 * 2.19.51.20090709), ld 2.18 does it all right.
	 * The following works around that: */
	. = ALIGN(0);
	VMLINUX_SYMBOL(__bss_start) = .;
	SBSS(0)
	BSS(0)
	/* . = ALIGN(0); */
	VMLINUX_SYMBOL(__bss_stop) = .;
	/* Who knows why '. = ALIGN(...);' doesn't work here? Again, ld 2.18
	 * works fine */
	/* . = ALIGN(1048576); */
	_ldscript_align_marker = ALIGN(1048576);
	. = _ldscript_align_marker;

	_end = .;

	STABS_DEBUG

	PROVIDE(_prog_img_end = .);
	.comment 0 : { *(.comment) }

	/* Default discards */
	DISCARDS
}

/*
 * These must never be empty
 * If you have to comment these two assert statements out, your
 * binutils is too old (for other reasons as well)
 */
ASSERT((__proc_info_end - __proc_info_begin), "missing CPU support")
ASSERT((__arch_info_end - __arch_info_begin), "no machine record defined")
