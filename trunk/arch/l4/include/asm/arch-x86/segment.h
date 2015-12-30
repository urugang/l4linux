#ifndef __ASM_L4__ARCH_I386__SEGMENT_H__
#define __ASM_L4__ARCH_I386__SEGMENT_H__

/* Just modify one value */

#include <asm-x86/segment.h>

#ifndef __ASSEMBLY__

extern unsigned l4x_fiasco_gdt_entry_offset;

#undef  GDT_ENTRY_TLS_MIN
#define GDT_ENTRY_TLS_MIN	l4x_fiasco_gdt_entry_offset

#ifdef CONFIG_X86_64
extern unsigned l4x_fiasco_user32_cs;
extern unsigned l4x_fiasco_user_cs;
extern unsigned l4x_fiasco_user_ds;

#undef loadsegment
#define loadsegment(seg, v) l4x_current_vcpu()->arch_state.user_##seg = (v)

#undef savesegment
#define savesegment(seg, v) (v) = l4x_current_vcpu()->arch_state.user_##seg

#undef __USER32_DS
#define __USER32_DS (l4x_fiasco_user_ds)

#undef __USER_DS
#define __USER_DS __USER32_DS

#undef __USER32_CS
#define  __USER32_CS (l4x_fiasco_user32_cs)

#undef __USER_CS
#define  __USER_CS (l4x_fiasco_user_cs)
#endif /* X86_64 */
#endif /* __ASSEMBLY__ */

#endif /* ! __ASM_L4__ARCH_I386__SEGMENT_H__ */
