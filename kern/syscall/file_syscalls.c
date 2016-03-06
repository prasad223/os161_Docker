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
  struct vnode *vin;
  struct vnode *vout;
  struct vnode *verr;

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

  if (vfs_open(out,O_WRONLY,0664, &vout)) {
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
  curthread->t_fdtable[1]->offset    = 0;

  if (vfs_open(err,O_WRONLY,0664, &verr)) {
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
int
check_isFileHandleValid(int fHandle) {
  if (fHandle >= OPEN_MAX || fHandle < 0) {
    return EBADF;
  }

  if (curthread->t_fdtable[fHandle] == NULL) {
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
  int readMode = 0;
  struct stat file_stat;
  (void)mode; // suppress warning, mode is unused

  kbuff = (char *)kmalloc(sizeof(char)*PATH_MAX);
  if (kbuff == NULL) {
    kprintf_n("Could not allocate kbuff to sys_open\n");
    return EFAULT;
  }

  if (copyin((const_userptr_t) filename, kbuff, PATH_MAX) ) {
    kfree(kbuff);
    kprintf_n("Could not copy the filename to kbuff\n");
    return EFAULT; /* filename was an invalid pointer */
  }
  //check the flags
  if (flags < 0) {
    kprintf_n("Flags cannot be negative\n");
    return EINVAL;
  }
  if (flags == O_RDONLY) {
    readMode  = 1;
  }

  if (  ( (flags & O_WRONLY) == O_WRONLY )||
        ( (flags & O_RDWR) == O_RDWR ) ) {
    if (readMode == 1)
    {
      kprintf_n("Both O_RDONLY and O_WRONLY flags passed\n");
      return EINVAL;
    }
  }  else if (readMode == 0 ) {
    kprintf_n("Unable to open file. Bad flags passed \n");
    return EINVAL;
  }
  if (readMode == 1 ) { // invalid flags with read_mode
    if ( ((flags & O_CREAT) == O_CREAT) ||
        ((flags & O_EXCL) == O_EXCL)   ||
        ((flags & O_TRUNC) == O_TRUNC) ||
        ((flags & O_APPEND) == O_APPEND) ) {
      kprintf_n("Invalid flags passed with read mode\n");
      return EINVAL;
    }
  }
  if ( ( (flags & O_EXCL) == O_EXCL) && ( (flags & O_CREAT) != O_CREAT) ) { // O_EXCL makes sense only with O_CREAT
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
  if (vfs_open(kbuff,flags,0664, &vn)) {
    kprintf_n("flags %d",flags  );
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
    /*file_stat = (struct stat *)kmalloc(sizeof(struct stat));
    if (file_stat == NULL) {
      kprintf_n("Unable to allocate memory for stat\n");
      kfree(kbuff);
      lock_destroy(curthread->t_fdtable[index]->lk);
      vfs_close(curthread->t_fdtable[index]->vn);
      kfree(curthread->t_fdtable[index]);
      curthread->t_fdtable[index] = NULL;
      return EFAULT;
    }*/
    result = VOP_STAT(curthread->t_fdtable[index]->vn,&file_stat);
    if (result) {
      kprintf_n("Unable to stat file for getting offset\n");
      kfree(kbuff);
      //kfree(file_stat);
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
  if (curthread->t_fdtable[fHandle]->vn == NULL) {
    //*retval = 1; // for any future uses
    kprintf_n("vnode is NULL in sys_close \n");
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

/** Function similiar to uio_kinit, only difference is this will initialize a uio for usage with user-space
* Important parameters are detailed below :
* iov_ubase : Points to a user-supplied buffer
* uio_segflg: UIO_USERSPACE to show we are dealing with userspace data
* uio_offset: This will be supplied by the File descriptor calling the funcition. The file descriptor will supply its own
*             offset value here
* uio_resid : The amount of data to transfer
* uio_space : current process's address space
* TODO      : Calling this function did not give me proper result . Investigate
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
  uio->uio_space = curthread->t_proc->p_addrspace;
}
/** system call function for  sys_write
* Note : ssize_t is a typecast for int
*/
ssize_t
sys_write(int fd, const void *buf, size_t nbytes, int *retval) {
  int result;
  /*if (fd < 0 || fd >= OPEN_MAX) {
    return EBADF;
  }
  if (curthread->t_fdtable[fd] == NULL) {
    return EBADF;
  }*/
  result = check_isFileHandleValid(fd);
  if (result > 0) {
    return result;
  }
  if (curthread->t_fdtable[fd]->openFlags  == O_RDONLY) { // inappropriate permissions
    return EBADF;
  }
  void *kbuff;
  /**this is a clever way to get around the fact that we are not aware
  * of the "type" of data to write. *buf points to first location of buf, thus sizeof(*buf) gives size of the primitive
  * held in buf. Then, it is straight forward to allocate kbuff the required number of bytes**/
  kbuff = (char *)kmalloc(sizeof(*buf)*nbytes);
  if (kbuff == NULL) {
    kprintf_n("Could not allocate kbuff to sys_write\n");
    return EFAULT;
  }

  if (copyin((const_userptr_t) buf, kbuff, sizeof(kbuff) ) ) {
    kprintf_n("Could not copy the buffer to kbuff in sys_write\n");
    return EFAULT; /* filename was an invalid pointer */
  }
  lock_acquire(curthread->t_fdtable[fd]->lk);
  struct iovec iov;
  struct uio user_uio;

  /** Write nbytes to UIO**/
  uio_uinit(&iov,&user_uio,(userptr_t)buf,nbytes,curthread->t_fdtable[fd]->offset,UIO_WRITE);
  result = VOP_WRITE(curthread->t_fdtable[fd]->vn,&user_uio);
  if (result) {
      kfree(kbuff);
      lock_release(curthread->t_fdtable[fd]->lk);
      return result;
  }
  *retval = nbytes - user_uio.uio_resid;
  curthread->t_fdtable[fd]->offset = user_uio.uio_offset;
  kfree(kbuff);
  lock_release(curthread->t_fdtable[fd]->lk);
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
    return result;
  }
  if (curthread->t_fdtable[fd]->openFlags  == O_WRONLY) { // inappropriate permissions
    return EBADF;
  }
  void *kbuff;
  /**this is a clever way to get around the fact that we are not aware
  * of the "type" of data to write. *buf points to first location of buf, thus sizeof(*buf) gives size of the primitive
  * held in buf. Then, it is straight forward to allocate kbuff the required number of bytes**/
  kbuff = (char *)kmalloc(sizeof(*buf)*nbytes);
  if (kbuff == NULL) {
    kprintf_n("Could not allocate kbuff to sys_read\n");
    return EFAULT;
  }
  if (copyin((const_userptr_t) buf, kbuff, sizeof(kbuff) ) ) {
    kprintf_n("Could not copy the buffer to kbuff in sys_read\n");
    return EFAULT; /* filename was an invalid pointer */
  }
  lock_acquire(curthread->t_fdtable[fd]->lk);
  struct iovec iov;
  struct uio user_uio;

  /** Read nbytes from UIO**/
  uio_uinit(&iov,&user_uio,(userptr_t)buf,nbytes,curthread->t_fdtable[fd]->offset,UIO_READ);
  result = VOP_READ(curthread->t_fdtable[fd]->vn,&user_uio);
  if (result) {
      kfree(kbuff);
      lock_release(curthread->t_fdtable[fd]->lk);
      return result;
  }
  *retval = nbytes - user_uio.uio_resid;
  curthread->t_fdtable[fd]->offset = user_uio.uio_offset;
  kfree(kbuff);
  lock_release(curthread->t_fdtable[fd]->lk);
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
  result = check_isFileHandleValid(fd);
  if (result > 0) {
    return result;
  }
  if (fd == 0 || fd == 1 || fd == 2) { // all seeks on console handles fail
    return ESPIPE;
  }
  if ((whence != SEEK_SET) && (whence != SEEK_CUR) && (whence != SEEK_END)) {
    kprintf_n("Whence value is invalid\n");
    return EINVAL;
  }
  /*offset itself can be negative. The resultant seek position however, cannot be. This is also verified in man
  * pages "Note that pos is a signed quantity."*/
  lock_acquire(curthread->t_fdtable[fd]->lk);
  if (!VOP_ISSEEKABLE(curthread->t_fdtable[fd]->vn)) {
    lock_release(curthread->t_fdtable[fd]->lk);
    kprintf_n("File does not support seeking \n");
    return ESPIPE;
  }
  struct stat file_stat;
  off_t newPos;
  if (whence == SEEK_SET) {
    newPos = pos;
  } else if (whence == SEEK_CUR) {
    newPos = curthread->t_fdtable[fd]->offset + pos;
  } else if (whence == SEEK_END) {
    result = VOP_STAT(curthread->t_fdtable[fd]->vn,&file_stat);
    if (result) {
      kprintf_n("Unable to stat file for getting offset\n");
      lock_release(curthread->t_fdtable[fd]->lk);
      return result;
    }
    newPos = file_stat.st_size + pos;
    //curthread->t_fdtable[fd]->offset =
  }
  if (newPos < (off_t)0) {
    kprintf_n("Resulting seek would be negative\n");
    lock_release(curthread->t_fdtable[fd]->lk);
    return EINVAL;
  }
  curthread->t_fdtable[fd]->offset = newPos;
  *retval1 = (uint32_t)((newPos & 0xFFFFFFFF00000000) >> 32); // higher 32 bits
  *retval2 = (uint32_t)(newPos & 0x0FFFFFFFFF) ; //lower 32 bits

  lock_release(curthread->t_fdtable[fd]->lk);
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
  result = check_isFileHandleValid(newfd);
  if (result > 0) {
    return result;
  }
  if (oldfd == newfd) { //file handle are same, this has no effect, just return the new handle, and move on
    *retval = newfd;
    return 0;
  }
  int retval1, index;
  lock_acquire(curthread->t_fdtable[oldfd]->lk);
  index =0;
  while(curthread->t_fdtable[index] != NULL) {
    index++;
  }
  if (index >= OPEN_MAX) { // process specific file limit was reached
    lock_release(curthread->t_fdtable[index]->lk  );
    kprintf_n("Process file table is full in oldfd in sys_dup2\n");
    return EMFILE;
  }
  if (curthread->t_fdtable[newfd] != NULL) {
      result = sys_close(newfd,&retval1);
      if (result) {
        kprintf_n("Unable to close newfd handle in sys_dup2\n");
        lock_release(curthread->t_fdtable[index]->lk  );
        return EINVAL;
      }
  }
  /** Allocate memory to newfd and copy everything from oldfd**/
  curthread->t_fdtable[newfd] = (struct file_descriptor*)kmalloc(sizeof(struct file_descriptor));
  if (curthread->t_fdtable[newfd] == NULL) {
    kprintf_n("Could not create new file descriptor in sys_dup2\n");
    lock_release(curthread->t_fdtable[oldfd]->lk);
    return EFAULT;
  }
  strcpy(curthread->t_fdtable[newfd]->fileName, curthread->t_fdtable[oldfd]->fileName);
  curthread->t_fdtable[newfd]->vn        = curthread->t_fdtable[oldfd]->vn;
  curthread->t_fdtable[newfd]->openFlags = curthread->t_fdtable[oldfd]->openFlags;
  curthread->t_fdtable[oldfd]->refCount  = curthread->t_fdtable[oldfd]->refCount + 1;
  curthread->t_fdtable[newfd]->refCount  = 1;
  curthread->t_fdtable[newfd]->lk        = lock_create(curthread->t_fdtable[newfd]->fileName); // never trust user buffers, always use kbuff

  *retval = newfd;
  lock_release(curthread->t_fdtable[oldfd]->lk);

  return 0;
}

/** System call to get current direction and stored in buf**/
int
sys__getcwd(char *buf, size_t buflen, int *retval) {
  int result;
  char *kbuff;
  kbuff = (char *)kmalloc(sizeof(char)*PATH_MAX);
  if (kbuff == NULL) {
    kprintf_n("Could not allocate kbuff to sys__getcwd\n");
    return EFAULT;
  }

  if (copyin((const_userptr_t) buf, kbuff, PATH_MAX) ) {
    kfree(kbuff);
    kprintf_n("Could not copy the buf to kbuff in sys__getcwd\n");
    return EFAULT; /* filename was an invalid pointer */
  }
  struct uio user_uio;
  struct iovec iov;
  //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
  /*iov.iov_ubase      = (userptr_t)buf; // why did this require buf & not kbuff
  iov.iov_len        = buflen -1 ; // last character is NULL terminator
  user_uio.uio_iov   = &iov;
  user_uio.uio_iovcnt= 1;
  user_uio.uio_segflg= UIO_USERSPACE;
  user_uio.uio_rw    = UIO_READ;
  user_uio.uio_offset= 0;
  user_uio.uio_resid = buflen -1 ;  // last character is NULL terminator
  user_uio.uio_space = curthread->t_proc->p_addrspace;*/
  //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
  /** Read nbytes from UIO**/
  uio_uinit(&iov,&user_uio,(userptr_t)buf,buflen-1,0,UIO_READ);
  result = vfs_getcwd(&user_uio);
  if (result ) {
    kfree(kbuff);
    kprintf_n("Could not fetch current working directory\n");
    return EINVAL;
  }
  /** We have to add a \0 terminator to the buffer that we pass as a result, as userspace library call getcwd expects
  it as such**/
  /* null terminate */

	buf[sizeof(buf)-1-user_uio.uio_resid] = '\0';

  *retval = strlen(buf);
  kfree(buf);
  return 0;

}
