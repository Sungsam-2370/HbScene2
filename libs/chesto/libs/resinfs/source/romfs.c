/*
 * romfs.c
 * romfs library functions
 * Copyright (C) 2019 rw-r-r-0644
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/dirent.h>
#include <sys/iosupport.h>
#include <sys/param.h>
#include <stdint.h>
#include <unistd.h>

#if defined(__WIIU__)
#include <coreinit/memory.h>
#endif


/* include the implementation files (needed for static functions) */
#include "romfs_tree.c.impl"
#include "romfs_devoptab.c.impl"
#include "romfs_tar.c.impl"


/* romfs binary symbols */
extern char _binary_romfs_tar_start[];
extern char _binary_romfs_tar_end[];

/* romfs initialization flag */
static int32_t romfs_initialised = 0;

/* ramfsInit: intialize romfs */
int32_t ramfsInit(void)
{
	/* already initialized */
	if (romfs_initialised)
		return 0;

	/* create romfs file entries */
	tar_create_entries(_binary_romfs_tar_start, _binary_romfs_tar_end);

	/* add the romfs devoptab to devices list */
	if (AddDevice(&romfs_devoptab) == -1)
	{
		node_destroytree(NULL, 0);
		return -1;
	}

	/* set the intialized flag */
	romfs_initialised = 1;

	return 0;
}

/* ramfsExit: exit romfs */
int32_t ramfsExit(void)
{
	/* never initialized */
	if (!romfs_initialised)
		return -1;

	/* remove the romfs devoptab from devices list */
	RemoveDevice("resin:");

	/* deallocate the file tree */
	node_destroytree(NULL, 0);

	/* clear the initialized flag */
	romfs_initialised = 0;

	return 0;
}
