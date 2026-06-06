/* LibreSSL's posix_win.c references fstat64; mingw-w64 CRT only has _fstat64.
   Undef the macro before defining the real symbol so the linker can resolve it. */
#ifdef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#ifdef fstat64
#undef fstat64
#endif
int fstat64(int fd, struct _stat64 *buf) { return _fstat64(fd, buf); }
#endif
