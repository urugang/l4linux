#ifndef __ASM_L4__ARCH_I386__ELF_H__
#define __ASM_L4__ARCH_I386__ELF_H__

/* L4Linux has a Linux native ABI... */
#include <asm-x86/elf.h>

/* ...but dosen't have segment registers */
#undef ELF_CORE_COPY_REGS
#define ELF_CORE_COPY_REGS(pr_reg, regs)		\
	pr_reg[0] = regs->bx;				\
	pr_reg[1] = regs->cx;				\
	pr_reg[2] = regs->dx;				\
	pr_reg[3] = regs->si;				\
	pr_reg[4] = regs->di;				\
	pr_reg[5] = regs->bp;				\
	pr_reg[6] = regs->ax;				\
	pr_reg[7] = 0; /* fake ds */			\
	pr_reg[8] = 0; /* fake es */			\
	pr_reg[9] = regs->fs;				\
	pr_reg[10] = 0; /* fake gs */			\
	pr_reg[11] = regs->orig_ax;			\
	pr_reg[12] = regs->ip;				\
	pr_reg[13] = 0; /* fake cs */			\
	pr_reg[14] = regs->flags;			\
	pr_reg[15] = regs->sp;				\
	pr_reg[16] = 0; /* fake ss */

#include <asm/api/config.h>

#undef VDSO_HIGH_BASE
#define VDSO_HIGH_BASE (UPAGE_USER_ADDRESS)

#undef VDSO_SYM
#define VDSO_SYM(x) ((unsigned long)(x))

#endif /* ! __ASM_L4__ARCH_I386__ELF_H__ */
