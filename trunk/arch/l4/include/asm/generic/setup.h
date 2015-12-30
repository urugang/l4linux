#ifndef __ASM_L4__GENERIC__SETUP_H__
#define __ASM_L4__GENERIC__SETUP_H__

#include <linux/thread_info.h>
#include <l4/sys/kip.h>

extern l4_kernel_info_t *l4lx_kinfo;

extern unsigned int l4x_kernel_taskno;

void l4x_setup_memory(char *cmdl,
                      unsigned long *main_mem_start,
                      unsigned long *main_mem_size);

void l4x_v2p_add_item(l4_addr_t phys, void *virt, l4_size_t size);
unsigned long l4x_v2p_del_item(void *virt);

void l4x_free_initrd_mem(void);
void __init l4x_load_initrd(const char *command_line);
#ifdef CONFIG_OF
unsigned long __init l4x_load_dtb(const char *command_line,
                                  unsigned long offset);
#endif

void l4x_prepare_irq_thread(struct thread_info *ti, unsigned _cpu);

void __noreturn l4x_exit_l4linux(void);
void __noreturn l4x_exit_l4linux_msg(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

void l4x_platform_shutdown(unsigned reboot);

int atexit(void (*f)(void));

#endif /* ! __ASM_L4__GENERIC__SETUP_H__ */
