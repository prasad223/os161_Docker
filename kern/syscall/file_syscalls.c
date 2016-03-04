/**
* The file containing the system-calls implementation of the following
* 1. open
* 2. close
* 3. read
* 4. write
* 5. dup2
* 6. chdir
* 7. getcwd
**/

#include <kern/file_syscalls.h>
#include <vnode.h>
#include <vfs.h>
#include <lib.h>
#include <synch.h>
#include <kern/fcntl.h>
#include <kern/errno.h>

/**
*  This is called by runprogram.c and we are creating three file descriptors for STDIN, STDOUT, STDERR
* and save them in positions 0, 1 and 2, respectively.
**/
int
init_file_descriptor(void) {
  struct vnode *vin;
  struct vnode *vout;
  struct vnode *verr;

  char *in    = NULL;
  char *out   = NULL;
  char *err   = NULL;
  in          = kstrdup("con:");
  out         = kstrdup("con:");
  err         = kstrdup("con:");

  if (vfs_open(in,O_RDONLY,0, &vin)) {
    kfree(in);
    kfree(out);
    kfree(err);
    return EINVAL; //still deciding on error return values

  }
  /**Allocate memory for Fdesc 0 **/
  curthread->t_fdtable[0] = (struct file_descriptor*)kmalloc(sizeof(struct file_descriptor));
  strcpy(curthread->t_fdtable[0]->fileName,"con:");
  curthread->t_fdtable[0]->vn        = vin;
  curthread->t_fdtable[0]->openFlags = O_RDONLY;
  curthread->t_fdtable[0]->refCount  = 1; // ref count set to 1, as it is only init once, and passed around after that
  curthread->t_fdtable[0]->lk        = lock_create(in);
  curthread->t_fdtable[0]->offset    = 0;

  if (vfs_open(out,O_WRONLY,0, &vout)) {
    kfree(in);
    kfree(out);
    kfree(err);
    return EINVAL; //still deciding on error return values

  }
  /**Allocate memory for Fdesc 1 **/
  curthread->t_fdtable[1] = kmalloc(sizeof(struct file_descriptor));
  strcpy(curthread->t_fdtable[1]->fileName,"con:");
  curthread->t_fdtable[1]->vn        = vout;
  curthread->t_fdtable[1]->openFlags = O_WRONLY;
  curthread->t_fdtable[1]->refCount  = 1; // ref count set to 1, as it is only init once, and passed around after that
  curthread->t_fdtable[1]->lk        = lock_create(out);
  curthread->t_fdtable[1]->offset = 0;

  if (vfs_open(err,O_WRONLY,0, &verr)) {
    kfree(in);
    kfree(out);
    kfree(err);
    return EINVAL; //still deciding on error return values
  }

  /**Allocate memory for Fdesc 1 **/
  curthread->t_fdtable[2] = kmalloc(sizeof(struct file_descriptor));
  strcpy(curthread->t_fdtable[2]->fileName,"con:");
  curthread->t_fdtable[2]->vn        = verr;
  curthread->t_fdtable[2]->openFlags = O_WRONLY;
  curthread->t_fdtable[2]->refCount  = 1; // ref count set to 1, as it is only init once, and passed around after that
  curthread->t_fdtable[2]->lk        = lock_create(err);
  curthread->t_fdtable[2]->offset    = 0;

  return 0;
}

/** --------------------------------------------------------------------------**/
/** Remember, in case of failure, the return code must indicate error number
In case of success, save the success status in retval**/

/** System call for opening the file name provided with given flags & modes **/
int
sys_open(const char *filename, int flags, mode_t mode, int *retval) {

}

/** System call for closing the file handle **/
int
sys_close(int fHandle,int *retval) {
  int result = 1;
  if (fHandle > OPEN_MAX || fHandle < 2) { // TODO : Clarify if fHandle should accept 0,1,2
    *retval = 1; // for any future uses
    return EBADF;
  }

  if (curthread->t_fdtable[fHandle] == NULL) {
    *retval = 1; // for any future uses
    return EBADF;
  }
  if (curthread->t_fdtable[fHandle]->vn == NULL) {
    *retval = 1; // for any future uses
    return EBADF;
  }
  curthread->t_fdtable[fHandle]->refCount = curthread->t_fdtable[fHandle]->refCount-1;
  if (curthread->t_fdtable[fHandle]->refCount == 1) {
    vfs_close(curthread->t_fdtable[fHandle]->vn);
    lock_destroy(curthread->t_fdtable[fHandle]->lk);
    curthread->t_fdtable[fHandle]->openFlags = 0; // probably not required, but I like to clean up
    curthread->t_fdtable[fHandle]->offset    = 0;
    kfree(curthread->t_fdtable[fHandle]);
    curthread->t_fdtable[fHandle] = NULL;
  }
  *retval = 0;
  result  = 0;
  return result;
}
