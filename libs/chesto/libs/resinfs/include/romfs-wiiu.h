#ifndef _ROMFS_WIIU_H
#define _ROMFS_WIIU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
	ramfsInit: mount romfs
	returns 0 when successful or a negative value for errors
*/
int32_t ramfsInit(void);

/*
	ramfsExit: unmount romfs
	returns 0 when successful or a negative value for errors
*/
int32_t ramfsExit(void);


#ifdef __cplusplus
}
#endif

#endif
