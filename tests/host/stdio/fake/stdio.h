/* Host test shim: the parts of UnixLib's FILE that fread.c and fwrite.c
   use, so their direct read()/write() loops run on the PC.  */
#ifndef FAKE_STDIO_H
#define FAKE_STDIO_H
#include <stddef.h>
#include <sys/types.h>
#define EOF (-1)
typedef struct
{
  struct { struct { unsigned int __read : 1, __write : 1; } __bits; } __mode;
  int __error, __eof, __pushedback, __pushedi_cnt, __pushedchar;
  int __string_istream, __linebuf, fd;
  unsigned char *i_base, *i_ptr, *o_base, *o_ptr;
  size_t i_cnt, o_cnt;
  off_t __offset;
} FILE;
size_t fread (void *, size_t, size_t, FILE *);
size_t fwrite (const void *, size_t, size_t, FILE *);
int __validfp (FILE *);
int __flslbbuf (void);
int __flsbuf (int, FILE *);
#define feof(f) ((f)->__eof)
#define ferror(f) ((f)->__error)
#define fileno(f) ((f)->fd)
int printf (const char *, ...);
#define strong_alias(a, b)
void *memrchr (const void *, int, size_t);
#endif
