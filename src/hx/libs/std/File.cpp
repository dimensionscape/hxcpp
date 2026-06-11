#include <hxcpp.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include <hx/OS.h>

#ifdef NEKO_WINDOWS
#   include <windows.h>
#   include <io.h>
#endif

/**
   <doc>
   <h1>File</h1>
   <p>
   The file api can be used for different kind of file I/O.
   </p>
   </doc>
**/

namespace
{

struct fio : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdFio };

   String name;
   FILE   *io;
   bool   closeIo;

   void create(FILE *inFile, String inName, bool inClose)
   {
      name = inName;
      HX_OBJ_WB_GET(this,name.raw_ref());
      io = inFile;
      closeIo = inClose;

      _hx_set_finalizer(this, finalize);
   }

   void destroy(bool inForceClose = false)
   {
      if (io && (inForceClose || closeIo))
         fclose(io);
      io = 0;
      name = String();
   }

   void __Mark(hx::MarkContext *__inCtx) { HX_MARK_MEMBER(name); }
   #ifdef HXCPP_VISIT_ALLOCS
   void __Visit(hx::VisitContext *__inCtx) { HX_VISIT_MEMBER(name); }
   #endif

   static void finalize(Dynamic inObj)
   {
      ((fio *)(inObj.mPtr))->destroy();
   }

   String toString() { return HX_CSTRING("fio:") + name; }

};

fio *getFio(Dynamic handle, bool inRequireFile=true)
{
   fio *result = dynamic_cast<fio *>(handle.mPtr);
   if (!result || (!result->io && inRequireFile))
      hx::Throw( HX_CSTRING("Bad file handle") );
   return result;
}


static void file_error(const char *msg, String inName)
{
   hx::ExitGCFreeZone();
   Array<String> err = Array_obj<String>::__new(2,2);
   err[0] = String(msg);
   err[1] = inName;
   hx::Throw(err);
}

}

/**
   file_open : f:string -> r:string -> 'file
   <doc>
   Call the C function [fopen] with the file path and access rights. 
   Return the opened file or throw an exception if the file couldn't be open.
   </doc>
**/
Dynamic _hx_std_file_open( String fname, String r )
{
   FILE *file = 0;

   hx::strbuf buf0;
   hx::strbuf buf1;
#ifdef NEKO_WINDOWS
   hx::EnterGCFreeZone();
   file = _wfopen(fname.wchar_str(&buf0),r.wchar_str(&buf1));
#else
   hx::EnterGCFreeZone();
   file = fopen(fname.utf8_str(&buf0),r.utf8_str(&buf1));
#endif
   if (!file)
      file_error("file_open",fname);
   hx::ExitGCFreeZone();

   fio *f = new fio;
   f->create(file,fname,true);
   return f;
}

/**
   file_close : 'file -> void
   <doc>Close an file. Any other operations on this file will fail</doc> 
**/
void _hx_std_file_close( Dynamic handle )
{
   fio *fio = getFio(handle);
   fio->destroy(true);
}

/**
   file_name : 'file -> string
   <doc>Return the name of the file which was opened</doc>
**/
String hx_std_file_name( Dynamic handle )
{
   fio *fio = getFio(handle);
   return fio->name;
}

/**
   file_write : 'file -> s:string -> p:int -> l:int -> int
   <doc>
   Write up to [l] chars of string [s] starting at position [p]. 
   Returns the number of chars written which is >= 0.
   </doc>
**/
int _hx_std_file_write( Dynamic handle, Array<unsigned char> s, int p, int n )
{
   fio *f = getFio(handle);

   int buflen = s->length;
   int len = n;
   if( p < 0 || len < 0 || p > buflen || p + len > buflen )
      return 0;

   hx::AutoGCFreeZone zone;
#ifdef HX_WINDOWS
   if (_isatty(_fileno(f->io))) {
      fflush(f->io);
      HANDLE win_handle = (HANDLE)_get_osfhandle(_fileno(f->io));
      static const int MAX_CHARS = 4096;
      wchar_t buf[MAX_CHARS];
      int done = 0;
      bool convertOk = true;
      while (done < len) {
         // Convert in bounded chunks - a single oversized conversion fails
         // outright and previously reported the whole write as successful
         // while printing nothing.  Each utf8 byte yields at most one utf16
         // unit, so a chunk of MAX_CHARS bytes always fits the buffer.
         int chunk = len - done;
         if (chunk > MAX_CHARS) {
            chunk = MAX_CHARS;
            // Do not split a multi-byte utf8 sequence between chunks
            while (chunk > 0 && (s[p + done + chunk] & 0xC0) == 0x80)
               chunk--;
            if (chunk == 0)
               chunk = MAX_CHARS;
         }
         int result = MultiByteToWideChar(CP_UTF8, 0, (char *)&s[p + done], chunk, buf, MAX_CHARS);
         if (result == 0) {
            // Not convertible - write the remainder as raw bytes below
            convertOk = false;
            break;
         }
         DWORD written = 0;
         if(!WriteConsoleW(win_handle, buf, result, &written, NULL)) {
            file_error("file_write", f->name);
         }
         if ((int)written == result) {
            done += chunk;
            continue;
         }
         // Partial console write - report how many utf8 bytes were consumed
         // so the caller can retry the rest
         if (written < (DWORD)result) {
            wchar_t next = buf[written];
            if (next >= 0xDC00 && next <= 0xDFFF) {
               // Do not leave a surrogate pair half-written
               DWORD tmp;
               WriteConsoleW(win_handle, &buf[written], 1, &tmp, NULL);
               written += 1;
            }
         }
         int count = 0;
         for (DWORD i = 0; i < written; i++) {
            wchar_t ch = buf[i];
            if (ch <= 0x7F)
               count += 1;
            else if (ch <= 0x07FF)
               count += 2;
            else if (ch >= 0xD800 && ch <= 0xDFFF)
               count += 2;   // 2 utf8 bytes per half: a full pair is 4
            else
               count += 3;
         }
         return done + count;
      }
      if (convertOk)
         return len;
      p += done;
      len -= done;
   }
#endif
   while( len > 0 )
   {
      POSIX_LABEL(file_write_again);
      int d = (int)fwrite(&s[p],1,len,f->io);
      if( d <= 0 )
      {
         HANDLE_FINTR(f->io,file_write_again);
         file_error("file_write",f->name);
      }
      p += d;
      len -= d;
   }
   return n;
}

/**
   file_read : 'file -> s:string -> p:int -> l:int -> int
   <doc>
   Read up to [l] chars into the string [s] starting at position [p].
   Returns the number of chars readed which is > 0 (or 0 if l == 0).
   </doc>
**/
int _hx_std_file_read( Dynamic handle, Array<unsigned char> buf, int p, int n )
{
   fio *f = getFio(handle);

   int buf_len = buf->length;
   int len = n;
   if( p < 0 || len < 0 || p > buf_len || p + len > buf_len )
      return 0;

   hx::EnterGCFreeZone();
   // Attempt to increase the chances of pinning on the stack...
   unsigned char *bufPtr = &buf[0];
   while( len > 0 )
   {
      POSIX_LABEL(file_read_again);
      int d = (int)fread(bufPtr + p,1,len,f->io);
      if( d <= 0 )
      {
         int size = n - len;
         HANDLE_FINTR(f->io,file_read_again);
         if( size == 0 )
            file_error("file_read",f->name);
         hx::ExitGCFreeZone();
         return size;
      }
      p += d;
      len -= d;
   }
   hx::ExitGCFreeZone();
   return n;
}

/**
   file_write_char : 'file -> c:int -> void
   <doc>Write the char [c]. Error if [c] outside of the range 0..255</doc>
**/
void _hx_std_file_write_char( Dynamic handle, int c )
{
   fio *f = getFio(handle);
   if( c < 0 || c > 255 )
      return;
   char cc = (char)c;

   hx::EnterGCFreeZone();
   POSIX_LABEL(write_char_again);
   if( fwrite(&cc,1,1,f->io) != 1 )
   {
      HANDLE_FINTR(f->io,write_char_again);
      file_error("file_write_char",f->name);
   }
   hx::ExitGCFreeZone();
}

/**
   file_read_char : 'file -> int
   <doc>Read a char from the file. Exception on error</doc>
**/
int _hx_std_file_read_char( Dynamic handle )
{
   fio *f = getFio(handle);

   unsigned char cc = 0;
   hx::EnterGCFreeZone();
   POSIX_LABEL(read_char_again);
   if( fread(&cc,1,1,f->io) != 1 )
   {
      HANDLE_FINTR(f->io,read_char_again);
      file_error("file_read_char",f->name);
   }
   hx::ExitGCFreeZone();
   return cc;
}

/**
   file_seek : 'file -> pos:int -> mode:int -> void
   <doc>Use [fseek] to move the file pointer.</doc>
**/
void _hx_std_file_seek( Dynamic handle, int pos, int kind )
{
   fio *f = getFio(handle);
   hx::EnterGCFreeZone();
   if( fseek(f->io,pos,kind) != 0 )
      file_error("file_seek",f->name);
   hx::ExitGCFreeZone();
}

/**
   file_tell : 'file -> int
   <doc>Return the current position in the file</doc>
**/
int _hx_std_file_tell( Dynamic handle )
{
   fio *f = getFio(handle);
   hx::EnterGCFreeZone();
   int p = ftell(f->io);
   if( p == -1 )
      file_error("file_tell",f->name);
   hx::ExitGCFreeZone();
   return p;
}

/**
   file_eof : 'file -> bool
   <doc>Tell if we have reached the end of the file</doc>
**/
bool _hx_std_file_eof( Dynamic handle )
{
   fio *f = getFio(handle);
   return feof(f->io);
}

/**
   file_flush : 'file -> void
   <doc>Flush the file buffer</doc>
**/
void _hx_std_file_flush( Dynamic handle )
{
   fio *f = getFio(handle);
   hx::EnterGCFreeZone();
   if( fflush( f->io ) != 0 )
      file_error("file_flush",f->name);
   hx::ExitGCFreeZone();
}

/**
   file_contents : f:string -> string
   <doc>Read the content of the file [f] and return it.</doc>
**/

// 64-bit file length - plain ftell is 32-bit on Windows and the int cast
// silently wrapped huge files elsewhere, returning truncated content with
// no error.  Returns -1 on failure.
static long long file_length64(FILE *file)
{
#ifdef NEKO_WINDOWS
   if (_fseeki64(file,0,SEEK_END))
      return -1;
   return _ftelli64(file);
#else
   if (fseeko(file,0,SEEK_END))
      return -1;
   return ftello(file);
#endif
}

String _hx_std_file_contents_string( String name )
{
   hx::strbuf buf;
#ifdef NEKO_WINDOWS
   hx::EnterGCFreeZone();
   FILE *file = _wfopen(name.wchar_str(&buf), L"rb");
#else
   hx::EnterGCFreeZone();
   FILE *file = fopen(name.utf8_str(&buf), "rb");
#endif
   if(!file)
      file_error("file_contents",name);

   long long len64 = file_length64(file);
   if (len64<0 || len64>0x7ffffff0)
   {
      fclose(file);
      file_error(len64<0 ? "file_ftell" : "file_too_large",name);
   }
   int len = (int)len64;
   if (len==0)
   {
      fclose(file);
      hx::ExitGCFreeZone();
      return String::emptyString;
   }

   fseek(file,0,SEEK_SET);
   // Read straight into the GC string buffer - the old std::vector staging
   // copy doubled peak memory and added a full extra memcpy
   hx::ExitGCFreeZone();
   char *dest = hx::NewString(len);
   hx::EnterGCFreeZone();
   int total = len;
   int p = 0;
   while( len > 0 )
   {
      POSIX_LABEL(file_contents);
      int d = (int)fread(dest+p,1,len,file);
      if( d <= 0 )
      {
         HANDLE_FINTR(file,file_contents);
         fclose(file);
         file_error("file_contents",name);
      }
      p += d;
      len -= d;
   }
   fclose(file);
   hx::ExitGCFreeZone();

   // dest is already a GC string buffer - wrap it without re-copying.
   // Non-ASCII content still needs the utf16 conversion pass.
   #ifdef HX_SMART_STRINGS
   const unsigned char *c = (const unsigned char *)dest;
   for(int i=0;i<total;i++)
      if (c[i]>127)
         return _hx_utf8_to_utf16(c, total, false);
   #endif
   return String(dest, total);
}



/**
   file_contents : f:string -> string
   <doc>Read the content of the file [f] and return it.</doc>
**/
Array<unsigned char> _hx_std_file_contents_bytes( String name )
{
   hx::strbuf buf;
#ifdef NEKO_WINDOWS
   hx::EnterGCFreeZone();
   FILE *file = _wfopen(name.wchar_str(&buf), L"rb");
#else
   hx::EnterGCFreeZone();
   FILE *file = fopen(name.utf8_str(&buf), "rb");
#endif
   if(!file)
      file_error("file_contents",name);

   long long len64 = file_length64(file);
   if (len64<0 || len64>0x7ffffff0)
   {
      fclose(file);
      file_error(len64<0 ? "file_ftell" : "file_too_large",name);
   }
   int len = (int)len64;

   fseek(file,0,SEEK_SET);
   hx::ExitGCFreeZone();

   Array<unsigned char> buffer = Array_obj<unsigned char>::__new(len,len);
   hx::EnterGCFreeZone();
   if (len)
   {
      char *dest = (char *)&buffer[0];

      int p = 0;
      while( len > 0 )
      {
         POSIX_LABEL(file_contents1);
         int d = (int)fread(dest + p,1,len,file);
         if( d <= 0 )
         {
            HANDLE_FINTR(file,file_contents1);
            fclose(file);
            file_error("file_contents",name);
         }
         p += d;
         len -= d;
      }
   }
   fclose(file);
   hx::ExitGCFreeZone();
   return buffer;
}



Dynamic _hx_std_file_stdin()
{
   fio *f = new fio();
   f->create(stdin, HX_CSTRING("stdin"), false);
   return f;
}


Dynamic _hx_std_file_stdout()
{
   fio *f = new fio();
   f->create(stdout, HX_CSTRING("stdout"), false);
   return f;
}

Dynamic _hx_std_file_stderr()
{
   fio *f = new fio();
   f->create(stderr, HX_CSTRING("stderr"), false);
   return f;
}

