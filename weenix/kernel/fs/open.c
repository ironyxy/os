/*
 *  FILE: open.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Mon Apr  6 19:27:49 1998
 */

#include "globals.h"
#include "errno.h"
#include "fs/fcntl.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/stat.h"
#include "util/debug.h"

/* find empty index in p->p_files[] */
int
get_empty_fd(proc_t *p)
{
        int fd;

        for (fd = 0; fd < NFILES; fd++) {
                if (!p->p_files[fd])
                        return fd;
        }

        dbg(DBG_ERROR | DBG_VFS, "ERROR: get_empty_fd: out of file descriptors "
            "for pid %d\n", curproc->p_pid);
        return -EMFILE;
}

/*
 * There a number of steps to opening a file:
 *      1. Get the next empty file descriptor.
 *      2. Call fget to get a fresh file_t.
 *      3. Save the file_t in curproc's file descriptor table.
 *      4. Set file_t->f_mode to OR of FMODE_(READ|WRITE|APPEND) based on
 *         oflags, which can be O_RDONLY, O_WRONLY or O_RDWR, possibly OR'd with
 *         O_APPEND.
 *      5. Use open_namev() to get the vnode for the file_t.
 *      6. Fill in the fields of the file_t.
 *      7. Return new fd.
 *
 * If anything goes wrong at any point (specifically if the call to open_namev
 * fails), be sure to remove the fd from curproc, fput the file_t and return an
 * error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        oflags is not valid.
 *      o EMFILE
 *        The process already has the maximum number of files open.
 *      o ENOMEM
 *        Insufficient kernel memory was available.
 *      o ENAMETOOLONG
 *        A component of filename was too long.
 *      o ENOENT
 *        O_CREAT is not set and the named file does not exist.  Or, a
 *        directory component in pathname does not exist.
 *      o EISDIR
 *        pathname refers to a directory and the access requested involved
 *        writing (that is, O_WRONLY or O_RDWR is set).
 *      o ENXIO
 *        pathname refers to a device special file and no corresponding device
 *        exists.
 */

int
do_open(const char *filename, int oflags)
{
        /* check invalid combinations */
        int wr = oflags & O_WRONLY;
        int rdwr = oflags & O_RDWR;
        int rd = 0;
        if( (wr == 0) && (rdwr == 0)){
            rd = 1;
        }
        int append = oflags & O_APPEND;
        int trunc = oflags &O_TRUNC;
        int creat = oflags & O_CREAT;

        if( (wr != 0) && (rdwr != 0) ){
            return -EINVAL;
        }
        
        /* 1.  Get the next empty file descriptor */
        int fd = get_empty_fd(curproc);
        if(fd == -EMFILE){
            return -EMFILE;
        }

        /* 2. Call fget to get a fresh file_t */
        file_t* file = fget(-1);
        if(file == NULL){
            /* not sure for this error */
            return -ENOMEM;
        }
        /* 3. Save the file_t in curproc's file descriptor table */
        curproc -> p_files[fd] = file;

        /* 4. Set file_t->f_mode to OR of FMODE_(READ|WRITE|APPEND) based on
         *    oflags, which can be O_RDONLY, O_WRONLY or O_RDWR, possibly OR'd with
         *    O_APPEND.
         */
        int mode = -1;

        if( (wr != 0) ||(rd != 0) || (rdwr != 0)){
            if(rd != 0){
                mode = FMODE_READ;
            }
            else if(wr != 0){
                mode = FMODE_WRITE;
            }
            else if(rdwr != 0){
                mode = FMODE_READ | FMODE_WRITE;
            }
            /* last check append */
            if( append != 0){
                mode |= FMODE_APPEND;
            }
        }

        if(mode == -1){
            /*  invalid combination */
            curproc -> p_files[fd] = NULL;
            fput(file);
            return -EINVAL;
        }

        file -> f_mode = mode;

        /* 5. Use open_namev() to get the vnode for the file_t. */
        vnode_t* res_vnode = NULL;
        vnode_t* base = curproc -> p_cwd;
        int res = open_namev(filename, oflags, &res_vnode, base);
        if(res < 0){
            curproc -> p_files[fd] = NULL;
            if(res_vnode)
                vput(res_vnode);
            fput(file);
            return res;/* including errors: ENAMETOOLONG, ENOTDIR, ENOENT */
        }

        if( ((res_vnode -> vn_mode & S_IFDIR) != 0) && (mode != FMODE_READ)){
            curproc -> p_files[fd] = NULL;
            vput(res_vnode);
            fput(file);
            return -EISDIR;
        }
        
        /* 6. Fill in the fields of the file_t */
        file->f_vnode = res_vnode;
        file->f_pos = 0;

        /* 7. return new fd */
        return fd;
        
}
