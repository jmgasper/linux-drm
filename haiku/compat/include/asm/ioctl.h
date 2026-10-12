#ifndef _ASM_X86_IOCTL_H
#define _ASM_X86_IOCTL_H
/* Linux's ioctl number encoding; DRM's userland on Haiku uses the same */
#define _IOC_TYPECHECK(t)	(sizeof(t))
#include <asm-generic/ioctl.h>
#endif
