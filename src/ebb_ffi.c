/* A ruby binding to the ebb web server
 * Copyright (c) 2008 Ry Dahl. This software is released under the MIT 
 * License. See README file for details.
 */
#include <ruby.h>
#include <rubyio.h>
#include <rubysig.h>
#include <assert.h>
#include <fcntl.h>

#define EV_STANDALONE 1
#include <ev.c>
#include "ebb.h"

/* Variables with a leading underscore are C-level variables */

#ifndef RSTRING_PTR
# define RSTRING_PTR(s) (RSTRING(s)->ptr)
# define RSTRING_LEN(s) (RSTRING(s)->len)
#endif

static char upcase[] =
  "\0______________________________"
  "_________________0123456789_____"
  "__ABCDEFGHIJKLMNOPQRSTUVWXYZ____"
  "__ABCDEFGHIJKLMNOPQRSTUVWXYZ____"
  "________________________________"
  "________________________________"
  "________________________________"
  "________________________________";

static VALUE cRequest;
static VALUE cConnection;
static VALUE waiting_requests;

/* You don't want to run more than one server per Ruby VM. Really
 * I'm making this explicit by not defining a Ebb::Server class but instead
 * initializing a single server and single event loop on module load.
 */
static ebb_server server;
struct ev_loop *loop;
struct ev_idle idle_watcher;

/* g is for global */
static VALUE g_fragment;
static VALUE g_path_info;
static VALUE g_query_string;
static VALUE g_request_body;
static VALUE g_request_method;
static VALUE g_request_path;
static VALUE g_request_uri;
static VALUE g_server_port;
static VALUE g_content_length;
static VALUE g_content_type;
static VALUE g_http_client_ip;
static VALUE g_http_prefix;
static VALUE g_http_version;

static VALUE g_COPY;
static VALUE g_DELETE;
static VALUE g_GET;
static VALUE g_HEAD;
static VALUE g_LOCK;
static VALUE g_MKCOL;
static VALUE g_MOVE;
static VALUE g_OPTIONS;
static VALUE g_POST;
static VALUE g_PROPFIND;
static VALUE g_PROPPATCH;
static VALUE g_PUT;
static VALUE g_TRACE;
static VALUE g_UNLOCK;

static void initialize_globals()
{
#define DEF_GLOBAL(N, val) g_##N = rb_obj_freeze(rb_str_new2(val)); rb_global_variable(&g_##N)
  DEF_GLOBAL(content_length, "CONTENT_LENGTH");
  DEF_GLOBAL(content_type, "CONTENT_TYPE");
  DEF_GLOBAL(fragment, "FRAGMENT");
  DEF_GLOBAL(path_info, "PATH_INFO");
  DEF_GLOBAL(query_string, "QUERY_STRING");
  DEF_GLOBAL(request_body, "REQUEST_BODY");
  DEF_GLOBAL(request_method, "REQUEST_METHOD");
  DEF_GLOBAL(request_path, "REQUEST_PATH");
  DEF_GLOBAL(request_uri, "REQUEST_URI");
  DEF_GLOBAL(server_port, "SERVER_PORT");
  DEF_GLOBAL(http_client_ip, "HTTP_CLIENT_IP");
  DEF_GLOBAL(http_prefix, "HTTP_");
  DEF_GLOBAL(http_version, "HTTP_VERSION");

  DEF_GLOBAL(COPY, "COPY");
  DEF_GLOBAL(DELETE, "DELETE");
  DEF_GLOBAL(GET, "GET");
  DEF_GLOBAL(HEAD, "HEAD");
  DEF_GLOBAL(LOCK, "LOCK");
  DEF_GLOBAL(MKCOL, "MKCOL");
  DEF_GLOBAL(MOVE, "MOVE");
  DEF_GLOBAL(OPTIONS, "OPTIONS");
  DEF_GLOBAL(POST, "POST");
  DEF_GLOBAL(PROPFIND, "PROPFIND");
  DEF_GLOBAL(PROPPATCH, "PROPPATCH");
  DEF_GLOBAL(PUT, "PUT");
  DEF_GLOBAL(TRACE, "TRACE");
  DEF_GLOBAL(UNLOCK, "UNLOCK");
}

static void attach_idle_watcher()
{
  if(!ev_is_active(&idle_watcher)) {
    ev_idle_start (loop, &idle_watcher);
  }
}

static void detach_idle_watcher()
{
  ev_idle_stop(loop, &idle_watcher);
}

#define APPEND_ENV(NAME) \
  VALUE rb_request = (VALUE)request->data;  \
  VALUE env = rb_iv_get(rb_request, "@env_ffi"); \
  VALUE v = rb_hash_aref(env, g_##NAME); \
  if(v == Qnil) \
    rb_hash_aset(env, g_##NAME, rb_str_new(at, len)); \
  else \
    rb_str_cat(v, at, len);

static void request_path(ebb_request *request, const char *at, size_t len)
{
  APPEND_ENV(request_path);
}

static void query_string(ebb_request *request, const char *at, size_t len)
{
  APPEND_ENV(query_string);
}

static void request_uri(ebb_request *request, const char *at, size_t len)
{
  APPEND_ENV(request_uri);
}

static void fragment(ebb_request *request, const char *at, size_t len)
{
  APPEND_ENV(fragment);
}

/* very ugly... */
static void header_field(ebb_request *request, const char *at, size_t len, int header_index)
{
  VALUE rb_request = (VALUE)request->data; 
  VALUE field = rb_iv_get(rb_request, "@field_in_progress");
  VALUE value = rb_iv_get(rb_request, "@value_in_progress");


  if( (field == Qnil && value == Qnil)
   || (field != Qnil && value != Qnil)
    ) 
  {
    if(field != Qnil) {
      VALUE env = rb_iv_get(rb_request, "@env_ffi");
      rb_hash_aset(env, field, value);
    }

    // prefix with HTTP_
    VALUE f = rb_str_new(NULL, RSTRING_LEN(g_http_prefix) + len);
    memcpy( RSTRING_PTR(f)
          , RSTRING_PTR(g_http_prefix)
          , RSTRING_LEN(g_http_prefix)
          );
    int i;
    // normalize
    for(i = 0; i < len; i++) {
      char *ch = RSTRING_PTR(f) + RSTRING_LEN(g_http_prefix) + i;
      *ch = upcase[at[i]];
    }
    rb_iv_set(rb_request, "@field_in_progress", f);
    rb_iv_set(rb_request, "@value_in_progress", Qnil);

  } else if(field != Qnil) {
    // nth pass n!= 1
    rb_str_cat(field, at, len);

  } else {
   assert(0 && "field == Qnil && value != Qnil"); 
  }

}

static void header_value(ebb_request *request, const char *at, size_t len, int _)
{
  VALUE rb_request = (VALUE)request->data; 
  VALUE v = rb_iv_get(rb_request, "@value_in_progress");
  if(v == Qnil)
    rb_iv_set(rb_request, "@value_in_progress", rb_str_new(at, len));
  else
    rb_str_cat(v, at, len);
}

static void headers_complete(ebb_request *request)
{
  VALUE rb_request = (VALUE)request->data; 
  VALUE env = rb_iv_get(rb_request, "@env_ffi");

  /* set REQUEST_METHOD. yuck */
  VALUE method;
  switch(request->method) {
  case EBB_COPY      : method = g_COPY      ; break;
  case EBB_DELETE    : method = g_DELETE    ; break;
  case EBB_GET       : method = g_GET       ; break;
  case EBB_HEAD      : method = g_HEAD      ; break;
  case EBB_LOCK      : method = g_LOCK      ; break;
  case EBB_MKCOL     : method = g_MKCOL     ; break;
  case EBB_MOVE      : method = g_MOVE      ; break;
  case EBB_OPTIONS   : method = g_OPTIONS   ; break;
  case EBB_POST      : method = g_POST      ; break;
  case EBB_PROPFIND  : method = g_PROPFIND  ; break;
  case EBB_PROPPATCH : method = g_PROPPATCH ; break;
  case EBB_PUT       : method = g_PUT       ; break;
  case EBB_TRACE     : method = g_TRACE     ; break;
  case EBB_UNLOCK    : method = g_UNLOCK    ; break;
  }
  rb_hash_aset(env, g_request_method, method);

  /* set PATH_INFO */
  rb_hash_aset(env, g_path_info, rb_hash_aref(env, g_request_path));

  /* set SERVER_PORT */
  char *server_port = ebb_request_connection(request)->server->port;
  if(server_port)
    rb_hash_aset(env, g_server_port, rb_str_new2(server_port));

  /* set HTTP_CLIENT_IP */
  char *client_ip = ebb_request_connection(request)->ip;
  if(client_ip)
    rb_hash_aset(env, g_http_client_ip, rb_str_new2(client_ip));

  /* set HTTP_VERSION */
  VALUE version = rb_str_buf_new(11);
  sprintf(RSTRING_PTR(version), "HTTP/%d.%d", request->version_major, request->version_minor);
  rb_str_set_len(version, strlen(RSTRING_PTR(version)));
  rb_hash_aset(env, g_http_version, version);

  rb_ary_push(waiting_requests, rb_request);
  // TODO set to detached if it has body
  attach_idle_watcher();
}

static void body_handler(ebb_request *request, const char *at, size_t length)
{
  // TODO push to @body_parts inside rb_request
  ;
}

static void request_complete(ebb_request *request)
{
  // TODO anything?
  ;
}

static ebb_request* new_request(ebb_connection *connection)
{
  ebb_request *request = malloc(sizeof(ebb_request));
  ebb_request_init(request);
  request->request_path = request_path;
  request->query_string = query_string;
  request->request_uri = request_uri;
  request->fragment = fragment;
  request->header_field = header_field;
  request->header_value = header_value;
  request->headers_complete = headers_complete;
  request->body_handler = body_handler;
  request->request_complete = request_complete;

  VALUE rb_request = Data_Wrap_Struct(cRequest, 0, free, request);
  rb_iv_set(rb_request, "@connection", (VALUE)connection->data);
  rb_iv_set(rb_request, "@env_ffi", rb_hash_new());
  rb_iv_set(rb_request, "@value_in_progress", Qnil);
  rb_iv_set(rb_request, "@field_in_progress", Qnil);
  request->data = (void*)rb_request;

  return request;
}

static int on_writable(ebb_connection *connection)
{
  VALUE rb_connection = (VALUE)connection->data;

  VALUE to_write = rb_iv_get(rb_connection, "@to_write");
  VALUE first = RARRAY_PTR(to_write)[0];

  VALUE buffer = RARRAY_PTR(first)[0];
  VALUE after_action = RARRAY_PTR(first)[1];
  int written = FIX2INT( RARRAY_PTR(first)[2] );

  /* TODO use writev here */

  ssize_t sent = write( connection->fd
                      , RSTRING_PTR(buffer) + written
                      , RSTRING_LEN(buffer) - written
                      );
  if(written == 0) goto error; /* XXX is this the write thing to do? */
  if(written < 0) goto error;

  written += sent;

  assert(written < RSTRING_LEN(buffer));

  if(written == sent) {
    /* done with this chunk */
    rb_ary_shift(to_write);
    if(RARRAY_LEN(to_write) == 0) {
      /* totally done - execute after action */
      if(after_action != Qnil)
        rb_funcall(after_action, rb_intern("send"), 0);
      return EBB_STOP;
    }
  } else {
    /* try again next time */
    RARRAY_PTR(first)[2] = INT2FIX(written);
  }

  return EBB_AGAIN;

error:
  ebb_connection_close(connection);
  return EBB_STOP;
}


static ebb_connection* 
new_connection(ebb_server *server, struct sockaddr_in *addr)
{
  ebb_connection *connection = malloc(sizeof(ebb_connection));

  ebb_connection_init(connection, 30.0);
  connection->new_request = new_request;
  connection->on_writable = on_writable;
  connection->free = (void (*)(ebb_connection*))free;

  VALUE rb_connection = Data_Wrap_Struct(cConnection, 0, 0, connection);
  rb_iv_set(rb_connection, "@to_write", rb_ary_new());
  connection->data = (void*)rb_connection;

  return connection;
}

static VALUE server_listen_on_fd(VALUE _, VALUE sfd)
{
  if(ebb_server_listen_on_fd(&server, FIX2INT(sfd)) < 0)
    rb_sys_fail("Problem listening on FD");
  return Qnil;
}

static VALUE server_listen_on_port(VALUE _, VALUE port)
{
  if(ebb_server_listen_on_port(&server, FIX2INT(port)) < 0)
    rb_sys_fail("Problem listening on port");
  return Qnil;
}

static struct timeval idle_timeout = { 0, 50000 };

static void
idle_cb (struct ev_loop *loop, struct ev_idle *w, int revents) {
  /* How to let other Ruby threads run while we're in this blocking C call */

  /* TODO: For Ruby 1.9 I should use rb_thread_blocking_region() instead of 
   * this hacky idle_cb
   */
  
//  if(ebb_server_clients_in_use_p(server)) {
//    /* If ruby has control of any clients - that means there are some requests
//     * still being processed inside of threads. We need to allow Ruby some
//     * time to work on these threads so we call rb_thread_schedule()
//     * I don't use rb_thread_select() here because it is very slow.
//     */
//    rb_thread_schedule();
//
//  } else 
  if(!rb_thread_alone()) {
    /* If no clients are in use, but there are still other Ruby threads then
     * some other thread is running in the Ruby VM which is not a request.
     * This is a sub-optimal situation and we solve it by calling 
     * rb_thread_select() to wait for the server fd to wake up.
     * One should try to avoid entering this state.
     */
    fd_set server_fd_set;
    FD_ZERO(&server_fd_set);
    FD_SET(server.fd, &server_fd_set);
    rb_thread_select(server.fd+1, &server_fd_set, 0, 0, &idle_timeout);
  } else {
    /* Otherwise there are no other threads. We can detach the idle_watcher
     * and allow the server_process_connections() to block until the 
     * server fd wakes up. Because we don't use rb_thread_select() this
     * is quite fast.
     */
    detach_idle_watcher();
  }
}

static VALUE server_process_connections(VALUE _)
{
  TRAP_BEG;
  ev_loop(loop, EVLOOP_ONESHOT);
  TRAP_END;
  return Qnil;
}


static VALUE server_unlisten(VALUE _)
{
  ebb_server_unlisten(&server);
  return Qnil;
}

static VALUE server_open(VALUE _)
{
  return server.listening ? Qtrue : Qfalse;
}

static VALUE server_waiting_requests(VALUE _)
{
  return waiting_requests;
}

static VALUE connection_enable_on_writable(VALUE _, VALUE rb_connection) 
{
  ebb_connection *connection; 
  Data_Get_Struct(rb_connection, ebb_connection, connection);
  ebb_connection_enable_on_writable(connection);
  return Qnil;
}

static VALUE connection_close(VALUE _, VALUE rb_connection) 
{
  ebb_connection *connection; 
  Data_Get_Struct(rb_connection, ebb_connection, connection);
  ebb_connection_close(connection);
  return Qnil;
}

void Init_ebb_ffi()
{
  VALUE mEbb = rb_define_module("Ebb");
  VALUE mFFI = rb_define_module_under(mEbb, "FFI");

  initialize_globals();

  rb_define_const(mFFI, "VERSION", rb_str_new2("BLANK"));
  
  rb_define_singleton_method(mFFI, "server_process_connections", server_process_connections, 0);
  rb_define_singleton_method(mFFI, "server_listen_on_fd", server_listen_on_fd, 1);
  rb_define_singleton_method(mFFI, "server_listen_on_port", server_listen_on_port, 1);
  rb_define_singleton_method(mFFI, "server_unlisten", server_unlisten, 0);
  rb_define_singleton_method(mFFI, "server_open?", server_open, 0);
  rb_define_singleton_method(mFFI, "server_waiting_requests", server_waiting_requests, 0);
  rb_define_singleton_method(mFFI, "connection_enable_on_writable", connection_enable_on_writable, 1);
  rb_define_singleton_method(mFFI, "connection_close", connection_close, 1);
  
  cRequest = rb_define_class_under(mEbb, "Request", rb_cObject);
  cConnection = rb_define_class_under(mEbb, "Connection", rb_cObject);
  
  /* initialize ebb_server */
  loop = ev_default_loop (0);
  ebb_server_init(&server, loop);
  
  ev_idle_init (&idle_watcher, idle_cb);
  attach_idle_watcher();
  
  waiting_requests = rb_ary_new();
  rb_iv_set(mFFI, "@waiting_requests", waiting_requests);

  server.new_connection = new_connection;
}
