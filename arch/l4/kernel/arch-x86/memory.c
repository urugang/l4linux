#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/thread_info.h>
#include <linux/mm.h>
#include <linux/bootmem.h>

#include <asm-generic/sections.h>
#include <asm/e820.h>

#include <asm/generic/setup.h>

#include <l4/sys/kdebug.h>

char * __init l4x_memory_setup(void)
{
	unsigned long mem_start, mem_size, isa_start, isa_size;

	setup_l4x_memory(boot_command_line, &mem_start, &mem_size,
	                 &isa_start, &isa_size);

	max_pfn_mapped = (mem_start + mem_size + ((1 << 12) - 1)) >> 12;

	e820.nr_map = 0;

        /* minimum 2 pages required */
        e820_add_region(0, PAGE_SIZE, E820_RAM);
	if (isa_size)
		e820_add_region(isa_start, isa_size, E820_RAM);
	if ((unsigned long)&_end > mem_start)
		printk("Uh, something looks strange.\n");
	e820_add_region((unsigned long)&_stext, (unsigned long)&_end, E820_RAM);
	e820_add_region((unsigned long)&_end, mem_start, E820_UNUSABLE);
	e820_add_region(mem_start, mem_size, E820_RAM);

	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);

	return "L4Lx-Memory";
}
