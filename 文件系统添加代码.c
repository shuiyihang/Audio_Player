FRESULT f_opendir_cluster (
	FF_DIR* dp,			/* Pointer to directory object to create */
	const TCHAR* path,	/* Pointer to the directory path */
	DWORD cluster,		/* Pointer to object cluster */
	DWORD blk_ofs		/* Pointer to object blk_ofs */
)
{
	FRESULT res;
	FATFS *fs;
	FFOBJID *obj;
	DEF_NAMBUF

	if (!dp) return FR_INVALID_OBJECT;

	/* Get logical drive */
	obj = &dp->obj;
	res = find_volume(&path, &fs, 0);
	if (res == FR_OK) {
		obj->fs = fs;
		INIT_NAMBUF(fs);

		obj->id = fs->id;
		dp->obj.sclust = cluster;
		dp->blk_ofs = blk_ofs;
		res = dir_sdi(dp, dp->blk_ofs);			/* Rewind directory */
#if FF_FS_LOCK != 0
		if (obj->sclust) {
			obj->lockid = inc_lock(dp, 0);	/* Lock the sub directory */
			if (!obj->lockid) res = FR_TOO_MANY_OPEN_FILES;
		} else {
			obj->lockid = 0;	/* Root directory need not to be locked */
		}
#endif
		FREE_NAMBUF();
	} else {
		obj->fs = 0;	/* Invalidate the directory object if function faild */
	}
	LEAVE_FF(fs, res);
}


/*-----------------------------------------------------------------------*/
/* Open a File by cluster                                                */
/*-----------------------------------------------------------------------*/

FRESULT f_open_cluster(
	FIL* fp,			/* Pointer to the blank file object */
	const TCHAR* path,	/* Pointer to the directory path */
	DWORD cluster,		/* Pointer to object cluster */
	DWORD blk_ofs,		/* Pointer to object blk_ofs */
	BYTE mode			/* Access mode and file open mode flags */
)
{
	FRESULT res;
	FF_DIR dj;
	FATFS *fs;
#if !FF_FS_READONLY
	DWORD bcs, clst, sc;
	FSIZE_t ofs;
#endif
	DEF_NAMBUF


	if (!fp) return FR_INVALID_OBJECT;

	/* Get logical drive */
	mode &= FF_FS_READONLY ? FA_READ : FA_READ | FA_WRITE | FA_CREATE_ALWAYS | FA_CREATE_NEW | FA_OPEN_ALWAYS | FA_OPEN_APPEND | FA_SEEKEND;
	res = find_volume(&path, &fs, mode);
	if (res == FR_OK) {
		dj.obj.fs = fs;
		INIT_NAMBUF(fs);
		dj.obj.sclust = cluster;
		dj.blk_ofs = blk_ofs;
		res = dir_sdi(&dj, dj.blk_ofs);		/* Rewind directory */
		if (res == FR_OK) {
			res = dir_read(&dj, 0);			/* Read an item */
		#if !FF_FS_READONLY					/* R/W configuration */
			if (res == FR_OK) {
			#if _FS_LOCK != 0
				res = chk_lock(&dj, (mode & ~FA_READ) ? 1 : 0);
			#endif
			}

			if (res == FR_OK) {					/* Following succeeded */
				if (dj.obj.attr & AM_DIR) {		/* It is a directory */
					res = FR_NO_FILE;
				} else {
					if ((mode & FA_WRITE) && (dj.obj.attr & AM_RDO)) { /* R/O violation */
						res = FR_DENIED;
					}
				}
			}

			if (res == FR_OK) {
				if (mode & FA_CREATE_ALWAYS)		/* Set file change flag if created or overwritten */
					mode |= FA_MODIFIED;
				fp->dir_sect = fs->winsect; 		/* Pointer to the directory entry */
				fp->dir_ptr = dj.dir;
			#if _FS_LOCK != 0
				fp->obj.lockid = inc_lock(&dj, (mode & ~FA_READ) ? 1 : 0);
				if (!fp->obj.lockid) res = FR_INT_ERR;
			#endif
			}

		#else		/* R/O configuration */
			if (res == FR_OK) {
				if (dj.obj.attr & AM_DIR) { 	/* It is a directory */
					res = FR_NO_FILE;
				}
			}
		#endif
		}

		if (res == FR_OK) {
		#if FF_FS_EXFAT
			if (fs->fs_type == FS_EXFAT) {
				fp->obj.sclust = ld_dword(fs->dirbuf + XDIR_FstClus);		/* Get allocation info */
				fp->obj.objsize = ld_qword(fs->dirbuf + XDIR_FileSize);
				fp->obj.stat = fs->dirbuf[XDIR_GenFlags] & 2;
				fp->obj.c_scl = dj.obj.sclust;
				fp->obj.c_size = ((DWORD)dj.obj.objsize & 0xFFFFFF00) | dj.obj.stat;
				fp->obj.c_ofs = dj.blk_ofs;
			} else
		#endif
			{
				fp->obj.sclust = ld_clust(fs, dj.dir);				/* Get allocation info */
				fp->obj.objsize = ld_dword(dj.dir + DIR_FileSize);
			}
		#if FF_USE_FASTSEEK
			fp->cltbl = 0;			/* Disable fast seek mode */
		#endif
			fp->obj.fs = fs;	 	/* Validate the file object */
			fp->obj.id = fs->id;
			fp->flag = mode;		/* Set file access mode */
			fp->err = 0;			/* Clear error flag */
			fp->sect = 0;			/* Invalidate current data sector */
			fp->fptr = 0;			/* Set file pointer top of the file */
	#if !FF_FS_READONLY
		#if !FF_FS_TINY
			mem_set(fp->buf, 0, FF_MAX_SS);	/* Clear sector buffer */
		#endif
			if ((mode & FA_SEEKEND) && fp->obj.objsize > 0) {	/* Seek to end of file if FA_OPEN_APPEND is specified */
				fp->fptr = fp->obj.objsize;			/* Offset to seek */
				bcs = (DWORD)fs->csize * SS(fs);	/* Cluster size in byte */
				clst = fp->obj.sclust;				/* Follow the cluster chain */
				for (ofs = fp->obj.objsize; res == FR_OK && ofs > bcs; ofs -= bcs) {
					clst = get_fat(&fp->obj, clst);
					if (clst <= 1) res = FR_INT_ERR;
					if (clst == 0xFFFFFFFF) res = FR_DISK_ERR;
				}
				fp->clust = clst;
				if (res == FR_OK && ofs % SS(fs)) {	/* Fill sector buffer if not on the sector boundary */
					if ((sc = clst2sect(fs, clst)) == 0) {
						res = FR_INT_ERR;
					} else {
						fp->sect = sc + (DWORD)(ofs / SS(fs));
					#if !_FS_TINY
						if (disk_read(fs->pdrv, fp->buf, fp->sect, 1) != RES_OK) res = FR_DISK_ERR;
					#endif
					}
				}
			}
	#endif
		}
		FREE_NAMBUF();
	}

	if (res != FR_OK) fp->obj.fs = 0;	/* Invalidate file object on error */

	LEAVE_FF(fs, res);

}