/*
 * Copyright (C)2005-2012 Haxe Foundation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <hxcpp.h>
#include "sqlite3.h"
#include <stdlib.h>


// Put in anon-namespace to avoid conflicts if static-linked
namespace {



struct result : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdSqlite };

   sqlite3 *db;
   sqlite3_stmt *r;
   int ncols;
   int count;
   String *names;
   int *bools;
   int done;
   int first;

   void create(sqlite3 *inDb, sqlite3_stmt *inR, String sql)
   {
      _hx_set_finalizer(this, finalize);

      db = inDb;
      r = inR;

      ncols = sqlite3_column_count(r);
      names = (String *)malloc(sizeof(String)*ncols);
      bools = (int*)malloc(sizeof(int)*ncols);
      first = 1;
      done = 0;
      for(int i=0;i<ncols;i++)
      {
         names[i] = String::createPermanent(sqlite3_column_name(r,i),-1);
         for(int j=0;j<i;j++)
            if( names[j] == names[i] )
               hx::Throw(HX_CSTRING("Error, same field is two times in the request ") + sql);

         const char *dtype = sqlite3_column_decltype(r,i);
         bools[i] = dtype?(strcmp(dtype,"BOOL") == 0):0;
      }
   }

   static void finalize(Dynamic obj) { ((result *)(obj.mPtr))->destroy(false); }
   void destroy(bool inThrowError)
   {
      if (bools)
      {
         free(bools);
         bools = 0;
      }
      if (names)
      {
         free(names);
         names = 0;
      }
      if (r)
      {
         first = 0;
         done = 1;
         if( ncols == 0 )
            count = sqlite3_changes(db);

         __hxcpp_enter_gc_free_zone();
         bool err = sqlite3_finalize(r) != SQLITE_OK;
         __hxcpp_exit_gc_free_zone();
         db = 0;
         r = 0;

         if( err && inThrowError)
            hx::Throw(HX_CSTRING("Could not finalize request"));
      }
   }

   String toString() HXCPP_OVERRIDE { return HX_CSTRING("Sqlite Result"); }

 //static void finalize_result( result *r, int exc, bool throwError = true )
};



/**
   <doc>
   <h1>SQLite</h1>
   <p>
   Sqlite is a small embeddable SQL database that store all its data into
   a single file. See http://sqlite.org for more details.
   </p>
   </doc>
**/

struct database : public hx::Object
{
   sqlite3 *db;
   hx::ObjectPtr<result> last;

   void create(sqlite3 *inDb)
   {
      db = inDb;
      _hx_set_finalizer(this, finalize);
   }
   static void finalize(Dynamic obj) { ((database *)(obj.mPtr))->destroy(false); }
   void destroy(bool inThrowError)
   {
      if (db)
      {
         if (last.mPtr)
         {
            last->destroy(inThrowError);
            last = null();
         }

         __hxcpp_enter_gc_free_zone();
         int err = sqlite3_close(db);
         __hxcpp_exit_gc_free_zone();
         if (err != SQLITE_OK)
         {
            if (inThrowError)
               hx::Throw(HX_CSTRING("Sqlite: could not close"));
         }
         db = 0;
      }
   }


   void setResult(result *inResult)
   {
      if (last.mPtr)
         last->destroy(true);

      last = inResult;
      HX_OBJ_WB_GET(this, last.mPtr);
   }

   void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE { HX_MARK_MEMBER(last); }
   #ifdef HXCPP_VISIT_ALLOCS
   void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE { HX_VISIT_MEMBER(last); }
   #endif

   String toString() HXCPP_OVERRIDE { return HX_CSTRING("Sqlite Databse"); }
};

static void sqlite_error( sqlite3 *db ) {
   hx::Throw( HX_CSTRING("Sqlite error : ") + String(sqlite3_errmsg(db)) );
}

database *getDatabase(Dynamic handle)
{
   database *db = dynamic_cast<database *>(handle.mPtr);
   if (!db || !db->db)
      hx::Throw( HX_CSTRING("Invalid sqlite database") );
   return db;
}


result *getResult(Dynamic handle, bool inRequireStatement)
{
   result *r = dynamic_cast<result *>(handle.mPtr);
   if (!r || (inRequireStatement && !r->r))
      hx::Throw( HX_CSTRING("Invalid sqlite result") );
   return r;
}


} // End anon-namespace




/**
   connect : filename:string -> 'db
   <doc>Open or create the database stored in the specified file.</doc>
**/
Dynamic _hx_sqlite_connect(String filename)
{
   sqlite3 *sqlDb = 0;
   const char *filenameUtf8 = filename.utf8_str();
   __hxcpp_enter_gc_free_zone();
   int err = sqlite3_open(filenameUtf8,&sqlDb);
   __hxcpp_exit_gc_free_zone();
   if (err != SQLITE_OK)
      sqlite_error(sqlDb);

   database *db = new database();
   db->create(sqlDb);
   return db;
}


/**
   close : 'db -> void
   <doc>Closes the database.</doc>
**/
void _hx_sqlite_close(Dynamic handle)
{
   database *db = getDatabase(handle);
   db->destroy(true);
}

/**
   last_insert_id : 'db -> int
   <doc>Returns the last inserted auto_increment id.</doc>
**/
int     _hx_sqlite_last_insert_id(Dynamic handle)
{
   database *db = getDatabase(handle);
   // The glue signature is Int (matching the haxe std extern) - saturate
   // instead of silently wrapping a 64-bit rowid
   sqlite3_int64 id = sqlite3_last_insert_rowid(db->db);
   if( id > 0x7fffffff )
      return 0x7fffffff;
   return (int)id;
}

/**
   get_autocommit : 'db -> bool
   <doc>Whether the database is in autocommit mode, which is to say whether no
   transaction is open: sqlite3_get_autocommit. A transaction begun or ended
   with SQL text -- BEGIN, COMMIT, a ROLLBACK SQLite performed itself after an
   error -- is reflected here as surely as one begun through the API.</doc>
**/
bool _hx_sqlite_get_autocommit(Dynamic handle)
{
   database *db = getDatabase(handle);
   return sqlite3_get_autocommit(db->db) != 0;
}

/**
   request : 'db -> sql:string -> 'result
   <doc>Executes the SQL request and returns its result</doc>
**/
Dynamic _hx_sqlite_request(Dynamic handle,String sql)
{
   database *db = getDatabase(handle);

   int byteLength = 0;
   const char * sqlStr = sql.utf8_str(0, true, &byteLength);
   sqlite3_stmt *statement = 0;
   const char *tl = 0;
   __hxcpp_enter_gc_free_zone();
   int err = sqlite3_prepare(db->db,sqlStr,byteLength,&statement,&tl);
   __hxcpp_exit_gc_free_zone();
   if (err != SQLITE_OK)
   {
      hx::Throw( HX_CSTRING("Sqlite error in ") + sql + HX_CSTRING(" : ") +
                  String(sqlite3_errmsg(db->db) ) );
   }
   if( *tl )
   {
      __hxcpp_enter_gc_free_zone();
      sqlite3_finalize(statement);
      __hxcpp_exit_gc_free_zone();
      hx::Throw(HX_CSTRING("Cannot execute several SQL requests at the same time"));
   }

   int i,j;

   result *r = new result();
   r->create(db->db, statement,sql);

   db->setResult(r);

   return r;
}







/**
   result_get_length : 'result -> int
   <doc>Returns the number of rows in the result or the number of rows changed by the request.</doc>
**/
int  _hx_sqlite_result_get_length(Dynamic handle)
{
   result *r = getResult(handle,false);
   if( r->ncols != 0 )
      hx::Throw(HX_CSTRING("Getting change count from non-change request")); // ???
   return r->count;
}

/**
   result_get_nfields : 'result -> int
   <doc>Returns the number of fields in the result.</doc>
**/
int     _hx_sqlite_result_get_nfields(Dynamic handle)
{
   return getResult(handle,false)->ncols;
}

/**
   result_next : 'result -> object?
   <doc>Returns the next row in the result or [null] if no more result.</doc>
**/

Dynamic _hx_sqlite_result_next(Dynamic handle)
{
   result *r = getResult(handle,false);
   if( r->done )
      return null();

   __hxcpp_enter_gc_free_zone();
   int step = sqlite3_step(r->r);
   __hxcpp_exit_gc_free_zone();
   switch(step)
   {
      case SQLITE_ROW:
      {
         hx::Anon v = hx::Anon_obj::Create();
         r->first = 0;
         for(int i=0;i<r->ncols;i++)
         {
            Dynamic f;
            switch( sqlite3_column_type(r->r,i) )
            {
            case SQLITE_NULL:
               break;
            case SQLITE_INTEGER:
               if( r->bools[i] )
                  f = bool(sqlite3_column_int(r->r,i));
               else
               {
                  // INTEGER is 64-bit, and sqlite3_column_int kept the low
                  // 32: a millisecond timestamp, 1727600000000, read as
                  // 1023147008. An Int when the value fits one, as every
                  // value did before, and an Int64 when it does not -- not a
                  // Float, which is exact only to 2^53, short of a Snowflake
                  // id or a SUM() of them.
                  sqlite3_int64 v = sqlite3_column_int64(r->r,i);
                  if( v >= -2147483647LL - 1 && v <= 2147483647LL )
                     f = int(v);
                  else
                     f = Dynamic( (cpp::Int64)v );
               }
               break;
            case SQLITE_FLOAT:
               f = Float(sqlite3_column_double(r->r,i));
               break;
            case SQLITE_TEXT:
               f = String((char*)sqlite3_column_text(r->r,i));
               break;
            case SQLITE_BLOB:
               {
                  int size = sqlite3_column_bytes(r->r,i);
                  f = Array_obj<unsigned char>::fromData((const unsigned char *)sqlite3_column_blob(r->r,i),size);
                  break;
               }
            default:
               {
                  hx::Throw( HX_CSTRING("Unknown Sqlite type #") +
                               String((int)sqlite3_column_type(r->r,i)));
               }
            }
            v->__SetField(r->names[i],f,hx::paccDynamic);
         }
         return v;
      }
      case SQLITE_DONE:
         r->destroy(true);
         return null();
      default:
      {
         // The step failed: finalize now, and ignore what finalize says. With
         // the legacy sqlite3_prepare this uses, finalize returns the step's
         // error again, and the statement was left for the next request or
         // close to finalize with destroy(true) -- which threw that error as
         // "Could not finalize request", failing the connection's next,
         // unrelated statement. Finalizing first is also what gives SQLite's
         // own message: the step's is only "SQL logic error".
         sqlite3 *db = r->db;
         r->destroy(false);
         if( step == SQLITE_BUSY || step == SQLITE_LOCKED )
            hx::Throw( HX_CSTRING("Database is busy : ") + String(sqlite3_errmsg(db)) );
         sqlite_error(db);
      }
   }

   return null();
}


static sqlite3_stmt *prepStatement(Dynamic handle,int n)
{
   result *r = getResult(handle,true);
   if( n < 0 || n >= r->ncols )
      hx::Throw( HX_CSTRING("Sqlite: Invalid index") );

   if( r->first )
      _hx_sqlite_result_next(handle);

   if( r->done )
      hx::Throw( HX_CSTRING("Sqlite: no more results") );

   return r->r;
}

/**
   result_get : 'result -> n:int -> string
   <doc>Return the [n]th field of the current result row.</doc>
**/


String  _hx_sqlite_result_get(Dynamic handle,int n)
{
   sqlite3_stmt *r = prepStatement(handle,n);
   return String((char*)sqlite3_column_text(r,n));
}

/**
   result_get_int : 'result -> n:int -> int
   <doc>Return the [n]th field of the current result row as an integer.</doc>
**/
int     _hx_sqlite_result_get_int(Dynamic handle,int n)
{
   sqlite3_stmt *r = prepStatement(handle,n);
   return sqlite3_column_int(r,n);
}

/**
   result_get_float : 'result -> n:int -> float
   <doc>Return the [n]th field of the current result row as a float.</doc>
**/
Float   _hx_sqlite_result_get_float(Dynamic handle,int n)
{
   sqlite3_stmt *r = prepStatement(handle,n);
   return sqlite3_column_double(r,n);
}




