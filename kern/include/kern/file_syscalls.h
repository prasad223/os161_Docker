/**
* The header file defining the file descriptor structure
**/
#ifndef _FILE_CALL_H_
#define _FILE_CALL_H_

#include <types.h>
#include <limits.h>
#include <uio.h>

struct file_descriptor {
	char fileName[__NAME_MAX] ; // the file name associated with the FD, can be used for debugging
	struct vnode* vn; //pointer to underlying file abstraction
	int openFlags ;   //flag set with which file was opened with
	int refCount  ;   //keep track of number of references to the file, required in dup2 and fork()
	struct lock* lk;  //for synchronization between threads sharing the file descriptor
	off_t offset;     //required by lseek syscalls ** TODO : explore any more usage
};

int sys_open(const char *filename, int flags, mode_t mode, int *retval);
int sys_close(int fHandle,int *retval);
ssize_t sys_read(int fd, void *buf, size_t nbytes, int *retval);
ssize_t sys_write(int fd, const void *buf, size_t nbytes, int *retval);
off_t sys_lseek(int fd, off_t pos, int whence, int *retval1, int *retval2); //off_t is int64, thus two int* to store them
int sys_dup2(int oldfd, int newfd, int *retval);
int sys_chdir(const char *pathname, int *retval);
int sys__getcwd(char *buf, size_t buflen, int *retval);

int init_file_descriptor(void);
void uio_uinit(struct iovec *iov, struct uio *uio, void *kbuff, size_t len, off_t pos, enum uio_rw rw);
int check_isFileHandleValid(int fHandle);

#endif /* _FILE_CALL_H_ */
