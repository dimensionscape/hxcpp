#include <hxcpp.h>
#include <map>
#include <unordered_map>
#include <vector>

#ifdef ANDROID
#include <android/log.h>
#endif


namespace hx
{

// Class names are permanent strings whose hashes are precomputed, so a
// hash map turns Resolve into a bucket probe instead of O(log N) string
// compares down a red-black tree.
struct ClassNameHash
{
   size_t operator()(const String &inName) const { return inName.hash(); }
};
struct ClassNameEq
{
   bool operator()(const String &inA, const String &inB) const { return inA==inB; }
};
typedef std::unordered_map<String,Class,ClassNameHash,ClassNameEq> ClassMap;
static ClassMap *sClassMap = 0;

// Dense registration-order list for the GC walks - iterating the hash map
// chases a bucket chain per class every collect.  Must stay in sync with
// the map: a class only here would root its statics forever (cppia reload
// replaces classes), one only in the map would miss marking.
static std::vector<Class> *sAllClasses = 0;

// Called with the lock held.  Replaces inOld's slot when a name is
// re-registered so the previous class does not stay rooted.
static void sAddClass(const Class &inOld, const Class &inNew)
{
   if (sAllClasses==0)
      sAllClasses = new std::vector<Class>;
   if (inOld.mPtr)
   {
      for(int i=(int)sAllClasses->size()-1; i>=0; i--)
         if ((*sAllClasses)[i].mPtr==inOld.mPtr)
         {
            (*sAllClasses)[i] = inNew;
            return;
         }
   }
   if (inNew.mPtr)
      sAllClasses->push_back(inNew);
}

// Registration is single-threaded at boot, but cppia/scriptable modules can
// register classes at any time while other threads call Type.resolveClass -
// an unsynchronized map mutation is a torn-read crash.  Writes are rare, so
// a spin lock is plenty.  MarkClassStatics/VisitClassStatics run during the
// stop-the-world collect and need no lock: nothing inside the locked
// sections below can reach a GC safe point, so no thread is ever paused
// mid-mutation.
static volatile int sClassMapLock = 0;
struct ClassMapLock
{
   ClassMapLock()
   {
      while(_hx_atomic_compare_exchange(&sClassMapLock, 0, 1) != 0)
      {
         // Spin
      }
   }
   ~ClassMapLock() { sClassMapLock = 0; }
};

Class _hx_RegisterClass(const String &inClassName, CanCastFunc inCanCast,
                    String inStatics[], String inMembers[],
                    ConstructEmptyFunc inConstructEmpty, ConstructArgsFunc inConstructArgs,
                    Class *inSuperClass, ConstructEnumFunc inConstructEnum,
                    MarkFunc inMarkFunc
                    #ifdef HXCPP_VISIT_ALLOCS
                    ,VisitFunc inVisitFunc
                    #endif
                    #ifdef HXCPP_SCRIPTABLE
                    ,const hx::StorageInfo *inStorageInfo
                    ,const hx::StaticInfo *inStaticInfo
                    #endif
                    )
{
   Class_obj *obj = new Class_obj(inClassName, inStatics, inMembers,
                                  inConstructEmpty, inConstructArgs, inSuperClass,
                                  inConstructEnum, inCanCast, inMarkFunc
                                  #ifdef HXCPP_VISIT_ALLOCS
                                  ,inVisitFunc
                                  #endif
                                  #ifdef HXCPP_SCRIPTABLE
                                  ,inStorageInfo
                                  ,inStaticInfo
                                  #endif
                                  );
   Class c(obj);
   ClassMapLock lock;
   if (sClassMap==0)
      sClassMap = new ClassMap;
   Class &slot = (*sClassMap)[inClassName];
   sAddClass(slot, c);
   slot = c;
   return c;
}

void _hx_RegisterClass(const String &inClassName, Class inClass)
{
   ClassMapLock lock;
   if (sClassMap==0)
      sClassMap = new ClassMap;
   Class &slot = (*sClassMap)[inClassName];
   sAddClass(slot, inClass);
   slot = inClass;
}



// -------- Class ---------------------------------------


Class_obj::Class_obj(const String &inClassName,String inStatics[], String inMembers[],
             ConstructEmptyFunc inConstructEmpty, ConstructArgsFunc inConstructArgs,
             Class *inSuperClass,ConstructEnumFunc inConstructEnum,
             CanCastFunc inCanCast, MarkFunc inFunc
             #ifdef HXCPP_VISIT_ALLOCS
             ,VisitFunc inVisitFunc
             #endif
             #ifdef HXCPP_SCRIPTABLE
             ,const hx::StorageInfo *inStorageInfo
             ,const hx::StaticInfo *inStaticInfo
             #endif
             )
{
   mName = inClassName.makePermanent();
   mSuper = inSuperClass;
   mConstructEmpty = inConstructEmpty;
   mConstructArgs = inConstructArgs;
   mConstructEnum = inConstructEnum;
   mMarkFunc = inFunc;
   #ifdef HXCPP_VISIT_ALLOCS
   mVisitFunc = inVisitFunc;
   #endif

   #ifdef HXCPP_SCRIPTABLE
   mMemberStorageInfo = inStorageInfo;
   mStaticStorageInfo = inStaticInfo;
   #endif

   mStatics = dupFunctions(inStatics);
   mMembers = dupFunctions(inMembers);
   mCanCast = inCanCast;
}

bool Class_obj::GetNoStaticField(const String &inString, Dynamic &outValue, hx::PropertyAccess inCallProp)
{
   return false;
}
bool Class_obj::SetNoStaticField(const String &inString, Dynamic &ioValue, hx::PropertyAccess inCallProp)
{
   return false;
}



::Array< ::String > Class_obj::dupFunctions(String inFuncs[])
{
   if (!inFuncs)
      return null();

   int count = 0;
   for(String *s = inFuncs; s->length; s++)
      count++;

   Array<String> result = Array_obj<String>::__newConstWrapper(&inFuncs[0],count);

   return result;
}

void Class_obj::registerScriptable(bool inOverwrite)
{
   ClassMapLock lock;
   Class &slot = (*sClassMap)[ mName ];
   if (!inOverwrite && slot.mPtr)
      return;
   sAddClass(slot, Class(this));
   slot = this;
}

Class Class_obj::GetSuper()
{
   if (!mSuper)
      return null();
	if (mSuper==&hx::Object::__SGetClass())
		return null();
   return *mSuper;
}


Class Class_obj__mClass;

Class  Class_obj::__GetClass() const { return Class_obj__mClass; }
Class &Class_obj::__SGetClass() { return Class_obj__mClass; }

void Class_obj::__boot()
{
Static(Class_obj__mClass) = hx::_hx_RegisterClass(HX_CSTRING("Class"),TCanCast<Class_obj>,sNone,sNone, 0,0 , 0, 0 );
}


void Class_obj::MarkStatics(hx::MarkContext *__inCtx)
{
   HX_MARK_MEMBER(__meta__);
   HX_MARK_MEMBER(mInstanceFieldsCache);
   if (mMarkFunc)
       mMarkFunc(__inCtx);
}
#ifdef HXCPP_VISIT_ALLOCS
void Class_obj::VisitStatics(hx::VisitContext *__inCtx)
{
   HX_VISIT_MEMBER(__meta__);
   HX_VISIT_MEMBER(mInstanceFieldsCache);
   if (mVisitFunc)
       mVisitFunc(__inCtx);
}
#endif

Class Class_obj::Resolve(String inName)
{
   // A native host may resolve before any class has booted
   if (!sClassMap)
      return null();
   ClassMapLock lock;
   ClassMap::const_iterator i = sClassMap->find(inName);
   if (i==sClassMap->end())
   {
      // Class class...
      if (inName==HX_CSTRING("Enum"))
         return Class_obj__mClass;
      return null();
   }
   return i->second;
}

Dynamic Class_obj::ConstructEmpty()
{
   return mConstructEmpty();
}

Dynamic Class_obj::ConstructArgs(DynamicArray inArgs)
{
   return mConstructArgs(inArgs);
}

Dynamic Class_obj::ConstructEnum(String inName,DynamicArray inArgs)
{
   if (mConstructEnum==0)
      return null();
   return mConstructEnum(inName,inArgs);
}



String Class_obj::__ToString() const { return mName; }


Array<String> Class_obj::GetInstanceFields()
{
   // Class metadata is immutable after registration, but the dedupe below
   // is quadratic in the field count - build once and hand out copies
   // (callers may mutate the result)
   if (mInstanceFieldsCache.mPtr)
      return mInstanceFieldsCache->copy();

   Array<String> result = mSuper && (*mSuper).mPtr != this ? (*mSuper)->GetInstanceFields() : Array<String>(0,0);
   if (mMembers.mPtr)
      for(int m=0;m<mMembers->size();m++)
      {
         const String &mem = mMembers[m];
         if (result->Find(mem)==-1)
            result.Add(mem);
      }
   mInstanceFieldsCache = result;
   return result->copy();
}

Array<String> Class_obj::GetClassFields()
{
   Array<String> result = mStatics.mPtr ? mStatics->copy() : new Array_obj<String>(0,0);
   if (__rtti__.raw_ptr())
      result->push( HX_CSTRING("__rtti") );
   return result;
}

bool Class_obj::__HasField(const String &inString)
{
   if (__rtti__.raw_ptr() && inString==HX_CSTRING("__rtti"))
      return true;

   if (mStatics.mPtr)
      for(int s=0;s<mStatics->size();s++)
         if (mStatics[s]==inString)
            return true;
   return false;
}

hx::Val Class_obj::__Field(const String &inString, hx::PropertyAccess inCallProp)
{
   if (inString==HX_CSTRING("__meta__"))
      return __meta__;
   if (inString==HX_CSTRING("__rtti"))
      return __rtti__;

   if (mGetStaticField)
   {
      Dynamic result;
      if (mGetStaticField(inString,result,inCallProp))
         return result;
 
      // Throw ?
      return null();
   }

   // Not the most efficient way of doing this!
   if (!mConstructEmpty)
      return null();
   Dynamic instance = mConstructEmpty();
   return instance->__Field(inString, inCallProp);
}

hx::Val Class_obj::__SetField(const String &inString,const hx::Val &inValue, hx::PropertyAccess inCallProp)
{

   if (mSetStaticField)
   {
      Dynamic result = inValue;
      if (mSetStaticField(inString,result,inCallProp))
         return result;
 
      // Throw ?
      return inValue;
   }




   // Not the most efficient way of doing this!
   if (!mConstructEmpty)
      return null();
   Dynamic instance = mConstructEmpty();
   return instance->__SetField(inString,inValue, inCallProp);
}

bool Class_obj::__IsEnum()
{
   return mConstructEnum || this==GetVoidClass().GetPtr() || this==GetBoolClass().GetPtr();
}

#ifdef HXCPP_SCRIPTABLE
const hx::StorageInfo* Class_obj::GetMemberStorage(String inName)
{
   if (mMemberStorageInfo)
   {
      for(const StorageInfo *s = mMemberStorageInfo; s->offset; s++)
      {
         if (s->name == inName)
            return s;
      }
   }
   if (mSuper)
      return (*mSuper)->GetMemberStorage(inName);
   return 0;
}


const hx::StaticInfo* Class_obj::GetStaticStorage(String inName)
{
   if (mStaticStorageInfo)
   {
      for(const StaticInfo *s = mStaticStorageInfo; s->address; s++)
      {
         if (s->name == inName)
            return s;
      }
   }
   return 0;
}


#endif



void MarkClassStatics(hx::MarkContext *__inCtx)
{
   #ifdef HXCPP_DEBUG
   MarkPushClass("MarkClassStatics",__inCtx);
   #endif
   if (sAllClasses)
   {
      size_t count = sAllClasses->size();
      for(size_t i=0; i<count; i++)
      {
         Class_obj *c = (*sAllClasses)[i].mPtr;
         if (c->__meta__.mPtr || c->mMarkFunc || c->mInstanceFieldsCache.mPtr)
         {
            #ifdef HXCPP_DEBUG
            hx::MarkPushClass(c->mName.raw_ptr(),__inCtx);
            hx::MarkSetMember("statics",__inCtx);
            #endif

            c->MarkStatics(__inCtx);

            #ifdef HXCPP_DEBUG
            hx::MarkPopClass(__inCtx);
            #endif
         }
      }
   }
   #ifdef HXCPP_DEBUG
   MarkPopClass(__inCtx);
   #endif
}

#ifdef HXCPP_VISIT_ALLOCS

void VisitClassStatics(hx::VisitContext *__inCtx)
{
   HX_VISIT_MEMBER(Class_obj__mClass);
   // Must walk the same set MarkClassStatics marks - an object kept alive by
   // the mark but missed by the visit would not have its pointers updated
   // when the heap moves
   if (sAllClasses)
   {
      size_t count = sAllClasses->size();
      for(size_t i=0; i<count; i++)
         (*sAllClasses)[i]->VisitStatics(__inCtx);
   }
}

#endif


} // End namespace hx

Array<String> __hxcpp_get_class_list()
{
   // Copy the names under the lock (String copies do not allocate), then
   // build the GC array outside it so the lock never spans a safe point
   std::vector<String> names;
   if (hx::sClassMap)
   {
      hx::ClassMapLock lock;
      names.reserve(hx::sClassMap->size());
      for(hx::ClassMap::iterator i=hx::sClassMap->begin(); i!=hx::sClassMap->end(); ++i)
      {
         if (i->second.mPtr)
            names.push_back( i->first );
      }
   }
   Array<String> result = Array_obj<String>::__new(0, (int)names.size());
   for(size_t i=0;i<names.size();i++)
      result->push(names[i]);
   return result;
}


