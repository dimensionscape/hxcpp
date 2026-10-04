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


/*
   The server's message and its error number and SQLSTATE -- never the
   statement. The message used to be prefixed with the whole SQL text, values
   and all, so a duplicate-key error carried an API token straight into the
   logs. The number and SQLSTATE are also kept for _hx_mysql_errno and
   _hx_mysql_sqlstate.
*/
static String describe_error( MYSQL *m )
{
   String message = String(mysql_error(m));
   int code = mysql_errno(m);
   if( code > 0 )
      message = message + HX_CSTRING(" (MySQL error ") + String(code) + HX_CSTRING(", SQLSTATE ") + String(mysql_sqlstate(m)) + HX_CSTRING(")");
   return message;
}

static void error( MYSQL *m, const char *msg )
{
   if( msg && *msg )
      hx::Throw( String(msg) + HX_CSTRING(" ") + describe_error(m) );
   hx::Throw( describe_error(m) );
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
   // For a result read a row at a time: the connection it reads from, held
   // so the connection outlives it, and the rows read so far.
   Dynamic owner;
   int rowsRead;

   void create(MYSQL_RES *inR)
   {
      r = inR;
      fields_convs = 0;
      field_names = null();
      nfields = 0;
      rowsRead = 0;
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

   void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE { HX_MARK_MEMBER(field_names); HX_MARK_MEMBER(owner); }
   #ifdef HXCPP_VISIT_ALLOCS
   void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE { HX_VISIT_MEMBER(field_names); HX_VISIT_MEMBER(owner); }
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
      fraction = _hx_strtod_c_locale(dot);

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
         // In the C locale whatever the process's is: the server writes a
         // point, and atof under a locale with a decimal comma read 1.5 as 1.
         return _hx_strtod_c_locale(s);
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

   Result *r = getResult(handle);
   // A result read as it arrives knows only the rows read so far.
   if( r->owner.mPtr )
      return r->rowsRead;
   return r->numRows();
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
   MYSQL_ROW row;
   if( r->owner.mPtr )
   {
      Connection *owner = dynamic_cast<Connection *>(r->owner.mPtr);
      if( !owner || !owner->m )
         HXTHROW("The connection this result reads from is closed");
      int failed = 0;
      row = mysql_fetch_row_stream(owner->m,r->r,&failed);
      if( !row )
      {
         if( failed )
            error(owner->m,0);
         return null();
      }
      r->rowsRead++;
   }
   else
      row = mysql_fetch_row(r->r);
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
   return s?_hx_strtod_c_locale(s):0;
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

   // At most the client's bound on columns, 65,535, which the server's
   // answer cannot get past: the count sized this before any column arrived.
   int num_fields = mysql_num_fields(r);
   int i,j;
   MYSQL_FIELD *fields = mysql_fetch_fields(r);
   res->current = 0;
   res->nfields = 0;
   res->field_names = Array_obj<String>::__new(num_fields,num_fields);
   HX_OBJ_WB_GET(res, res->field_names.mPtr);
   res->fields_convs = (CONV*)malloc(sizeof(CONV)*(num_fields > 0 ? num_fields : 1));
   if( !res->fields_convs )
      HXTHROW("Out of memory reading the server's answer");
   res->nfields = num_fields;

   for(i=0;i<num_fields;i++)
   {
      String name;
      const char *own = fields[i].name ? fields[i].name : "";
      // The column's own name. One computed by the statement (COUNT(*) and
      // the like) was renamed '???', so two of them in one row overwrote
      // each other and neither could be read by name.
      if( strchr(own,'(') )
         name = String::create(own, -1);
      else
         name = String::createPermanent(own, -1);

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
      error(connection->m,0);

   MYSQL_RES *res = mysql_store_result(connection->m);
   if( !res )
   {
      if( mysql_field_count(connection->m) == 0 )
         return mysql_affected_rows(connection->m);
      else
         error(connection->m,0);
   }

   return alloc_result(connection,res);
}


/**
   request_stream : 'connection -> string -> 'result
   <doc>Executes an SQL request whose rows are read as they arrive, one per
   [result_next], rather than all of them first. Another command on the
   connection before the last row reads the rest aside for this result to
   take back. The handle of a statement with no rows is its affected-row
   count, as for [request].</doc>
**/
Dynamic _hx_mysql_request_stream(Dynamic handle,String req)
{
   Connection *connection = getConnection(handle);

   hx::strbuf sqlBuffer;
   int sqlBytes = 0;
   const char *sql = req.utf8_str(&sqlBuffer,true,&sqlBytes);

   if( mysql_real_query(connection->m,sql,sqlBytes) != 0 )
      error(connection->m,0);

   MYSQL_RES *res = mysql_use_result(connection->m);
   if( !res )
   {
      if( mysql_field_count(connection->m) == 0 )
         return mysql_affected_rows(connection->m);
      else
         error(connection->m,0);
   }

   Result *result = alloc_result(connection,res);
   result->owner = handle;
   HX_OBJ_WB_GET(result, result->owner.mPtr);
   return result;
}

/**
   insert_id : 'connection -> int
   <doc>The AUTO_INCREMENT id the last statement generated, 0 when it generated
   none: an Int, or an Int64 past 2^31. From the OK packet, where it came
   free; reading it needed no SELECT LAST_INSERT_ID().</doc>
**/
Dynamic _hx_mysql_insert_id(Dynamic handle)
{
   return integer_value(mysql_insert_id(getConnection(handle)->m));
}

/**
   affected_rows : 'connection -> int
   <doc>The rows the last statement changed: an Int, or an Int64 past 2^31.</doc>
**/
Dynamic _hx_mysql_affected_rows(Dynamic handle)
{
   return integer_value(mysql_affected_rows64(getConnection(handle)->m));
}

/**
   server_version : 'connection -> string
   <doc>The version the server gave in its greeting.</doc>
**/
String _hx_mysql_server_version(Dynamic handle)
{
   return String(mysql_get_server_info(getConnection(handle)->m));
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
   create : { host => string, port => int, user => string, pass => string, socket => string?,
              sslMode => int?, sslCa => string?, serverPublicKey => string?,
              allowPublicKeyRetrieval => bool?, connectTimeout => float?,
              readTimeout => float?, writeTimeout => float?, keepAlive => bool?,
              keepAliveIdle => int?, keepAliveInterval => int?, keepAliveCount => int? } -> 'connection
   <doc>A connection, not yet open: open it with [open], and read why an open
   failed with [errno] and [sqlstate] before closing it.
   sslMode: 0 disabled (the default), 1 preferred, 2 required, 3 verify the
   certificate's chain against sslCa (a PEM file), 4 verify its name too.
   serverPublicKey: PEM of the server's RSA key, for caching_sha2_password
   and sha256_password without TLS; allowPublicKeyRetrieval lets the client
   ask the server for it instead, which a man in the middle could answer.
   Timeouts in seconds, 0 for none: connectTimeout is one deadline for the
   TCP connect and the handshake together (unset: the system's connect, then
   50 s for each read of the handshake); readTimeout and writeTimeout each
   read and write afterwards (unset: five hours). keepAlive turns TCP keepalive on, with the probe timing given or
   the system's.
   </doc>
**/
static char *copy_param(Dynamic params, const String &name)
{
   Dynamic value = params->__Field(name, hx::paccDynamic);
   if( value == null() )
      return 0;
   String text = value;
   if( !text.raw_ptr() || text.length == 0 )
      return 0;
   hx::strbuf buffer;
   return strdup(text.utf8_str(&buffer));
}

static double seconds_param(Dynamic params, const String &name, double unset)
{
   Dynamic value = params->__Field(name, hx::paccDynamic);
   return value == null() ? unset : (double)value;
}

static int int_param(Dynamic params, const String &name)
{
   Dynamic value = params->__Field(name, hx::paccDynamic);
   return value == null() ? 0 : (int)value;
}

Dynamic _hx_mysql_create(Dynamic params)
{
   // Copied to native memory: the connect blocks, and blocking calls run
   // outside the collector's sight, where a collected string may not be
   // read.
   Dynamic sslMode = params->__Field(HX_CSTRING("sslMode"), hx::paccDynamic);
   Dynamic allowRetrieval = params->__Field(HX_CSTRING("allowPublicKeyRetrieval"), hx::paccDynamic);
   Dynamic keepAlive = params->__Field(HX_CSTRING("keepAlive"), hx::paccDynamic);

   MYSQL *cnx = mysql_init(NULL);
   if( !cnx )
      HXTHROW("Out of memory creating a MySQL connection");
   mysql_set_endpoint(cnx,
      copy_param(params, HX_CSTRING("host")),
      int_param(params, HX_CSTRING("port")),
      copy_param(params, HX_CSTRING("user")),
      copy_param(params, HX_CSTRING("pass")),
      copy_param(params, HX_CSTRING("socket")));
   mysql_set_options(cnx,
      sslMode == null() ? 0 : (int)sslMode,
      copy_param(params, HX_CSTRING("sslCa")),
      copy_param(params, HX_CSTRING("serverPublicKey")),
      allowRetrieval != null() && (bool)allowRetrieval);
   mysql_set_timeouts(cnx,
      seconds_param(params, HX_CSTRING("connectTimeout"), -1),
      seconds_param(params, HX_CSTRING("readTimeout"), -1),
      seconds_param(params, HX_CSTRING("writeTimeout"), -1));
   mysql_set_keepalive(cnx,
      keepAlive != null() && (bool)keepAlive,
      int_param(params, HX_CSTRING("keepAliveIdle")),
      int_param(params, HX_CSTRING("keepAliveInterval")),
      int_param(params, HX_CSTRING("keepAliveCount")));

   Connection *connection = new Connection();
   connection->create(cnx);
   return connection;
}

/**
   open : 'connection -> void
   <doc>Connects and logs in. Throws the reason on failure; [errno] and
   [sqlstate] still answer for it until the connection is closed.</doc>
**/
void _hx_mysql_open(Dynamic handle)
{
   Connection *connection = getConnection(handle);
   if( mysql_open(connection->m) != 0 )
      hx::Throw( HX_CSTRING("Failed to connect to mysql server : ") + describe_error(connection->m) );
}

/**
   connect : 'params -> 'connection
   <doc>[create] and [open] in one: the connection, or the reason it could
   not be made. The parameters are [create]'s.</doc>
**/
Dynamic _hx_mysql_connect(Dynamic params)
{
   Dynamic handle = _hx_mysql_create(params);
   Connection *connection = getConnection(handle);
   if( mysql_open(connection->m) != 0 )
   {
      String error = HX_CSTRING("Failed to connect to mysql server : ") + describe_error(connection->m);
      connection->destroy();
      hx::Throw(error);
   }
   return handle;
}

/**
   ping : 'connection -> bool
   <doc>COM_PING: whether the server answers, at the cost of one small round
   trip rather than a statement.</doc>
**/
bool _hx_mysql_ping(Dynamic handle)
{
   Connection *connection = getConnection(handle);
   return mysql_ping(connection->m) == 0;
}

/**
   errno : 'connection -> int
   <doc>The MySQL error number of the last failure, 0 after a success: the
   server's (1062 a duplicate key, 1213 a deadlock) or the client's (2013 the
   connection lost or timed out).</doc>
**/
int _hx_mysql_errno(Dynamic handle)
{
   return mysql_errno(getConnection(handle)->m);
}

/**
   sqlstate : 'connection -> string
   <doc>The SQLSTATE of the last failure, "00000" after a success.</doc>
**/
String _hx_mysql_sqlstate(Dynamic handle)
{
   return String(mysql_sqlstate(getConnection(handle)->m));
}

/**
   thread_id : 'connection -> float
   <doc>The server's id for this connection, as KILL takes it.</doc>
**/
Float _hx_mysql_thread_id(Dynamic handle)
{
   return (Float)mysql_thread_id(getConnection(handle)->m);
}

/**
   is_tls : 'connection -> bool
   <doc>Whether the session runs over TLS.</doc>
**/
bool _hx_mysql_is_tls(Dynamic handle)
{
   return mysql_is_tls(getConnection(handle)->m) != 0;
}

/**
   keepalive : 'connection -> int array
   <doc>The TCP keepalive the connection's socket has, read back from the
   socket: on (1 or 0), then the idle and interval in seconds and the probe
   count, each -1 where the system does not report it; all four -1 once a
   failure has lost the connection.</doc>
**/
Array<int> _hx_mysql_keepalive(Dynamic handle)
{
   int state[4];
   mysql_keepalive_state(getConnection(handle)->m,state);
   Array<int> result = Array_obj<int>::__new(4,4);
   for(int i=0;i<4;i++)
      result[i] = state[i];
   return result;
}

/**
   auth_plugin : 'connection -> string
   <doc>The authentication plugin the account logged in with.</doc>
**/
String _hx_mysql_auth_plugin(Dynamic handle)
{
   return String(mysql_auth_plugin(getConnection(handle)->m));
}


