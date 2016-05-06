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
#include <uio.h>
#include <proc.h>
#include <kern/seek.h>
/**
*  This is called by runprogram.c and we are creating three file descriptors for STDIN, STDOUT, STDERR
* and save them in positions 0, 1 and 2, respectively.
**/
int
init_file_descriptor(void) {
  struct vnode *vin = NULL;
  struct vnode *vout= NULL;
  struct vnode *verr= NULL;

  char *in    = NULL;
  char *out   = NULL;
  char *err   = NULL;
  in          = kstrdup("con:");
  out         = kstrdup("con:");
  err         = kstrdup("con:");

  if (vfs_open(in,O_RDONLY,0664, &vin)) {
    kfree(in);
    kfree(out);
    kfree(err);
    return EINVAL; //still deciding on error return values

  }
/**Allocate memory for Fdesc 0 **/
  curproc->t_fdtable[0] = (struct file_descriptor*)kmalloc(sizeof(struct file_descriptor));
  if (curproc->t_fdtable[0] == NULL) { /** failed to allocate memory **/
    kfree(in);
    kfree(out);
    kfree(err);
    vfs_close(vin);
    return EFAULT;
  }
  strcpy(curproc->t_fdtable[0]->fileName,"con:");
  curproc->t_fdtable[0]->vn        = vin;
  curproc->t_fdtable[0]->openFlags = O_RDONLY;
  curproc->t_fdtable[0]->refCount  = 1; // ref count set to 1, as it is only init once, and passed around after that
  curproc->t_fdtable[0]->lk        = lock_create(in);
  curproc->t_fdtable[0]->offset    = 0;

  if (vfs_open(out,O_WRONLY,0664, &vout)) {
    kfree(in);
    kfree(out);
    kfree(err);
    lock_destroy(curproc->t_fdtable[0]->lk);
    vfs_close(curproc->t_fdtable[0]->vn);
    kfree(curproc->t_fdtable[0]);
    curproc->t_fdtable[0] = NULL;
    return EINVAL; //still deciding on error return values

  }
  /**Allocate memory for Fdesc 1 **/
  curproc->t_fdtable[1] = (struct file_descriptor*)kmalloc(sizeof(struct file_descriptor));
  if (curproc->t_fdtable[1] == NULL) { /** failed to allocate memory **/
    kfree(in);
    kfree(out);
    kfree(err);
    lock_destroy(curproc->t_fdtable[0]->lk);
    vfs_close(curproc->t_fdtable[0]->vn);
    kfree(curproc->t_fdtable[0]);
    curproc->t_fdtable[0] = NULL;

    vfs_close(vout);
    return EFAULT;
  }
  strcpy(curproc->t_fdtable[1]->fileName,"con:");
  curproc->t_fdtable[1]->vn        = vout;
  curproc->t_fdtable[1]->openFlags = O_WRONLY;
  curproc->t_fdtable[1]->refCount  = 1; // ref count set to 1, as it is only init once, and passed around after that
  curproc->t_fdtable[1]->lk        = lock_create(out);
  curproc->t_fdtable[1]->offset    = 0;

  if (vfs_open(err,O_WRONLY,0664, &verr)) {
    kfree(in);
    kfree(out);
    kfree(err);
    lock_destroy(curproc->t_fdtable[0]->lk);
    vfs_close(curproc->t_fdtable[0]->vn);
    kfree(curproc->t_fdtable[0]);
    curproc->t_fdtable[0]= NULL;

    lock_destroy(curproc->t_fdtable[1]->lk);
    vfs_close(curproc->t_fdtable[1]->vn);
    kfree(curproc->t_fdtable[1]);
    curproc->t_fdtable[1]= NULL;
    return EINVAL; //still deciding on error return values
  }

  /**Allocate memory for Fdesc 1 **/
  curproc->t_fdtable[2] = (struct file_descriptor*)kmalloc(sizeof(struct file_descriptor));
  if (curproc->t_fdtable[2] == NULL) { /** failed to allocate memory **/
    kfree(in);
    kfree(out);
    kfree(err);

    lock_destroy(curproc->t_fdtable[0]->lk);
    vfs_close(curproc->t_fdtable[0]->vn);
    kfree(curproc->t_fdtable[0]);
    curproc->t_fdtable[0]= NULL;

    lock_destroy(curproc->t_fdtable[1]->lk);
    vfs_close(curproc->t_fdtable[1]->vn);
    kfree(curproc->t_fdtable[1]);
    curproc->t_fdtable[1]= NULL;
    return EFAULT;
  }
  strcpy(curproc->t_fdtable[2]->fileName,"con:");
  curproc->t_fdtable[2]->vn        = verr;
  curproc->t_fdtable[2]->openFlags = O_WRONLY;
  curproc->t_fdtable[2]->refCount  = 1; // ref count set to 1, as it is only init once, and passed around after that
  curproc->t_fdtable[2]->lk        = lock_create(err);
  curproc->t_fdtable[2]->offset    = 0;

  kfree(in);
  kfree(out);
  kfree(err);
  return 0;
}

/** --------------------------------------------------------------------------**/
int
check_isFileHandleValid(int fHandle) {
  if (fHandle >= OPEN_MAX || fHandle < 0) {
    return EBADF;
  }

  if (curproc->t_fdtable[fHandle] == NULL) {
    return EBADF;
  }
  return 0;
}
/** Remember, in case of failure, the return code must indicate error number
In case of success, save the success status in retval**/

/** System call for opening the file name provided with given flags & modes **/
int
sys_open(const char *filename, int flags, mode_t mode, int *retval) {
  int index = 0,result;
  char* kbuff;
  struct vnode* vn;
  //int readMode = 0;
  struct stat file_stat;
  (void)mode; // suppress warning, mode is unused

  kbuff = (char *)kmalloc(sizeof(char)*PATH_MAX);
  if (kbuff == NULL) {
    kprintf_n("Could not allocate kbuff to sys_open\n");
    return EFAULT;
  }
  result = copyin((const_userptr_t) filename, kbuff, PATH_MAX);
  if (result) {
//    kprintf_n("%p",filename);
    kfree(kbuff);
    //kprintf_n("Could not copy the filename to kbuff error is %d\n",result);
    return EFAULT; /* filename was an invalid pointer */
  }
  //check the flags
  if (flags < 0) {
    kfree(kbuff);
    kprintf_n("Flags cannot be negative\n");
    return EINVAL;
  }

  /** **/
  while(curproc->t_fdtable[index] != NULL) {
    index++;
  }
  if ( index == OPEN_MAX) {
    *retval = 1; // error
    kfree(kbuff);
    return EMFILE; /* Too many open files */
  }
  // create the vnode
  result = vfs_open(kbuff,flags,0664, &vn);
  if (result) {
    kprintf_n("flags %d",flags  );
    kprintf_n("Could not open vnode for sys_open\n");
    kfree(kbuff);
    return result;
  }
  curproc->t_fdtable[index] = (struct file_descriptor*)kmalloc(sizeof(struct file_descriptor));
  if (curproc->t_fdtable[index] == NULL) {
    kprintf_n("Could not create new file descriptor in sys_open\n");
    kfree(kbuff);
    vfs_close(vn);
    return EFAULT;
  }
  strcpy(curproc->t_fdtable[index]->fileName, kbuff);
  curproc->t_fdtable[index]->vn        = vn;
  curproc->t_fdtable[index]->openFlags = flags;
  curproc->t_fdtable[index]->refCount  = 1; // ref count set to 1, as it is only init once, and passed around after that
  curproc->t_fdtable[index]->lk        = lock_create(kbuff); // never trust user buffers, always use kbuff
  if (flags && O_APPEND == O_APPEND) { //set offset to end of file
    /*file_stat = (struct stat *)kmalloc(sizeof(struct stat));
    if (file_stat == NULL) {
      kprintf_n("Unable to allocate memory for stat\n");
      kfree(kbuff);
      lock_destroy(curproc->t_fdtable[index]->lk);
      vfs_close(curproc->t_fdtable[index]->vn);
      kfree(curproc->t_fdtable[index]);
      curproc->t_fdtable[index] = NULL;
      return EFAULT;
    }*/
    result = VOP_STAT(curproc->t_fdtable[index]->vn,&file_stat);
    if (result) {
      kprintf_n("Unable to stat file for getting offset\n");
      kfree(kbuff);
      //kfree(file_stat);
      lock_destroy(curproc->t_fdtable[index]->lk);
      vfs_close(curproc->t_fdtable[index]->vn);
      kfree(curproc->t_fdtable[index]);
      curproc->t_fdtable[index] = NULL;
      return EFAULT;
    }
    curproc->t_fdtable[index]->offset    = file_stat.st_size; // gives file size ** TODO : Test this ** /
  } else {
    curproc->t_fdtable[index]->offset    = 0;
  }

  *retval = index; /** return file handle on success**/
  kfree(kbuff);
  //kfree(file_stat);
  return 0;
}

/** System call for closing the file handle **/
int
sys_close(int fHandle,int *retval) {
  int result = 1;
  result = check_isFileHandleValid(fHandle);
  if (result > 0) {
    kprintf_n("file handle passed in not valid in sys_close!\n");
    return result;
  }
  if (curproc->t_fdtable[fHandle]->vn == NULL) {
    //*retval = 1; // for any future uses
    kprintf_n("vnode is NULL in sys_close \n");
    return EBADF;
  }
  curproc->t_fdtable[fHandle]->refCount = curproc->t_fdtable[fHandle]->refCount-1;
  if (curproc->t_fdtable[fHandle]->refCount == 0) {
    vfs_close(curproc->t_fdtable[fHandle]->vn);
    lock_destroy(curproc->t_fdtable[fHandle]->lk);
    curproc->t_fdtable[fHandle]->refCount  = 0;
    curproc->t_fdtable[fHandle]->openFlags = 0; // probably not required, but I like to clean up
    curproc->t_fdtable[fHandle]->offset    = 0;
    kfree(curproc->t_fdtable[fHandle]);
    curproc->t_fdtable[fHandle] = NULL;
  }
  *retval = 0;
  result  = 0;
  return result;
}

/** Function similiar to uio_kinit, only difference is this will initialize a uio for usage with user-space
* Important parameters are detailed below :
* iov_ubase : Points to a user-supplied buffer
* uio_segflg: UIO_USERSPACE to show we are dealing with userspace data
* uio_offset: This will be supplied by the File descriptor calling the funcition. The file descriptor will supply its own
*             offset value here
* uio_resid : The amount of data to transfer
* uio_space : current process's address space
*/
void
uio_uinit(struct iovec *iov, struct uio *uio,
	       void *kbuff, size_t len, off_t pos, enum uio_rw rw) {
  iov->iov_ubase = (userptr_t)kbuff;
  iov->iov_len   = len;
  uio->uio_iov   = iov;
  uio->uio_iovcnt= 1;
  uio->uio_segflg= UIO_USERSPACE;
  uio->uio_rw    = rw;
  uio->uio_offset= pos;
  uio->uio_resid = len;
  uio->uio_space = curproc->p_addrspace;
}
/** system call function for  sys_write
* Note : ssize_t is a typecast for int
*/
ssize_t
sys_write(int fd, const void *buf, size_t nbytes, int *retval) {
  int result;
  result = check_isFileHandleValid(fd);
  if (result > 0) {
    kprintf_n("File handle is invalid in sys_write\n");
    return result;
  }
  if ((curproc->t_fdtable[fd]->openFlags & O_ACCMODE) == 0) {
    kprintf_n("File permissions are invalid. Flags are read only in sys_write\n");
    return EBADF;
  }
  lock_acquire(curproc->t_fdtable[fd]->lk);
  struct iovec iov;
  struct uio user_uio;

  /** Write nbytes to UIO**/
  uio_uinit(&iov,&user_uio,(userptr_t)buf,nbytes,curproc->t_fdtable[fd]->offset,UIO_WRITE);
  result = VOP_WRITE(curproc->t_fdtable[fd]->vn,&user_uio);
  if (result) {
      //kfree(kbuff);
      lock_release(curproc->t_fdtable[fd]->lk);
      return result;
  }
  *retval = nbytes - user_uio.uio_resid;
  curproc->t_fdtable[fd]->offset = user_uio.uio_offset;
  //kfree(kbuff);
  lock_release(curproc->t_fdtable[fd]->lk);
  return 0;
}

/** system call function for  sys_write
* Note : ssize_t is a typecast for int
*/
ssize_t
sys_read(int fd, void *buf, size_t nbytes, int *retval) {
  int result;
  result = check_isFileHandleValid(fd);
  if (result > 0) {
    kprintf_n("File handle invalid in sys_read\n");
    return result;
  }
  /** check the flags **/
  if ((curproc->t_fdtable[fd]->openFlags & O_WRONLY) == O_WRONLY) {
    kprintf_n("File opened with write-only\n");
    return EBADF;
  }

  lock_acquire(curproc->t_fdtable[fd]->lk);
  struct iovec iov;
  struct uio user_uio;

  /** Read nbytes from UIO**/
  uio_uinit(&iov,&user_uio,(userptr_t)buf,nbytes,curproc->t_fdtable[fd]->offset,UIO_READ);
  result = VOP_READ(curproc->t_fdtable[fd]->vn,&user_uio);
  if (result) {
      //kfree(kbuff);
      lock_release(curproc->t_fdtable[fd]->lk);
      return result;
  }
  *retval = nbytes - user_uio.uio_resid;
  curproc->t_fdtable[fd]->offset = user_uio.uio_offset;
  //kfree(kbuff);
  lock_release(curproc->t_fdtable[fd]->lk);
  return 0;

}

/** System call for sys_chdir **/
int sys_chdir(const char *pathname, int *retval) {
  int result;
  char *kbuff;
  kbuff = (char *)kmalloc(sizeof(char)*PATH_MAX);
  if (kbuff == NULL) {
    kprintf_n("Could not allocate kbuff to sys_open\n");
    return EFAULT;
  }

  if (copyin((const_userptr_t) pathname, kbuff, PATH_MAX) ) {
    kfree(kbuff);
    kprintf_n("Could not copy the pathname to kbuff\n");
    return EFAULT; /* filename was an invalid pointer */
  }
  result = vfs_chdir(kbuff);
  if (result ) {
    kfree(kbuff);
    kprintf_n("vfs_chdir failed \n");
    return result;
  }
  *retval = 0;
  kfree(kbuff);
  return 0;
}

/** System call for lseek**/
off_t
sys_lseek(int fd, off_t pos, int whence, int *retval1, int *retval2) {
  int result;
  kprintf_n("fd %d\n",fd);
  result = check_isFileHandleValid(fd);
  if (result > 0) {
    return result;
  }
  if ((fd <= 2 ) && ((int)pos > 0)) {
    kprintf_n("File handle is invalid in sys_lseek\n");
    return ESPIPE;
  }
  if ((whence != SEEK_SET) && (whence != SEEK_CUR) && (whence != SEEK_END)) {
    kprintf_n("Whence value is invalid\n");
    return EINVAL;
  }
  /*offset itself can be negative. The resultant seek position however, cannot be. This is also verified in man
  * pages "Note that pos is a signed quantity."*/
  lock_acquire(curproc->t_fdtable[fd]->lk);
  if (!VOP_ISSEEKABLE(curproc->t_fdtable[fd]->vn)) {
    lock_release(curproc->t_fdtable[fd]->lk);
    kprintf_n("File does not support seeking \n");
    return ESPIPE;
  }
  struct stat file_stat;
  off_t newPos;
  if (whence == SEEK_SET) {
    newPos = pos;
  } else if (whence == SEEK_CUR) {
    newPos = curproc->t_fdtable[fd]->offset + pos;
  } else if (whence == SEEK_END) {
    result = VOP_STAT(curproc->t_fdtable[fd]->vn,&file_stat);
    if (result) {
      kprintf_n("Unable to stat file for getting offset\n");
      lock_release(curproc->t_fdtable[fd]->lk);
      return result;
    }
    newPos = file_stat.st_size + pos;
    //curproc->t_fdtable[fd]->offset =
  }
  if (newPos < (off_t)0) {
    kprintf_n("Resulting seek would be negative\n");
    lock_release(curproc->t_fdtable[fd]->lk);
    return EINVAL;
  }
  curproc->t_fdtable[fd]->offset = newPos;
  *retval1 = (uint32_t)((newPos & 0xFFFFFFFF00000000) >> 32); // higher 32 bits
  *retval2 = (uint32_t)(newPos & 0x0FFFFFFFFF) ; //lower 32 bits

  lock_release(curproc->t_fdtable[fd]->lk);
  return 0;
}

/** System call for dup2**/
int
sys_dup2(int oldfd, int newfd, int *retval) {
  int result;

  result = check_isFileHandleValid(oldfd);
  if (result > 0) {
    return result;
  }
  if (newfd >= OPEN_MAX || newfd < 0) {
    return EBADF;
  }

  if (oldfd == newfd) { //file handle are same, this has no effect, just return the new handle, and move on
    *retval = newfd;
    return 0;
  }
  int retval1;
  lock_acquire(curproc->t_fdtable[oldfd]->lk);
  if (curproc->t_fdtable[newfd] != NULL) {
      result = sys_close(newfd,&retval1);
      if (result) {
        kprintf_n("Unable to close newfd handle in sys_dup2\n");
        return EINVAL;
      }
  }
  /** Allocate memory to newfd and copy everything from oldfd**/
  curproc->t_fdtable[newfd] = (struct file_descriptor*)kmalloc(sizeof(struct file_descriptor));
  if (curproc->t_fdtable[newfd] == NULL) {
    kprintf_n("Could not create new file descriptor in sys_dup2\n");
    lock_release(curproc->t_fdtable[oldfd]->lk);
    return EFAULT;
  }
  strcpy(curproc->t_fdtable[newfd]->fileName, curproc->t_fdtable[oldfd]->fileName);
  curproc->t_fdtable[newfd]->vn        = curproc->t_fdtable[oldfd]->vn;
  curproc->t_fdtable[newfd]->openFlags = curproc->t_fdtable[oldfd]->openFlags;
  curproc->t_fdtable[oldfd]->refCount  = curproc->t_fdtable[oldfd]->refCount + 1;
  curproc->t_fdtable[newfd]->refCount  = 1;
  curproc->t_fdtable[newfd]->lk        = lock_create(curproc->t_fdtable[newfd]->fileName); // never trust user buffers, always use kbuff

  *retval = newfd;
  lock_release(curproc->t_fdtable[oldfd]->lk);

  return 0;
}

/** System call to get current direction and stored in buf**/
int
sys__getcwd(char *buf, size_t buflen, int *retval) {
  int result;
  if (buf == NULL) {
    kprintf_n("Invalid Buffer.\n");
    return EINVAL;
  }
  /*char *kbuff;
  kbuff = (char *)kmalloc(sizeof(char)*PATH_MAX);
  if (kbuff == NULL) {
    kprintf_n("Could not allocate kbuff to sys__getcwd\n");
    return EFAULT;
  }

  if (copyin((const_userptr_t) buf, kbuff, PATH_MAX) ) {
    kfree(kbuff);
    kprintf_n("Could not copy the buf to kbuff in sys__getcwd\n");
    return EFAULT;
  }*/
  struct uio user_uio;
  struct iovec iov;

  /** Read nbytes from UIO**/
  uio_uinit(&iov,&user_uio,(userptr_t)buf,buflen-1,0,UIO_READ);
  result = vfs_getcwd(&user_uio);
  if (result ) {
    //kfree(kbuff);
    kprintf_n("Could not fetch current working directory\n");
    return EINVAL;
  }
  /** We have to add a \0 terminator to the buffer that we pass as a result, as userspace library call getcwd expects
  it as such**/
  /* null terminate */

	buf[sizeof(buf)-1-user_uio.uio_resid] = '\0';

  *retval = strlen(buf);
  //kfree(buf);
  return 0;

}
