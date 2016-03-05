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
#include <lib.h>
#include <test.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <copyinout.h>
#include <kern/stat.h>

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
  if (curthread->t_fdtable[0] == NULL) { /** failed to allocate memory **/
    kfree(in);
    kfree(out);
    kfree(err);
    vfs_close(vin);
    return EFAULT;
  }
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
    lock_destroy(curthread->t_fdtable[0]->lk);
    vfs_close(curthread->t_fdtable[0]->vn);
    kfree(curthread->t_fdtable[0]);
    curthread->t_fdtable[0] = NULL;
    return EINVAL; //still deciding on error return values

  }
  /**Allocate memory for Fdesc 1 **/
  curthread->t_fdtable[1] = (struct file_descriptor*)kmalloc(sizeof(struct file_descriptor));
  if (curthread->t_fdtable[1] == NULL) { /** failed to allocate memory **/
    kfree(in);
    kfree(out);
    kfree(err);
    lock_destroy(curthread->t_fdtable[0]->lk);
    vfs_close(curthread->t_fdtable[0]->vn);
    kfree(curthread->t_fdtable[0]);
    curthread->t_fdtable[0] = NULL;

    vfs_close(vout);
    return EFAULT;
  }
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
    lock_destroy(curthread->t_fdtable[0]->lk);
    vfs_close(curthread->t_fdtable[0]->vn);
    kfree(curthread->t_fdtable[0]);
    curthread->t_fdtable[0]= NULL;

    lock_destroy(curthread->t_fdtable[1]->lk);
    vfs_close(curthread->t_fdtable[1]->vn);
    kfree(curthread->t_fdtable[1]);
    curthread->t_fdtable[1]= NULL;
    return EINVAL; //still deciding on error return values
  }

  /**Allocate memory for Fdesc 1 **/
  curthread->t_fdtable[2] = (struct file_descriptor*)kmalloc(sizeof(struct file_descriptor));
  if (curthread->t_fdtable[2] == NULL) { /** failed to allocate memory **/
    kfree(in);
    kfree(out);
    kfree(err);

    lock_destroy(curthread->t_fdtable[0]->lk);
    vfs_close(curthread->t_fdtable[0]->vn);
    kfree(curthread->t_fdtable[0]);
    curthread->t_fdtable[0]= NULL;

    lock_destroy(curthread->t_fdtable[1]->lk);
    vfs_close(curthread->t_fdtable[1]->vn);
    kfree(curthread->t_fdtable[1]);
    curthread->t_fdtable[1]= NULL;
    return EFAULT;
  }
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
  int index = 0, result;
  char* kbuff;
  struct vnode* vn;
  int readMode = 0;
  struct stat file_stat;
  (void)mode; // suppress warning, mode is unused

  kbuff = (char *)kmalloc(sizeof(char)*PATH_MAX);
  if (kbuff == NULL) {
    kprintf_n("Could not allocate kbuff to sys_open\n");
    return EFAULT;
  }

  if (copyin((const_userptr_t) filename, kbuff, PATH_MAX) ) {
    kprintf_n("Could not copy the filename to kbuff\n");
    return EFAULT; /* filename was an invalid pointer */
  }
  //check the flags
  if (flags < 0) {
    kprintf_n("\n");
    return EINVAL;
  }
  if (flags && O_RDONLY == O_RDONLY) {
    readMode  = 1;
  }
  if (flags && O_WRONLY == O_WRONLY) {
    if (readMode == 1)
    {
      kprintf_n("Both O_RDONLY and O_WRONLY flags passed\n");
      return EINVAL;
    }
  }
  if (readMode == 1 ) { // invalid flags with read_mode
    if ((flags && O_CREAT == O_CREAT) ||
        (flags && O_EXCL == O_EXCL)   ||
        (flags && O_TRUNC == O_TRUNC) ||
        (flags && O_APPEND == O_APPEND)) {
      kprintf_n("Invalid flags passed with read mode\n");
      return EINVAL;
    }
  }
  if ( (flags && O_EXCL == O_EXCL) && (flags && O_CREAT != O_CREAT) ) { // O_EXCL makes sense only with O_CREAT
    kprintf_n("flag combinations wrong ! O_EXCL passed, but not O_CREAT");
    return EINVAL;
  }
  /** **/
  while(curthread->t_fdtable[index] != NULL) {
    index++;
  }
  if ( index == OPEN_MAX) {
    *retval = 1; // error
    return EMFILE; /* Too many open files */
  }
  // create the vnode
  if (vfs_open(kbuff,flags,0, &vn)) {
    kprintf_n("Could not open vnode for sys_open\n");
    kfree(kbuff);
    return EFAULT;
  }
  curthread->t_fdtable[index] = (struct file_descriptor*)kmalloc(sizeof(struct file_descriptor));
  if (curthread->t_fdtable[index] == NULL) {
    kprintf_n("Could not create new file descriptor in sys_open\n");
    kfree(kbuff);
    vfs_close(vn);
    return EFAULT;
  }
  strcpy(curthread->t_fdtable[index]->fileName, kbuff);
  curthread->t_fdtable[index]->vn        = vn;
  curthread->t_fdtable[index]->openFlags = flags;
  curthread->t_fdtable[index]->refCount  = 1; // ref count set to 1, as it is only init once, and passed around after that
  curthread->t_fdtable[index]->lk        = lock_create(kbuff); // never trust user buffers, always use kbuff
  if (flags && O_APPEND == O_APPEND) { //set offset to end of file
    result = VOP_STAT(curthread->t_fdtable[index]->vn,&file_stat);
    if (result) {
      kprintf_n("Unable to stat file for getting offset\n");
      kfree(kbuff);
      lock_destroy(curthread->t_fdtable[index]->lk);
      vfs_close(curthread->t_fdtable[index]->vn);
      kfree(curthread->t_fdtable[index]);
      curthread->t_fdtable[index] = NULL;
      return EFAULT;
    }
    curthread->t_fdtable[index]->offset    = file_stat.st_size; // gives file size ** TODO : Test this ** /
  } else {
    curthread->t_fdtable[index]->offset    = 0;
  }

  *retval = index; /** return file handle on success**/
  kfree(kbuff);
  return 0;
}

/** System call for closing the file handle **/
int
sys_close(int fHandle,int *retval) {
  int result = 1;
  if (fHandle > OPEN_MAX || fHandle < 0) {
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
