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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "mysql.h"
#include <string.h>

#ifdef HX_ANDROID
#define atof(x) strtod((x),0)
#endif

/**
   <doc>
   <h1>MySQL</h1>
   <p>
   API to connect and use MySQL database
   </p>
   </doc>
**/


#define HXTHROW(x) hx::Throw(HX_CSTRING(x))




namespace
{

struct Connection : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdMysql };


   MYSQL *m;


   void create(MYSQL *inM)
   {
      m = inM;
      _hx_set_finalizer(this, finalize);
   }

   void destroy()
   {
      if (m)
      {
         mysql_close(m);
         m = 0;
      }
   }

   static void finalize(Dynamic obj)
   {
      ((Connection *)(obj.mPtr))->destroy();
   }
};

Connection *getConnection(Dynamic o)
{
   Connection *connection = dynamic_cast<Connection *>(o.mPtr);
   if (!connection || !connection->m)
      hx::Throw( HX_CSTRING("Invalid Connection") );
   return connection;
}


static void error( MYSQL *m, const char *msg )
{
   hx::Throw( String(msg) + HX_CSTRING(" ") + String(mysql_error(m)) );
}

// ---------------------------------------------------------------
// Result

/**
   <doc><h2>Result</h2></doc>
**/

#undef CONV_FLOAT
typedef enum {
   CONV_INT,       // fits an Int whatever its value
   CONV_STRING,
   CONV_FLOAT,
   CONV_BINARY,
   CONV_DATE,
   CONV_DATETIME,
   CONV_BOOL,
   CONV_INTEGER,   // signed, up to 64 bits: Int when it fits, Int64 when not
   CONV_UNSIGNED,  // unsigned, up to 64 bits: the same, text past Int64
   CONV_DECIMAL    // exact: text
} CONV;

struct Result : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdMysqlResult };

   MYSQL_RES *r;
   int nfields;
   CONV *fields_convs;
   // Held by the collector, not in malloc'd memory: a name computed by the
   // statement -- COUNT(*), CONCAT(...) -- is an ordinary string, since
   // making each one permanent would keep every one ever seen.
   Array<String> field_names;
   MYSQL_ROW current;

   void create(MYSQL_RES *inR)
   {
      r = inR;
      fields_convs = 0;
      field_names = null();
      nfields = 0;
      _hx_set_finalizer(this, finalize);
   }

   void destroy()
   {
      if (r)
      {
         if (fields_convs)
           free(fields_convs);
         fields_convs = 0;
         mysql_free_result(r);
         r = 0;
      }
   }

   int numRows() { return mysql_num_rows(r); }

   static void finalize(Dynamic obj)
   {
      ((Result *)(obj.mPtr))->destroy();
   }

   void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE { HX_MARK_MEMBER(field_names); }
   #ifdef HXCPP_VISIT_ALLOCS
   void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE { HX_VISIT_MEMBER(field_names); }
   #endif
};

Result *getResult(Dynamic o)
{
   Result *result = dynamic_cast<Result *>(o.mPtr);
   if (!result)
      HXTHROW("Invalid result");
   return result;
}

cpp::Function< Dynamic(Dynamic) > gDataToBytes;
cpp::Function< Dynamic(Float) > gDateFromSeconds;

// Days from 1970-01-01 to a date of the proleptic Gregorian calendar, for any
// year: the arithmetic mktime does, without its range or its time zone.
static long long days_from_civil( long long y, int m, int d )
{
   y -= m <= 2;
   long long era = (y >= 0 ? y : y - 399) / 400;
   long long yoe = y - era * 400;
   long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
   long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
   return era * 146097 + doe - 719468;
}

// An integer column at its full width: an Int when it fits one, which is
// what every INT column was already, and an Int64 when it does not.
static Dynamic integer_value( long long v )
{
   if( v >= -2147483647LL - 1 && v <= 2147483647LL )
      return (int)v;
   return Dynamic( (cpp::Int64)v );
}

/*
   DATE, DATETIME and TIMESTAMP, read as UTC, to the microsecond the text
   carries. They were read through mktime, in the local time zone, and DATE
   through an int: past 2038 it wrapped (2040-06-01 read as 1904-04-26), and
   on Windows mktime fails before 1970. A DATETIME holds no zone, and UTC is
   the one reading that maps every value to a distinct instant and back; a
   local reading skips the hour a clock springs forward. The zero dates MySQL
   allows (0000-00-00) are no date at all, and read as null.
*/
static Dynamic date_value( const char *s )
{
   int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
   int n = sscanf(s,"%d-%d-%d %d:%d:%d",&y,&mo,&d,&h,&mi,&sec);
   if( n < 3 || mo < 1 || mo > 12 || d < 1 || d > 31 )
      return null();

   double fraction = 0;
   const char *dot = strchr(s,'.');
   if( dot )
      fraction = atof(dot);

   double seconds = (double)days_from_civil(y,mo,d) * 86400.0 + h * 3600.0 + mi * 60.0 + sec + fraction;
   return gDateFromSeconds.call(seconds);
}

static Dynamic convert_value( CONV conv, const char *s, unsigned long length )
{
   switch( conv )
   {
      case CONV_INT:
         return atoi(s);
      case CONV_INTEGER:
         return integer_value(strtoll(s,0,10));
      case CONV_UNSIGNED:
         {
            unsigned long long v = strtoull(s,0,10);
            if( v > 9223372036854775807ULL )
               return String::create(s,(int)length);
            return integer_value((long long)v);
         }
      case CONV_DECIMAL:
         return String::create(s,(int)length);
      case CONV_BOOL:
         return *s != '0';
      case CONV_FLOAT:
         return atof(s);
      case CONV_BINARY:
         {
            Array<unsigned char> buf = Array_obj<unsigned char>::__new((int)length,(int)length);
            if( length )
               memcpy(&buf[0],s,length);
            return gDataToBytes.call(buf);
         }
      case CONV_DATE:
      case CONV_DATETIME:
         return date_value(s);
      case CONV_STRING:
      default:
         return String::create(s,(int)length);
   }
}

}

void _hx_mysql_set_conversion(
      cpp::Function< Dynamic(Dynamic) > inDataToBytes,
      cpp::Function< Dynamic(Float) > inDateFromSeconds )
{
   gDataToBytes = inDataToBytes;
   gDateFromSeconds = inDateFromSeconds;
}



/**
   result_get_length : 'result -> int
   <doc>Return the number of rows returned or affected</doc>
**/
int  _hx_mysql_result_get_length(Dynamic handle)
{
   if( handle->__GetType() == vtInt )
      return handle;

   return getResult(handle)->numRows();
}

/**
   result_get_nfields : 'result -> int
   <doc>Return the number of fields in a result row</doc>
**/
int  _hx_mysql_result_get_nfields(Dynamic handle)
{
   if( handle->__GetType() == vtInt )
     return 0;

   return getResult(handle)->nfields;
}

/**
   result_get_fields_names : 'result -> string array
   <doc>Return the fields names corresponding results columns</doc>
**/
Array<String> _hx_mysql_result_get_fields_names(Dynamic handle)
{
   // A statement that returns no rows answers with an OK packet, and its
   // handle is the affected-row count: it has no fields.
   if( handle->__GetType() == vtInt )
      return Array_obj<String>::__new(0);

   Result *r = getResult(handle);

   MYSQL_FIELD *fields = mysql_fetch_fields(r->r);
   int count = r->nfields;
   Array<String> output = Array_obj<String>::__new(count);

   for(int k=0;k<count;k++)
      output[k] = String(fields[k].name);

   return output;
}

/**
   result_next : 'result -> object?
   <doc>
   Return the next row if available. A row is represented
   as an object, which fields have been converted to the
   corresponding Neko value (int, float or string). For
   Date and DateTime you can specify your own conversion
   function using [result_set_conv_date]. By default they're
   returned as plain strings. Additionally, the TINYINT(1) will
   be converted to either true or false if equal to 0.
   </doc>
**/
Dynamic _hx_mysql_result_next(Dynamic handle)
{
   // The handle of an INSERT, UPDATE or DELETE is the Int the OK packet
   // carried, which has no rows. This threw "Invalid result" for it, so
   // iterating the result of any write -- as every generic caller does,
   // since it cannot know in advance -- reported a failure after the server
   // had applied the write.
   if( handle->__GetType() == vtInt )
      return null();

   Result *r = getResult(handle);
   MYSQL_ROW row = mysql_fetch_row(r->r);
   if( !row )
      return null();

   hx::Anon cur = hx::Anon_obj::Create(0);

   r->current = row;
   unsigned long *lengths = mysql_fetch_lengths(r->r);
   for(int i=0;i<r->nfields;i++)
   {
      Dynamic v;
      if( row[i] )
         v = convert_value(r->fields_convs[i],row[i],lengths ? lengths[i] : (unsigned long)strlen(row[i]));
      // SQL NULL is a value: the field is there, holding null. It was left
      // out of the row, so a NULL column and a misspelt one looked alike.
      cur->__SetField(r->field_names[i],v, hx::paccDynamic );
   }
   return cur;
}


/**
   result_get : 'result -> n:int -> string
   <doc>Return the [n]th field of the current row</doc>
**/
String  _hx_mysql_result_get(Dynamic handle,int n)
{
   Result *r = getResult(handle);
   if( n < 0 || n >= r->nfields )
      HXTHROW("Invalid index");

   if( !r->current )
   {
      _hx_mysql_result_next(handle);
      if( !r->current )
         HXTHROW("No more results");
   }

   return String(r->current[n]);
}

/**
   result_get_int : 'result -> n:int -> int
   <doc>Return the [n]th field of the current row as an integer (or 0)</doc>
**/
int _hx_mysql_result_get_int(Dynamic handle,int n)
{
   Result *r = getResult(handle);
   if( n < 0 || n >= r->nfields )
      HXTHROW("Invalid index");

   if( !r->current )
   {
      _hx_mysql_result_next(handle);
      if( !r->current )
         HXTHROW("No more results");
   }

   const char *s = r->current[n];
   return  s?atoi(s):0;
}

/**
   result_get_float : 'result -> n:int -> float
   <doc>Return the [n]th field of the current row as a float (or 0)</doc>
**/
Float   _hx_mysql_result_get_float(Dynamic handle,int n)
{
   Result *r = getResult(handle);
   if( n < 0 || n >= r->nfields )
      HXTHROW("Invalid index");

   if( !r->current )
   {
      _hx_mysql_result_next(handle);
      if( !r->current )
         HXTHROW("No more results");
   }

   const char *s = r->current[n];
   return s?atof(s):0;
}

static CONV convert_type( enum enum_field_types t, int flags, unsigned int length, int charset ) {
   switch( t ) {
   case FIELD_TYPE_TINY:
      if( length == 1 )
         return CONV_BOOL;
      return CONV_INT;
   case FIELD_TYPE_SHORT:
   case FIELD_TYPE_INT24:
      return CONV_INT;
   case FIELD_TYPE_LONG:
      // INT UNSIGNED goes to 4294967295, and atoi saturated it at 2^31 - 1.
      return (flags & UNSIGNED_FLAG) ? CONV_INTEGER : CONV_INT;
   case FIELD_TYPE_LONGLONG:
      // BIGINT was a Float, exact only to 2^53: every Snowflake id is past
      // it.
      return (flags & UNSIGNED_FLAG) ? CONV_UNSIGNED : CONV_INTEGER;
   case FIELD_TYPE_DECIMAL:
   case FIELD_TYPE_NEWDECIMAL:
      // Exact, as the column is; a Float was not.
      return CONV_DECIMAL;
   case FIELD_TYPE_FLOAT:
   case FIELD_TYPE_DOUBLE:
      return CONV_FLOAT;
   case FIELD_TYPE_DATETIME:
   case FIELD_TYPE_TIMESTAMP:
      return CONV_DATETIME;
   case FIELD_TYPE_DATE:
   case FIELD_TYPE_NEWDATE:
      return CONV_DATE;
   case FIELD_TYPE_BIT:
   case FIELD_TYPE_GEOMETRY:
      return CONV_BINARY;
   case FIELD_TYPE_VARCHAR:
   case FIELD_TYPE_VAR_STRING:
   case FIELD_TYPE_STRING:
   case FIELD_TYPE_TINY_BLOB:
   case FIELD_TYPE_MEDIUM_BLOB:
   case FIELD_TYPE_LONG_BLOB:
   case FIELD_TYPE_BLOB:
      // Bytes for the binary character set only. BINARY_FLAG is set on text
      // with a _bin collation too, so a VARCHAR ... COLLATE utf8mb4_bin came
      // back as Bytes.
      if( charset == 63 )
         return CONV_BINARY;
      return CONV_STRING;
   default:
      // TIME, YEAR, JSON, ENUM, SET: text.
      return CONV_STRING;
   }
}



static Result *alloc_result( Connection *c, MYSQL_RES *r )
{
   Result *res = new Result();
   res->create(r);

   int num_fields = mysql_num_fields(r);
   int i,j;
   MYSQL_FIELD *fields = mysql_fetch_fields(r);
   res->current = 0;
   res->nfields = num_fields;
   res->field_names = Array_obj<String>::__new(num_fields,num_fields);
   HX_OBJ_WB_GET(res, res->field_names.mPtr);
   res->fields_convs = (CONV*)malloc(sizeof(CONV)*num_fields);

   for(i=0;i<num_fields;i++)
   {
      String name;
      // The column's own name. One computed by the statement (COUNT(*) and
      // the like) was renamed '???', so two of them in one row overwrote
      // each other and neither could be read by name.
      if( strchr(fields[i].name,'(') )
         name = String::create(fields[i].name, -1);
      else
         name = String::createPermanent(fields[i].name, -1);

      res->field_names[i] = name;
      res->fields_convs[i] = convert_type(fields[i].type,fields[i].flags,fields[i].length,fields[i].charset);
   }

   return res;
}

// ---------------------------------------------------------------
// Connection

/** <doc><h2>Connection</h2></doc> **/

/**
   close : 'connection -> void
   <doc>Close the connection. Any subsequent operation will fail on it</doc>
**/
Dynamic _hx_mysql_close(Dynamic handle)
{
   Connection *connection = getConnection(handle);
   connection->destroy();
   return true;
}

/**
   select_db : 'connection -> string -> void
   <doc>Select the database</doc>
**/
void _hx_mysql_select_db(Dynamic handle,String db)
{
   Connection *connection = getConnection(handle);

   if( mysql_select_db(connection->m,db.utf8_str()) != 0 )
      error(connection->m,"Failed to select database :");
}

/**
   request : 'connection -> string -> 'result
   <doc>Execute an SQL request. Exception on error</doc>
**/
Dynamic _hx_mysql_request(Dynamic handle,String req)
{
   Connection *connection = getConnection(handle);

   // The length of the UTF-8 bytes, not of the string. req.length counts
   // UTF-16 units, so every extra byte of a non-ASCII character anywhere in
   // the query cut one byte off its end: an UPDATE naming a city with an
   // umlaut and ending WHERE id = 12 reached the server as WHERE id = 1.
   hx::strbuf sqlBuffer;
   int sqlBytes = 0;
   const char *sql = req.utf8_str(&sqlBuffer,true,&sqlBytes);

   if( mysql_real_query(connection->m,sql,sqlBytes) != 0 )
      error(connection->m,req);

   MYSQL_RES *res = mysql_store_result(connection->m);
   if( !res )
   {
      if( mysql_field_count(connection->m) == 0 )
         return mysql_affected_rows(connection->m);
      else
         error(connection->m,req);
   }

   return alloc_result(connection,res);
}


/**
   server_status : 'connection -> int
   <doc>The status flags the server sent with its last OK or EOF packet:
   SERVER_STATUS_IN_TRANS (1), SERVER_STATUS_AUTOCOMMIT (2),
   SERVER_STATUS_NO_BACKSLASH_ESCAPES (512) and the rest. Costs no round
   trip.</doc>
**/
int _hx_mysql_server_status(Dynamic handle)
{
   Connection *connection = getConnection(handle);
   return mysql_server_status(connection->m);
}

/**
   escape : 'connection -> string -> string
   <doc>Escape the string for inserting into a SQL request</doc>
**/
struct AutoBuf
{
   AutoBuf(int inLen) { buffer = new char[inLen]; }
   ~AutoBuf() { delete [] buffer; }
   char *buffer;
};


String  _hx_mysql_escape(Dynamic handle,String str)
{
   Connection *connection = getConnection(handle);
   // Sized and escaped by UTF-8 bytes, as the query is sent: a length in
   // UTF-16 units escaped only the first str.length bytes of the value and
   // left a buffer too small for the rest.
   hx::strbuf inBuffer;
   int inBytes = 0;
   const char *in = str.utf8_str(&inBuffer,true,&inBytes);
   int len = inBytes * 2 + 1;
   AutoBuf sout(len);

   int finalLen = mysql_real_escape_string(connection->m,sout.buffer,in,inBytes);
   if( finalLen < 0 )
      hx::Throw( HX_CSTRING("Unsupported charset : ") + String(mysql_character_set_name(connection->m)) );

   return String::create(sout.buffer,finalLen);
}

// ---------------------------------------------------------------
// Sql


/**
   connect : { host => string, port => int, user => string, pass => string, socket => string? } -> 'connection
   <doc>Connect to a database using the connection informations</doc>
**/
Dynamic _hx_mysql_connect(Dynamic params)
{
   String host = params->__Field(HX_CSTRING("host"), hx::paccDynamic );
   int    port = params->__Field(HX_CSTRING("port"), hx::paccDynamic);
   String user = params->__Field(HX_CSTRING("user"), hx::paccDynamic);
   String pass = params->__Field(HX_CSTRING("pass"), hx::paccDynamic);
   String socket = params->__Field(HX_CSTRING("socket"), hx::paccDynamic );

   MYSQL *cnx = mysql_init(NULL);
   if( mysql_real_connect(cnx,host.utf8_str(),user.utf8_str(),pass.utf8_str(),NULL,port,socket.utf8_str(),0) == NULL )
   {
      String error = HX_CSTRING("Failed to connect to mysql server : ") + String(mysql_error(cnx));
      mysql_close(cnx);
      hx::Throw(error);
   }

   Connection *connection = new Connection();
   connection->create(cnx);
   return connection;
}


