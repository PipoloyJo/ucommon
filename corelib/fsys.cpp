// Copyright (C) 2006-2010 David Sugar, Tycho Softworks.
//
// This file is part of GNU uCommon C++.
//
// GNU uCommon C++ is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// GNU uCommon C++ is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with GNU uCommon C++.  If not, see <http://www.gnu.org/licenses/>.

#ifndef _MSC_VER
#include <sys/stat.h>
#endif

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <ucommon-config.h>

// broken BSD; XOPEN should not imply _POSIX_C_SOURCE,
//  _POSIX_C_SOURCE should not stop __BSD_VISIBLE

#define u_int unsigned int
#define u_short unsigned short
#define u_long unsigned long
#define u_char unsigned char

#include <ucommon/export.h>
#include <ucommon/thread.h>
#include <ucommon/fsys.h>
#include <ucommon/string.h>
#include <ucommon/memory.h>
#include <ucommon/shell.h>

#ifdef  HAVE_SYSLOG_H
#include <syslog.h>
#endif

#ifdef HAVE_LINUX_VERSION_H
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,8)
#ifdef  HAVE_POSIX_FADVISE
#undef  HAVE_POSIX_FADVISE
#endif
#endif
#endif

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#ifdef  HAVE_POSIX_FADVISE
#ifndef POSIX_FADV_RANDOM
#undef  HAVE_POSIX_FADVISE
#endif
#endif

#ifdef  HAVE_DIRENT_H
#include <dirent.h>
#endif

#ifdef _MSWINDOWS_
#include <direct.h>
#include <winioctl.h>
#include <io.h>
#endif

#ifdef  HAVE_SYS_INOTIFY_H
#include <sys/inotify.h>
#endif

#ifdef  HAVE_SYS_EVENT_H
#include <sys/event.h>
#endif

using namespace UCOMMON_NAMESPACE;

const fsys::offset_t fsys::end = (offset_t)(-1);
charfile cstdin(stdin);
charfile cstdout(stdout);
charfile cstderr(stderr);

#ifdef  _MSWINDOWS_

// removed from some sdk versions...
struct LOCAL_REPARSE_DATA_BUFFER
{
    DWORD  ReparseTag;
    WORD   ReparseDataLength;
    WORD   Reserved;

    // IO_REPARSE_TAG_MOUNT_POINT specifics follow
    WORD   SubstituteNameOffset;
    WORD   SubstituteNameLength;
    WORD   PrintNameOffset;
    WORD   PrintNameLength;
    WCHAR  PathBuffer[1];
};

int fsys::remapError(void)
{
    DWORD err = GetLastError();

    switch(err)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
        return ENOENT;
    case ERROR_TOO_MANY_OPEN_FILES:
        return EMFILE;
    case ERROR_ACCESS_DENIED:
    case ERROR_WRITE_PROTECT:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return EACCES;
    case ERROR_INVALID_HANDLE:
        return EBADF;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return ENOMEM;
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_UNIT:
    case ERROR_BAD_DEVICE:
        return ENODEV;
    case ERROR_NOT_SAME_DEVICE:
        return EXDEV;
    case ERROR_NOT_SUPPORTED:
    case ERROR_CALL_NOT_IMPLEMENTED:
        return ENOSYS;
    case ERROR_END_OF_MEDIA:
    case ERROR_EOM_OVERFLOW:
    case ERROR_HANDLE_DISK_FULL:
    case ERROR_DISK_FULL:
        return ENOSPC;
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
        return EACCES;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        return EEXIST;
    case ERROR_CANNOT_MAKE:
    case ERROR_NOT_OWNER:
        return EPERM;
    case ERROR_NO_PROC_SLOTS:
        return EAGAIN;
    case ERROR_BROKEN_PIPE:
    case ERROR_NO_DATA:
        return EPIPE;
    case ERROR_OPEN_FAILED:
        return EIO;
    case ERROR_NOACCESS:
        return EFAULT;
    case ERROR_IO_DEVICE:
    case ERROR_CRC:
    case ERROR_NO_SIGNAL_SENT:
        return EIO;
    case ERROR_CHILD_NOT_COMPLETE:
    case ERROR_SIGNAL_PENDING:
    case ERROR_BUSY:
        return EBUSY;
    case ERROR_DIR_NOT_EMPTY:
        return ENOTEMPTY;
    case ERROR_DIRECTORY:
        return ENOTDIR;
    default:
        return EINVAL;
    }
}

int fsys::create(const char *path, unsigned mode)
{
    if(!CreateDirectory(path, NULL))
        return remapError();

    return mode(path, mode);
}

fd_t fsys::nullfile(void)
{
    SECURITY_ATTRIBUTES sattr;

    sattr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sattr.bInheritHandle = TRUE;
    sattr.lpSecurityDescriptor = NULL;

    return CreateFile("nul", GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, &sattr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

int fsys::pipe(fd_t& input, fd_t& output, size_t size)
{
    input = output = NULL;
    SECURITY_ATTRIBUTES sattr;

    sattr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sattr.bInheritHandle = TRUE;
    sattr.lpSecurityDescriptor = NULL;

    if(!CreatePipe(&input, &output, &sattr, size))
        return remapError();

    return 0;
}

int fsys::info(const char *path, struct stat *buf)
{
    if(_stat(path, (struct _stat *)(buf)))
        return remapError();
    return 0;
}

int fsys::trunc(offset_t offset)
{
    if(fsys::seek(offset) != 0)
        return remapError();

    if(SetEndOfFile(fd))
        return 0;

    return remapError();
}

int fsys::info(struct stat *buf)
{
    int fn = _open_osfhandle((intptr_t)(fd), O_RDONLY);

    int rtn = _fstat(fn, (struct _stat *)(buf));
    _close(fn);
    if(rtn)
        error = remapError();
    return rtn;
}

int fsys::prefix(const char *path)
{
    if (_chdir(path))
        return remapError();
    return 0;
}

int fsys::prefix(char *path, size_t len)
{
    if (_getcwd(path, len))
        return remapError();
    return 0;
}

int fsys::mode(const char *path, unsigned value)
{
    if(_chmod(path, value))
        return remapError();
    return 0;
}

bool fsys::is_exists(const char *path)
{
    if(_access(path, F_OK))
        return false;
    return true;
}

bool fsys::is_readable(const char *path)
{
    if(_access(path, R_OK))
        return false;
    return true;
}

bool fsys::is_writable(const char *path)
{
    if(_access(path, W_OK))
        return false;
    return true;
}

bool fsys::is_executable(const char *path)
{
    const char *strrchr(path, '.');
    if(eq_case(path, ".exe"))
        return true;

    if(eq_case(path, ".bat"))
        return true;

    if(eq_case(path, ".com"))
        return true;

    if(eq_case(path, ".cmd"))
        return true;

    if(eq_case(path, ".ps1"))
        return true;

    return false;
}

bool fsys::is_tty(fd_t fd)
{
    if(fd == INVALID_HANDLE_VALUE)
        return false;
    DWORD type = GetFileType(fd);
    if(type == FILE_TYPE_CHAR)
        return true;
    return false;
}

bool fsys::is_tty(void)
{
    error = 0;
    if(fd == INVALID_HANDLE_VALUE)
        return false;
    DWORD type = GetFileType(fd);
    if(!type)
        error = remapError();
    if(type == FILE_TYPE_CHAR)
        return true;
    return false;
}

int fsys::close(void)
{
    error = 0;

    if(fd == INVALID_HANDLE_VALUE)
        return EBADR;

    if(ptr) {
        if(::FindClose(fd)) {
            delete ptr;
            ptr = NULL;
            fd = INVALID_HANDLE_VALUE;
        }
        else
            error = remapError();
    }
    else if(::CloseHandle(fd))
        fd = INVALID_HANDLE_VALUE;
    else
        error = remapError();
    return error;
}

ssize_t fsys::read(void *buf, size_t len)
{
    ssize_t rtn = -1;
    DWORD count;

    if(ptr) {
        snprintf((char *)buf, len, ptr->cFileName);
        rtn = strlen(ptr->cFileName);
        if(!FindNextFile(fd, ptr))
            close();
        return rtn;
    }

    if(ReadFile(fd, (LPVOID) buf, (DWORD)len, &count, NULL))
        rtn = count;
    else
        error = remapError();

    return rtn;
}

ssize_t fsys::write(const void *buf, size_t len)
{
    ssize_t rtn = -1;
    DWORD count;

    if(WriteFile(fd, (LPVOID) buf, (DWORD)len, &count, NULL))
        rtn = count;
    else
        error = remapError();

    return rtn;
}

int fsys::sync(void)
{
    return 0;
}

fd_t fsys::input(const char *path)
{
    SECURITY_ATTRIBUTES sattr;

    sattr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sattr.bInheritHandle = TRUE;
    sattr.lpSecurityDescriptor = NULL;

    return CreateFile(path, GENERIC_READ, FILE_SHARE_READ, &sattr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

fd_t fsys::output(const char *path)
{
    SECURITY_ATTRIBUTES sattr;

    sattr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sattr.bInheritHandle = TRUE;
    sattr.lpSecurityDescriptor = NULL;

    return CreateFile(path, GENERIC_WRITE, 0, &sattr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

fd_t fsys::append(const char *path)
{
    SECURITY_ATTRIBUTES sattr;

    sattr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sattr.bInheritHandle = TRUE;
    sattr.lpSecurityDescriptor = NULL;

    fd_t fd = CreateFile(path, GENERIC_WRITE, 0, &sattr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if(fd != INVALID_HANDLE_VALUE)
        SetFilePointer(fd, 0, NULL, FILE_END);

    return fd;
}

void fsys::release(fd_t fd)
{
    CloseHandle(fd);
}

void fsys::open(const char *path, access_t access)
{
    bool append = false;
    DWORD amode = 0;
    DWORD smode = 0;
    DWORD attr = FILE_ATTRIBUTE_NORMAL;
    char buf[128];

    close();

    if(access == ACCESS_DEVICE) {
#ifdef  _MSWINDOWS_
        if(isalpha(path[0]) && path[1] == ':') {
            if(!QueryDosDevice(path, buf, sizeof(buf))) {
                error = ENODEV;
                return;
            }
            path = buf;
        }
#else
        if(!strchr(path, '/')) {
            if(path[0] == 'S' && isdigit(path[1]))
                snprintf(buf, sizeof(buf) "/dev/tty%s", path);
            else if(strncmp(path, "USB", 3))
                snprintf(buf, sizeof(buf), "/dev/tty%s", path);
            else
                snprintf(buf, sizeof(buf), "/dev/%s", path);

            char *cp = strchr(buf, ':');
            if(cp)
                *cp = 0;
            path = buf;
        }
#endif
        if(!is_device(buf)) {
            error = ENODEV;
            return;
        }
    }

    switch(access)
    {
    case ACCESS_STREAM:
#ifdef  FILE_FLAG_SEQUENTIAL_SCAN
        attr |= FILE_FLAG_SEQUENTIAL_SCAN;
#endif
    case ACCESS_RDONLY:
        amode = GENERIC_READ;
        smode = FILE_SHARE_READ;
        break;
    case ACCESS_WRONLY:
        amode = GENERIC_WRITE;
        break;
    case ACCESS_EXCLUSIVE:
    case ACCESS_DEVICE:
        amode = GENERIC_READ | GENERIC_WRITE;
        break;
    case ACCESS_RANDOM:
        attr |= FILE_FLAG_RANDOM_ACCESS;
    case ACCESS_REWRITE:
        amode = GENERIC_READ | GENERIC_WRITE;
        smode = FILE_SHARE_READ;
        break;
    case ACCESS_APPEND:
        amode = GENERIC_WRITE;
        append = true;
        break;
    case ACCESS_SHARED:
        amode = GENERIC_READ | GENERIC_WRITE;
        smode = FILE_SHARE_READ | FILE_SHARE_WRITE;
        break;
    case ACCESS_DIRECTORY:
        char tpath[256];
        DWORD attr = GetFileAttributes(path);

        if((attr == (DWORD)~0l) || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            error = ENOTDIR;
            return;
        }

        snprintf(tpath, sizeof(tpath), "%s%s", path, "\\*");
        ptr = new WIN32_FIND_DATA;
        fd = FindFirstFile(tpath, ptr);
        if(fd == INVALID_HANDLE_VALUE) {
            delete ptr;
            ptr = NULL;
            error = remapError();
        }
        return;
    }

    fd = CreateFile(path, amode, smode, NULL, OPEN_EXISTING, attr, NULL);
    if(fd != INVALID_HANDLE_VALUE && append)
        seek(end);
    else if(fd == INVALID_HANDLE_VALUE)
        error = remapError();
}

void fsys::open(const char *path, unsigned fmode, access_t access)
{
    bool append = false;
    DWORD amode = 0;
    DWORD cmode = 0;
    DWORD smode = 0;
    DWORD attr = FILE_ATTRIBUTE_NORMAL;

    fmode &= 0666;

    const char *cp = strrchr(path, '\\');
    const char *cp2 = strrchr(path, '/');
    if(cp2 > cp)
        cp = cp2;

    if(!cp)
        cp = path;
    else
        ++cp;

    if(*cp == '.')
        attr = FILE_ATTRIBUTE_HIDDEN;

    switch(access)
    {
    case ACCESS_DEVICE:
        error = ENOSYS;
        return;

    case ACCESS_RDONLY:
        amode = GENERIC_READ;
        cmode = OPEN_ALWAYS;
        smode = FILE_SHARE_READ;
        break;
    case ACCESS_STREAM:
    case ACCESS_WRONLY:
        amode = GENERIC_WRITE;
        cmode = CREATE_ALWAYS;
        break;
    case ACCESS_EXCLUSIVE:
        amode = GENERIC_READ | GENERIC_WRITE;
        cmode = OPEN_ALWAYS;
        break;
    case ACCESS_RANDOM:
        attr |= FILE_FLAG_RANDOM_ACCESS;
    case ACCESS_REWRITE:
        amode = GENERIC_READ | GENERIC_WRITE;
        cmode = OPEN_ALWAYS;
        smode = FILE_SHARE_READ;
        break;
    case ACCESS_APPEND:
        amode = GENERIC_WRITE;
        cmode = OPEN_ALWAYS;
        append = true;
        break;
    case ACCESS_SHARED:
        amode = GENERIC_READ | GENERIC_WRITE;
        cmode = OPEN_ALWAYS;
        smode = FILE_SHARE_READ | FILE_SHARE_WRITE;
        break;
    case ACCESS_DIRECTORY:
        create(path, mode);
        open(path, access);
        return;
    }
    fd = CreateFile(path, amode, smode, NULL, cmode, attr, NULL);
    if(fd != INVALID_HANDLE_VALUE && append)
        seek(end);
    else if(fd == INVALID_HANDLE_VALUE)
        error = remapError();
    if(fd != INVALID_HANDLE_VALUE)
        mode(path, fmode);
}

fsys::fsys(const fsys& copy)
{
    error = 0;
    fd = INVALID_HANDLE_VALUE;
    ptr = NULL;

    if(copy.fd == INVALID_HANDLE_VALUE)
        return;

    HANDLE pHandle = GetCurrentProcess();
    if(!DuplicateHandle(pHandle, copy.fd, pHandle, &fd, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        fd = INVALID_HANDLE_VALUE;
        error = remapError();
    }
}

int fsys::inherit(fd_t& from, bool enable)
{
    HANDLE pHandle = GetCurrentProcess();

    if(!enable) {
        if(!SetHandleInformation(from, HANDLE_FLAG_INHERIT, 0))
            return remapError();
        return 0;
    }

    fd_t fd;
    if(DuplicateHandle(pHandle, from, pHandle, &fd, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        release(from);
        from = fd;
        return 0;
    }

    return remapError();
}

void fsys::operator=(fd_t from)
{
    HANDLE pHandle = GetCurrentProcess();

    if(fd != INVALID_HANDLE_VALUE) {
        if(!CloseHandle(fd)) {
            error = remapError();
            return;
        }
    }
    if(DuplicateHandle(pHandle, from, pHandle, &fd, 0, FALSE, DUPLICATE_SAME_ACCESS))
        error = 0;
    else {
        fd = INVALID_HANDLE_VALUE;
        error = remapError();
    }
}

void fsys::operator=(const fsys& from)
{
    HANDLE pHandle = GetCurrentProcess();

    if(fd != INVALID_HANDLE_VALUE) {
        if(!CloseHandle(fd)) {
            error = remapError();
            return;
        }
    }
    if(DuplicateHandle(pHandle, from.fd, pHandle, &fd, 0, FALSE, DUPLICATE_SAME_ACCESS))
        error = 0;
    else {
        fd = INVALID_HANDLE_VALUE;
        error = remapError();
    }
}

int fsys::drop(offset_t size)
{
    error = ENOSYS;
    return ENOSYS;
}

int fsys::seek(offset_t pos)
{
    DWORD rpos = pos;
    int mode = FILE_BEGIN;

    if(rpos == (DWORD)end) {
        rpos = 0;
        mode = FILE_END;
    }
    if(SetFilePointer(fd, rpos, NULL, mode) == INVALID_SET_FILE_POINTER) {
        error = remapError();
        return error;
    }
    return 0;
}

#else

ssize_t fsys::read(void *buf, size_t len)
{
    if(ptr) {
        dirent *entry = ::readdir((DIR *)ptr);

        if(!entry)
            return 0;

        String::set((char *)buf, len, entry->d_name);
        return strlen(entry->d_name);
    }

#ifdef  __PTH__
    int rtn = ::pth_read(fd, buf, len);
#else
    int rtn = ::read(fd, buf, len);
#endif

    if(rtn < 0)
        error = remapError();
    return rtn;
}

int fsys::sync(void)
{
    if(ptr) {
        error = EBADF;
        return -1;
    }

    int rtn = ::fsync(fd);
    if(rtn < 0)
        error = remapError();
    else
        return 0;
    return error;
}

ssize_t fsys::write(const void *buf, size_t len)
{
    if(ptr) {
        error = EBADF;
        return -1;
    }

#ifdef  __PTH__
    int rtn = pth_write(fd, buf, len);
#else
    int rtn = ::write(fd, buf, len);
#endif

    if(rtn < 0)
        error = remapError();
    return rtn;
}

fd_t fsys::nullfile(void)
{
    return ::open("/dev/null", O_RDWR);
}

int fsys::pipe(fd_t& input, fd_t& output, size_t size)
{
    input = output = -1;
    int pfd[2];
    if(::pipe(pfd))
        return remapError();
    input = pfd[0];
    output = pfd[1];
    return 0;
}

bool fsys::is_tty(fd_t fd)
{
    if(isatty(fd))
        return true;
    return false;
}

bool fsys::is_tty(void)
{
    if(isatty(fd))
        return true;
    return false;
}

int fsys::close(void)
{
    error = 0;
    if(ptr) {
        if(::closedir((DIR *)ptr))
            error = remapError();
        ptr = NULL;
    }
    else if(fd != INVALID_HANDLE_VALUE) {
        if(::close(fd) == 0)
            fd = INVALID_HANDLE_VALUE;
        else
            error = remapError();
    }
    else
        error = EBADR;
    return error;
}

fd_t fsys::input(const char *path)
{
    return ::open(path, O_RDONLY);
}

fd_t fsys::output(const char *path)
{
    return ::open(path, O_WRONLY | O_CREAT | O_TRUNC, EVERYONE);
}

fd_t fsys::append(const char *path)
{
    return ::open(path, O_WRONLY | O_CREAT | O_APPEND, EVERYONE);
}

void fsys::release(fd_t fd)
{
    ::close(fd);
}

void fsys::open(const char *path, unsigned fmode, access_t access)
{
    unsigned flags = 0;

    close();

    fmode &= 0666;

    switch(access)
    {
    case ACCESS_DEVICE:
        error = ENOSYS;
        return;

    case ACCESS_RDONLY:
        flags = O_RDONLY | O_CREAT;
        break;
    case ACCESS_STREAM:
    case ACCESS_WRONLY:
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        break;
    case ACCESS_RANDOM:
    case ACCESS_SHARED:
    case ACCESS_REWRITE:
    case ACCESS_EXCLUSIVE:
        flags = O_RDWR | O_CREAT;
        break;
    case ACCESS_APPEND:
        flags = O_RDWR | O_APPEND | O_CREAT;
        break;
    case ACCESS_DIRECTORY:
        ::mkdir(path, fmode);
        open(path, access);
        return;
    }
    fd = ::open(path, flags, fmode);
    if(fd == INVALID_HANDLE_VALUE)
        error = remapError();
#ifdef HAVE_POSIX_FADVISE
    else {
        if(access == ACCESS_RANDOM)
            posix_fadvise(fd, (off_t)0, (off_t)0, POSIX_FADV_RANDOM);
    }
#endif
}

int fsys::create(const char *path, unsigned perms)
{
    if(perms & 06)
        perms |= 01;
    if(perms & 060)
        perms |= 010;
    if(perms & 0600)
        perms |= 0100;

    if(::mkdir(path, perms))
        return remapError();
    return 0;
}

void fsys::open(const char *path, access_t access)
{
    unsigned flags = 0;

    close();

    switch(access)
    {
    case ACCESS_STREAM:
#if defined(O_STREAMING)
        flags = O_RDONLY | O_STREAMING;
        break;
#endif
    case ACCESS_RDONLY:
        flags = O_RDONLY;
        break;
    case ACCESS_WRONLY:
        flags = O_WRONLY;
        break;
    case ACCESS_EXCLUSIVE:
    case ACCESS_RANDOM:
    case ACCESS_SHARED:
    case ACCESS_REWRITE:
    case ACCESS_DEVICE:
        flags = O_RDWR;
        break;
    case ACCESS_APPEND:
        flags = O_RDWR | O_APPEND;
        break;
    case ACCESS_DIRECTORY:
        ptr = opendir(path);
        if(!ptr)
            error = remapError();
        return;
    }
    fd = ::open(path, flags);
    if(fd == INVALID_HANDLE_VALUE)
        error = remapError();
#ifdef HAVE_POSIX_FADVISE
    else {
        // Linux kernel bug prevents use of POSIX_FADV_NOREUSE in streaming...
        if(access == ACCESS_STREAM)
            posix_fadvise(fd, (off_t)0, (off_t)0, POSIX_FADV_SEQUENTIAL);
        else if(access == ACCESS_RANDOM)
            posix_fadvise(fd, (off_t)0, (off_t)0, POSIX_FADV_RANDOM);
    }
#endif
}

int fsys::info(const char *path, struct stat *ino)
{
    if(::stat(path, ino))
        return remapError();
    return 0;
}

#ifdef  HAVE_FTRUNCATE
int fsys::trunc(offset_t offset)
{
    if(fsys::seek(offset) != 0)
        return remapError();

    if(::ftruncate(fd, offset) == 0)
        return 0;
    return remapError();
}
#else
int fsys::trunc(offset_t offset)
{
    if(fsys::seek(offset) != 0)
        return remapError();

    return ENOSYS;
}
#endif

int fsys::info(struct stat *ino)
{
    if(::fstat(fd, ino)) {
        error = remapError();
        return error;
    }
    return 0;
}

int fsys::prefix(const char *path)
{
    if(::chdir(path))
        return remapError();
    return 0;
}

int fsys::prefix(char *path, size_t len)
{
    if(::getcwd(path, len))
        return remapError();
    return 0;
}

int fsys::mode(const char *path, unsigned value)
{
    if(::chmod(path, value))
        return remapError();
    return 0;
}

bool fsys::is_exists(const char *path)
{
    if(::access(path, F_OK))
        return false;

    return true;
}

bool fsys::is_readable(const char *path)
{
    if(::access(path, R_OK))
        return false;

    return true;
}

bool fsys::is_writable(const char *path)
{
    if(::access(path, W_OK))
        return false;

    return true;
}

bool fsys::is_executable(const char *path)
{
    if(is_dir(path))
        return false;

    if(::access(path, X_OK))
        return false;

    return true;
}

fsys::fsys(const fsys& copy)
{
    fd = INVALID_HANDLE_VALUE;
    ptr = NULL;
    error = 0;

    if(copy.fd != INVALID_HANDLE_VALUE) {
        fd = ::dup(copy.fd);
    }
    else
        fd = INVALID_HANDLE_VALUE;
}

int fsys::inherit(fd_t& fd, bool enable)
{
    unsigned long flags;
    if(fd > -1) {
        flags = fcntl(fd, F_GETFL);
        if(enable)
            flags &= ~FD_CLOEXEC;
        else
            flags |= FD_CLOEXEC;
        if(fcntl(fd, F_SETFL, flags))
            return remapError();
    }
    return 0;
}

void fsys::operator=(fd_t from)
{
    close();
    if(fd == INVALID_HANDLE_VALUE && from != INVALID_HANDLE_VALUE) {
        fd = ::dup(from);
        if(fd == INVALID_HANDLE_VALUE)
            error = remapError();
    }
}

void fsys::operator=(const fsys& from)
{
    close();
    if(fd == INVALID_HANDLE_VALUE && from.fd != INVALID_HANDLE_VALUE) {
        fd = ::dup(from.fd);
        if(fd == INVALID_HANDLE_VALUE)
            error = remapError();
    }
}

int fsys::drop(offset_t size)
{
#ifdef  HAVE_POSIX_FADVISE
    if(posix_fadvise(fd, (off_t)0, size, POSIX_FADV_DONTNEED)) {
        error = remapError();
        return error;
    }
    return 0;
#else
    error = ENOSYS;
    return ENOSYS;
#endif
}

int fsys::seek(offset_t pos)
{
    unsigned long rpos = pos;
    int mode = SEEK_SET;

    if(rpos == (unsigned long)end) {
        rpos = 0;
        mode = SEEK_END;
    }
    if(lseek(fd, rpos, mode) == ~0l) {
        error = remapError();
        return error;
    }
    return 0;
}

#endif

fsys::fsys()
{
    fd = INVALID_HANDLE_VALUE;
    ptr = NULL;
    error = 0;
}

fsys::fsys(const char *path, access_t access)
{
    fd = INVALID_HANDLE_VALUE;
    ptr = NULL;
    error = 0;
    open(path, access);
}


fsys::fsys(const char *path, unsigned fmode, access_t access)
{
    fd = INVALID_HANDLE_VALUE;
    ptr = NULL;
    error = 0;
    open(path, fmode, access);
}

fsys::~fsys()
{
    close();
}

void fsys::operator*=(fd_t& from)
{
    if(fd != INVALID_HANDLE_VALUE)
        close();
    fd = from;
    from = INVALID_HANDLE_VALUE;
}

int fsys::linkinfo(const char *path, char *buffer, size_t size)
{
#if defined(_MSWINDOWS_)
    HANDLE h;
    char reparse[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    DWORD rsize;

    if(!fsys::islink(path))
        return EINVAL;

    h = CreateFile(path, GENERIC_READ, 0, 0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);

    if(!h || h == INVALID_HANDLE_VALUE)
        return EINVAL;

    memset(reparse, 0, sizeof(reparse));
    LOCAL_REPARSE_DATA_BUFFER *rb = (LOCAL_REPARSE_DATA_BUFFER*)&reparse;

    if(!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, (LPVOID *)rb, MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &rsize, 0)) {
        CloseHandle(h);
        return remapError();
    }

#ifdef  UNICODE
    String::set(buffer, size, rb.PathBuffer);
#else
    WideCharToMultiByte(CP_THREAD_ACP, 0, rb->PathBuffer, rb->SubstituteNameLength / sizeof(WCHAR) + 1, buffer, size, "", FALSE);
#endif
    CloseHandle(h);
    return 0;
#elif defined(HAVE_READLINK)
    if(::readlink(path, buffer, size))
        return remapError();
    return 0;
#else
    return EINVAL;
#endif
}

int fsys::hardlink(const char *path, const char *target)
{
#ifdef _MSWINDOWS_
    if(!CreateHardLink(target, path, NULL))
        return remapError();
    return 0;
#else
    if(::link(path, target))
        return remapError();
    return 0;
#endif
}

int fsys::link(const char *path, const char *target)
{
#if defined(_MSWINDOWS_)
    TCHAR dest[512];
    HANDLE h;
    char reparse[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    char *part;
    DWORD size;
    WORD len;

    lstrcpy(dest, "\\??\\");
    if(!GetFullPathName(path, sizeof(dest) - (4 * sizeof(TCHAR)), &dest[4], &part) || GetFileAttributes(&dest[4]) == INVALID_FILE_ATTRIBUTES)
        return remapError();

    memset(reparse, 0, sizeof(reparse));
    LOCAL_REPARSE_DATA_BUFFER *rb = (LOCAL_REPARSE_DATA_BUFFER*)&reparse;

    if(!MultiByteToWideChar(CP_THREAD_ACP, MB_PRECOMPOSED, dest, lstrlenA(dest) + 1, rb->PathBuffer, lstrlenA(dest) + 1))
        return remapError();

    len = lstrlenW(rb->PathBuffer) * 2;
    rb->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    rb->ReparseDataLength = len + 12;
    rb->SubstituteNameLength = len;
    rb->PrintNameOffset = len + 2;
    h = CreateFile(target, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if(!h || h == INVALID_HANDLE_VALUE)
        return hardlink(path, target);
    if(!DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, (LPVOID)rb, rb->ReparseDataLength + FIELD_OFFSET(LOCAL_REPARSE_DATA_BUFFER, SubstituteNameOffset), NULL, 0, &size, 0)) {
        CloseHandle(h);
        return hardlink(path, target);
    }
    CloseHandle(h);
    return 0;
#elif defined(HAVE_SYMLINK)
    if(::symlink(path, target))
        return remapError();
    return 0;
#else
    if(::link(path, target))
        return remapError();
    return 0;
#endif
}

int fsys::unlink(const char *path)
{
#ifdef  _MSWINDOWS_
    HANDLE h = INVALID_HANDLE_VALUE;
    if(islink(path))
        h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if(!h || h != INVALID_HANDLE_VALUE) {
        REPARSE_GUID_DATA_BUFFER rb;
        memset(&rb, 0, sizeof(rb));
        DWORD size;
        rb.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
        if(!DeviceIoControl(h, FSCTL_DELETE_REPARSE_POINT, &rb,
            REPARSE_GUID_DATA_BUFFER_HEADER_SIZE, NULL, 0, &size, 0)) {
            CloseHandle(h);
            return remapError();
        }
        CloseHandle(h);
        ::remove(path);
        return 0;
    }
#endif
    if(::remove(path))
        return remapError();
    return 0;
}

int fsys::erase(const char *path)
{
    if(is_device(path))
        return ENOSYS;

    if(is_dir(path))
        return ENOENT;

    if(::remove(path))
        return remapError();
    return 0;
}

int fsys::remove(const char *path)
{
    if(is_device(path))
        return ENOSYS;

#ifdef  _MSWINDOWS_
    if(RemoveDirectory(path))
        return 0;
    int error = remapError();
    if(ENOTEMPTY)
        return ENOTEMPTY;
#else
    if(!::rmdir(path))
        return 0;
    if(errno != ENOTDIR)
        return errno;
#endif

    if(::remove(path))
        return remapError();
    return 0;
}

int fsys::copy(const char *oldpath, const char *newpath, size_t size)
{
    int result = 0;
    char *buffer = new char[size];
    fsys src, dest;
    ssize_t count = size;

    if(!buffer) {
        result = ENOMEM;
        goto end;
    }

    remove(newpath);

    src.open(oldpath, fsys::ACCESS_STREAM);
    if(!is(src))
        goto end;

    dest.open(newpath, GROUP_PUBLIC, fsys::ACCESS_STREAM);
    if(!is(dest))
        goto end;

    while(count > 0) {
        count = src.read(buffer, size);
        if(count < 0) {
            result = src.err();
            goto end;
        }
        if(count > 0)
            count = dest.write(buffer, size);
        if(count < 0) {
            result = dest.err();
            goto end;
        }
    }

end:
    if(is(src))
        src.close();

    if(is(dest))
        dest.close();

    if(buffer)
        delete[] buffer;

    if(result != 0)
        remove(newpath);

    return result;
}

int fsys::rename(const char *oldpath, const char *newpath)
{
    if(::rename(oldpath, newpath))
        return remapError();
    return 0;
}

int fsys::load(const char *path)
{
    fsys module;

    load(module, path);
#ifdef  _MSWINDOWS_
    if(module.mem) {
        module.mem = 0;
        return 0;
    }
    return remapError();
#else
    if(module.ptr) {
        module.ptr = 0;
        return 0;
    }
    return module.error;
#endif
}

bool fsys::is_file(const char *path)
{
#ifdef _MSWINDOWS_
    DWORD attr = GetFileAttributes(path);
    if(attr == (DWORD)~0l)
        return false;

    if(attr & FILE_ATTRIBUTE_DIRECTORY)
        return false;

    return true;

#else
    struct stat ino;

    if(::stat(path, &ino))
        return false;

    if(S_ISREG(ino.st_mode))
        return true;

    return false;
#endif
}

bool fsys::is_link(const char *path)
{
#if defined(_MSWINDOWS_)
    DWORD attr = GetFileAttributes(path);
    if (attr == 0xffffffff || !(attr & FILE_ATTRIBUTE_REPARSE_POINT))
        return false;
    return true;
#elif defined(HAVE_LSTAT)
    struct stat ino;

    if(::lstat(path, &ino))
        return false;

    if(S_ISLNK(ino.st_mode))
        return true;

    return false;
#else
    return false;
#endif
}

bool fsys::is_dir(const char *path)
{
#ifdef _MSWINDOWS_
    DWORD attr = GetFileAttributes(path);
    if(attr == (DWORD)~0l)
        return false;

    if(attr & FILE_ATTRIBUTE_DIRECTORY)
        return true;

    return false;

#else
    struct stat ino;

    if(::stat(path, &ino))
        return false;

    if(S_ISDIR(ino.st_mode))
        return true;

    return false;
#endif
}

#ifdef  _MSWINDOWS_

void fsys::load(fsys& module, const char *path)
{
    module.error = 0;
    module.mem = LoadLibrary(path);
    if(!module.mem)
        module.error = ENOEXEC;
}

void fsys::unload(fsys& module)
{
    if(module.mem)
        FreeLibrary(module.mem);
    module.mem = 0;
}

void *fsys::find(fsys& module, const char *sym)
{
    return (void *)GetProcAddress(module.mem, sym);
}

#elif defined(HAVE_DLFCN_H)
#include <dlfcn.h>

#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL 0
#endif

void fsys::load(fsys& module, const char *path)
{
    module.error = 0;
    module.ptr = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if(module.ptr == NULL)
        module.error = ENOEXEC;
}

void fsys::unload(fsys& module)
{
    if(module.ptr)
        dlclose(module.ptr);
    module.ptr = NULL;
}

void *fsys::find(fsys& module, const char *sym)
{
    if(!module.ptr)
        return NULL;

    return dlsym(module.ptr, (char *)sym);
}

#elif HAVE_MACH_O_DYLD_H
#include <mach-o/dyld.h>

void fsys::load(fsys& module, const char *path)
{
    NSObjectFileImage oImage;
    NSSymbol sym = NULL;
    NSModule mod;
    void (*init)(void);

    module.ptr = NULL;
    module.error = 0;

    if(NSCreateObjectFileImageFromFile(path, &oImage) != NSObjectFileImageSuccess) {
        module.error = ENOEXEC;
        return;
    }

    mod = NSLinkModule(oImage, path, NSLINKMODULE_OPTION_BINDNOW | NSLINKMODULE_OPTION_RETURN_ON_ERROR);
    NSDestroyObjectFileImage(oImage);
    if(mod == NULL) {
        module.error = ENOEXEC;
        return;
    }

    sym = NSLookupSymbolInModule(mod, "__init");
    if(sym) {
        init = (void (*)(void))NSAddressOfSymbol(sym);
        init();
    }
    module.ptr = (void *)mod;
}

void fsys::unload(fsys& module)
{
    if(!module.ptr)
        return;

    NSModule mod = (NSModule)module.ptr;
    NSSymbol sym;
    void (*fini)(void);
    module.ptr = NULL;

    sym = NSLookupSymbolInModule(mod, "__fini");
    if(sym != NULL) {
        fini = (void (*)(void))NSAddressOfSymbol(sym);
        fini();
    }
    NSUnlinkModule(mod, NSUNLINKMODULE_OPTION_NONE);
}

void *fsys::find(fsys& module, const char *sym)
{
    if(!module.ptr)
        return NULL;

    NSModule mod = (NSModule)module.ptr;
    NSSymbol sym;

    sym = NSLookupSymbolInModule(mod, sym);
    if(sym != NULL) {
        return NSAddressOfSymbol(sym);

    return NULL;
}

#elif HAVE_SHL_LOAD
#include <dl.h>

void fsys::load(fsys& module, const char *path)
{
    module.error = 0;
    module.ptr = (void *))shl_load(path, BIND_IMMEDIATE, 0l);
    if(!module.ptr)
        module.error = ENOEXEC;
}

void *fsys::find(fsys& module, const char *sym)
{
    shl_t image = (shl_t)module.ptr;

    if(shl_findsym(&image, sym, 0, &value) == 0)
        return (void *)value;

    return NULL;
}

void fsys::unload(fsys& module)
{
    shl_t image = (shl_t)module.ptr;
    if(addr)
        shl_unload(image);
    module.ptr = NULL;
}

#else

void fsys::load(fsys& module, const char *path)
{
    module.error = ENOEXEC;
    module.ptr = NULL;
}

void fsys::unload(mem_t addr)
{
}

void *fsys::find(mem_t addr, const char *sym)
{
    return NULL;
}

#endif

bool fsys::is_device(const char *path)
{
    if(!path)
        return false;

#ifndef _MSWINDOWS_
    if(is_dir(path))
        return false;

    if(!strncmp(path, "/dev/", 5))
        return true;

    return false;
#else
    if(path[1] == ':' && !path[2] && isalpha(*path))
        return true;

    if(!strncmp(path, "com", 3) || !strncmp(path, "lpt", 3)) {
        path += 3;
        while(isdigit(*path))
            ++path;
        if(!path || *path == ':')
            return true;
        return false;
    }

    if(!strncmp(path, "aux") || !strcmp(path, "prn")) {
        if(!path[3] || path[3] == ':')
            return true;
        return false;
    }

    if(!strncmp(path, "\\\\.\\", 4))
        return true;

    if(!strnicmp(path, "\\\\?\\Device\\", 12))
        return true;

    return false;

#endif
}

bool fsys::is_hidden(const char *path)
{
#ifdef  _MSWINDOWS_
    DWORD attr = GetFileAttributes(path);
    if(attr == (DWORD)~0l)
        return false;

    return ((attr & FILE_ATTRIBUTE_HIDDEN) != 0);
#else
    const char *cp = strrchr(path, '/');
    if(cp)
        ++cp;
    else
        cp = path;

    if(*cp == '.')
        return true;

    return false;
#endif
}

charfile::charfile(const char *file, const char *mode)
{
    fp = NULL;
    open(file, mode);
}

charfile::charfile()
{
    fp = NULL;
    opened = false;
}

charfile::~charfile()
{
    close();
}

bool charfile::is_tty(void)
{
#ifdef  _MSWINDOWS_
    if(_isatty(_fileno(fp)))
        return true;
#else
    if(isatty(fileno(fp)))
        return true;
#endif
    return false;
}

void charfile::open(const char *path, const char *mode)
{
    if(fp)
        fclose(fp);

    fp = fopen(path, mode);
    opened = true;
}

void charfile::close(void)
{
    if(fp && opened)
        fclose(fp);
    fp = NULL;
}

size_t charfile::printf(const char *format, ...)
{
    if(!fp)
        return 0;

    va_list args;
    va_start(args, format);
    size_t result = vfprintf(fp, format, args);
    va_end(args);
    if(result == (size_t)EOF)
        result = 0;
    return result;
}

size_t charfile::put(const char *data)
{
    if(!fp)
        return 0;

    int result = fputs(data, fp);
    if(result < 0)
        result = 0;

    return (size_t)result;
}

size_t charfile::readline(char *address, size_t size)
{
    address[0] = 0;

    if(!fp)
        return false;

    if(!fgets(address, size, fp) || feof(fp))
        return 0;

    size_t result = size = strlen(address);

    if(address[size - 1] == '\n') {
        --size;
        if(size && address[size - 1] == '\r')
            --size;
    }
    address[size] = 0;
    return result;
}

size_t charfile::readline(String& s)
{
    if(!s.c_mem())
        return true;

    if(!fgets(s.c_mem(), s.size(), fp) || feof(fp)) {
        s.clear();
        return false;
    }

    String::fix(s);
    size_t result = s.len();

    if(s[-1] == '\n')
        --s;

    if(s[-1] == '\r')
        --s;

    return result;
}

bool charfile::eof(void)
{
    if(!fp)
        return false;

    return feof(fp) != 0;
}

int charfile::err(void)
{
    if(!fp)
        return EBADF;

    return ferror(fp);
}

int charfile::_getch(void)
{
    if(!fp)
        return EOF;

    return fgetc(fp);
}

int charfile::_putch(int code)
{
    if(!fp)
        return EOF;

    return fputc(code, fp);
}

String str(charfile& so, strsize_t size)
{
    String s(size);
    so.readline(s.c_mem(), s.size());
    String::fix(s);
    return s;
}

fsys::fsys(fd_t handle)
{
    fd = handle;
    ptr = NULL;
    error = 0;
}

void fsys::set(fd_t handle)
{
    close();
    fd = handle;
    ptr = NULL;
    error = 0;
}

fd_t fsys::release(void)
{
    fd_t save = fd;

    fd = INVALID_HANDLE_VALUE;
    ptr = NULL;
    error = 0;
    return save;
}

int fsys::exec(const char *path, char **argv, char **envp)
{
    shell::pid_t pid = shell::spawn(path, argv, envp);
    return shell::wait(pid);
}

string_t fsys::prefix(void)
{
    char buf[256];

    prefix(buf, sizeof(buf));
    string_t s = buf;
    return s;
}
