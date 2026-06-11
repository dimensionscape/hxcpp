#include <hxcpp.h>
#include <stdlib.h>
#include <string.h>
#include <hx/zip/Compress.hpp>
#include <hx/zip/Uncompress.hpp>

/**
   <doc>
   <h1>ZLib</h1>
   <p>
   Give access to the popular ZLib compression library, used in several file
   formats such as ZIP and PNG.
   </p>
   </doc>
**/

using namespace hx::zip;
using namespace cpp::marshal;

hx::zip::Result::Result(int inDone, int inRead, int inWrite) : done(inDone), read(inRead), write(inWrite) {}

/**
   deflate_init : level:int -> 'dstream
   <doc>Open a compression stream with the given level of compression</doc>
**/
Dynamic _hx_deflate_init(int level)
{
    return Compress_obj::create(level);
}

/**
   deflate_buffer : 'dstream -> src:string -> srcpos:int -> dst:string -> dstpos:int -> { done => bool, read => int, write => int }
**/
Dynamic _hx_deflate_buffer(Dynamic handle, Array<unsigned char> src, int srcPos, Array<unsigned char> dest, int destPos)
{
    // Keep the data buffers' allocation starts visible in this frame: zlib
    // runs inside a GC free zone holding raw pointers into them, and the
    // sliced views below are interior pointers, which do not pin a moving
    // collection.  (The array heads pin only themselves, not their data.)
    unsigned char * volatile pinSrc = (unsigned char *)src->getBase();
    unsigned char * volatile pinDst = (unsigned char *)dest->getBase();

    auto srcView = View<uint8_t>(src->getBase(), src->length).slice(srcPos);
    auto dstView = View<uint8_t>(dest->getBase(), dest->length).slice(destPos);
    auto result  = handle.StaticCast<Compress>()->execute(srcView, dstView);

    return
        hx::Anon_obj::Create(3)
            ->setFixed(0,HX_("write",df,6c,59,d0),result.write)
            ->setFixed(1,HX_("done",82,f0,6d,42),result.done)
            ->setFixed(2,HX_("read",56,4b,a7,4b),result.read);
}


/**
   deflate_bound : 'dstream -> n:int -> int
   <doc>Return the maximum buffer size needed to write [n] bytes</doc>
**/
int _hx_deflate_bound(Dynamic handle,int length)
{
    return handle.StaticCast<Compress>()->getBounds(length);
}

void _hx_deflate_end(Dynamic handle)
{
    handle.StaticCast<Compress>()->close();
}


/**
   set_flush_mode : 'stream -> string -> void
   <doc>Change the flush mode ("NO","SYNC","FULL","FINISH","BLOCK")</doc>
**/
void _hx_zip_set_flush_mode(Dynamic handle, String flushMode)
{
    Flush flush;
    if (flushMode == HX_CSTRING("NO"))
    {
        flush = Flush::None;
    }
    else if (flushMode == HX_CSTRING("SYNC"))
    {
        flush = Flush::Sync;
    }
    else if (flushMode == HX_CSTRING("FULL"))
    {
        flush = Flush::Full;
    }
    else if (flushMode == HX_CSTRING("FINISH"))
    {
        flush = Flush::Finish;
    }
    else if (flushMode == HX_CSTRING("BLOCK"))
    {
        flush = Flush::Block;
    }
    else
    {
        // Was an uninitialized read; neko/hl throw here too
        hx::Throw(HX_CSTRING("Bad flush mode : ") + flushMode);
        return;
    }

    handle.StaticCast<Zip>()->setFlushMode(flush);
}




/**
   inflate_init : window_size:int? -> 'istream
   <doc>Open a decompression stream</doc>
**/
Dynamic _hx_inflate_init(Dynamic windowBits)
{
    return Uncompress_obj::create(windowBits == null() ? 15 : (int)windowBits);
}

/**
   inflate_buffer : 'istream -> src:string -> srcpos:int -> dst:string -> dstpos:int -> { done => bool, read => int, write => int }
**/
Dynamic _hx_inflate_buffer(Dynamic handle, Array<unsigned char> src, int srcPos, Array<unsigned char> dest, int destPos)
{
    // See _hx_deflate_buffer - pin the data buffers for the free-zone call
    unsigned char * volatile pinSrc = (unsigned char *)src->getBase();
    unsigned char * volatile pinDst = (unsigned char *)dest->getBase();

    auto srcView = View<uint8_t>(src->getBase(), src->length).slice(srcPos);
    auto dstView = View<uint8_t>(dest->getBase(), dest->length).slice(destPos);
    auto result  = handle.StaticCast<Uncompress>()->execute(srcView, dstView);

    return
        hx::Anon_obj::Create(3)
            ->setFixed(0,HX_("write",df,6c,59,d0),result.write)
            ->setFixed(1,HX_("done",82,f0,6d,42),result.done)
            ->setFixed(2,HX_("read",56,4b,a7,4b),result.read);
}

/**
   inflate_end : 'istream -> void
   <doc>Close a decompression stream</doc>
**/
void _hx_inflate_end(Dynamic handle)
{
    handle.StaticCast<Uncompress>()->close();
}
