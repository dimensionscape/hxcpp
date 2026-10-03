#ifndef HX_GC_HELPERS_INCLUDED
#define HX_GC_HELPERS_INCLUDED

#if defined(HX_WINDOWS) && defined(HXCPP_ARM64)
// Eg, Microsoft Surface 
#define HXCPP_CAPTURE_SETJMP
#endif

#ifdef HXCPP_CAPTURE_SETJMP
   #include <setjmp.h>
#else

   #if (defined(HX_WINDOWS) || defined(HX_MACOS)  || (defined(HX_LINUX) && defined(__i386__))) && !defined(HXCPP_M64)
      #define HXCPP_CAPTURE_x86
   #endif

   #if (defined(HX_MACOS) || (defined(HX_WINDOWS) && !defined(HX_WINRT)) || defined(_XBOX_ONE) || (defined(HX_LINUX) && defined(__x86_64__)) ) && defined(HXCPP_M64)
      #define HXCPP_CAPTURE_x64
      #if !defined(__GNUC__)
         // RtlCaptureContext and CONTEXT
         #include <windows.h>
      #endif
   #endif

   #if defined(HXCPP_ARM64)
      //#define HXCPP_CAPTURE_ARM64
      // Awlays use setjmp on arm64
      #include <setjmp.h>
      #define HXCPP_CAPTURE_SETJMP
   #endif

#endif


namespace hx
{

// Capture Registers
//
#ifdef HXCPP_CAPTURE_SETJMP // {

typedef jmp_buf RegisterCaptureBuffer;

#define CAPTURE_REGS \
   setjmp(mRegisterBuf);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(&mRegisterBuf+1)

#elif defined(HXCPP_CAPTURE_x86) // } {

struct RegisterCaptureBuffer
{
   void *ebx;
   void *edi;
   void *esi;
};

void CaptureX86(RegisterCaptureBuffer &outBuffer);

#define CAPTURE_REGS \
   hx::CaptureX86(mRegisterBuf);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(&mRegisterBuf+1)

#elif defined(HXCPP_CAPTURE_x64) && !defined(__GNUC__) // }  {

// Windows x64 (MSVC). RBX, RBP, RDI, RSI and R12-R15 are callee-saved here, and a
// pointer the interrupted code holds only in one of them has to be found.
//
// RtlCaptureContext is called by the capturing function itself, not by a helper:
// a helper that needs its argument after the call keeps it in a callee-saved
// register, and so records its own value there instead of the caller's.
//
// The capturing function (PauseForCollect, EnterGCFreeZone, SetupStackAndCollect)
// saves the callee-saved registers it uses in its prologue and then reuses them,
// so some of its caller's values are only in that save area - and MSVC can put the
// 'dummy' local that marks the bottom of the scanned stack in the home area above
// it. So the words from the captured stack pointer up are copied as well: the
// capturing frame, save area included, and the few frames above it. They are
// copied rather than scanned in place because EnterGCFreeZone returns before a
// collection scans the thread, and its frame is reused by then.
enum { CAPTURE_FRAME_WORDS = 64 };

struct RegisterCaptureBuffer
{
   CONTEXT context;
   void    *frame[CAPTURE_FRAME_WORDS];
   // Zero until the first capture: a thread is marked from the time it attaches
   size_t  frameWords = 0;
};

// Copies the stack from ioBuffer.context.Rsp up, to at most inTopOfStack, into ioBuffer.frame
void CaptureX64Frame(RegisterCaptureBuffer &ioBuffer, int *inTopOfStack);

#define CAPTURE_REGS \
   RtlCaptureContext(&mRegisterBuf.context); \
   hx::CaptureX64Frame(mRegisterBuf, mTopOfStack);

#define CAPTURE_REG_START (int *)(&mRegisterBuf.context)
#define CAPTURE_REG_END (int *)(mRegisterBuf.frame + mRegisterBuf.frameWords)

#elif defined(HXCPP_CAPTURE_x64) // }  {


struct RegisterCaptureBuffer
{
   void *rbx;
   void *rbp;
   void *rdi;
   void *r12;
   void *r13;
   void *r14;
   void *r15;

   void *xmm[16*2];
};

void CaptureX64(RegisterCaptureBuffer &outBuffer);

#define CAPTURE_REGS \
   hx::CaptureX64(mRegisterBuf);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(&mRegisterBuf+1)


#elif defined(HXCPP_CAPTURE_ARM64) // }  {


struct RegisterCaptureBuffer
{
   void *x19;
   void *x20;
   void *x21;
   void *x22;
   void *x23;
   void *x24;
   void *x25;
   void *x26;
   void *x27;
   void *x28;
};

void CaptureArm64(RegisterCaptureBuffer &outBuffer);

#define CAPTURE_REGS \
   hx::CaptureArm64(mRegisterBuf);

#define CAPTURE_REG_START (int *)(&mRegisterBuf)
#define CAPTURE_REG_END (int *)(&mRegisterBuf+1)


#else //  }  default capture... {


class RegisterCapture
{
public:
	virtual int Capture(int *inTopOfStack,int **inBuf,int &outSize,int inMaxSize,int *inDummy);
   static RegisterCapture *Instance();
};

typedef int *RegisterCaptureBuffer[20];

#define CAPTURE_REGS \
   hx::RegisterCapture::Instance()->Capture(mTopOfStack, \
                mRegisterBuf,mRegisterBufSize,20,mBottomOfStack); \

#define CAPTURE_REG_START (int *)mRegisterBuf
#define CAPTURE_REG_END (int *)(mRegisterBuf+mRegisterBufSize)

#endif // }



}


#endif
