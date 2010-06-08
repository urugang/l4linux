#ifndef __ASM_L4__GENERIC__DEVS_H__
#define __ASM_L4__GENERIC__DEVS_H__

void l4x_arm_devices_init(void);
int l4x_register_platform_data(const char *l4io_name,
                               const char *linux_name,
                               void *platformdata);

#endif /* ! __ASM_L4__GENERIC__DEVS_H__ */
