/* A ruby binding to the ebb web server
 * Copyright (c) 2008 Ry Dahl. This software is released under the MIT 
 * License. See README file for details.
 */
#include <ruby.h>
#include <assert.h>
#include <fcntl.h>
#include <ebb.h>
#include <ev.h>

static VALUE cServer;
static VALUE cClient;
static VALUE eParserError;

static VALUE global_http_prefix;
static VALUE global_request_method;
static VALUE global_request_uri;
static VALUE global_fragment;
static VALUE global_request_path;
static VALUE global_query_string;
static VALUE global_http_version;
static VALUE global_request_body;
static VALUE global_server_name;
static VALUE global_server_port;
static VALUE global_path_info;
static VALUE global_content_length;
static VALUE global_http_host;

#define ASCII_UPPER(ch) ('a' <= ch && ch <= 'z' ? ch - 'a' + 'A' : ch)

VALUE client_new(ebb_client *_client)
{
  VALUE client = Data_Wrap_Struct(cClient, 0, 0, _client);
  // rb_iv_set(client, "@content_length", INT2FIX(_client->content_length));
  return client;
}

void request_cb(ebb_client *_client, void *data)
{
  VALUE server = (VALUE)data;
  VALUE waiting_clients;
  VALUE client = client_new(_client);
  
  waiting_clients = rb_iv_get(server, "@waiting_clients");
  rb_ary_push(waiting_clients, client);
}

VALUE server_alloc(VALUE self)
{
  struct ev_loop *loop = ev_default_loop (0);
  ebb_server *_server = ebb_server_alloc();
  VALUE server = Qnil;
  server = Data_Wrap_Struct(cServer, 0, ebb_server_free, _server);
  ebb_server_init(_server, loop, request_cb, (void*)server);
  return server; 
}


// VALUE server_initialize(VALUE x, VALUE server)
// {
//   struct ev_loop *loop = ev_default_loop (0);
//   ebb_server *_server;
//   
//   Data_Get_Struct(server, ebb_server, _server);
//   return Qnil;
// }


VALUE server_listen_on_port(VALUE x, VALUE server, VALUE port)
{
  ebb_server *_server;
  Data_Get_Struct(server, ebb_server, _server);
  int r = ebb_server_listen_on_port(_server, FIX2INT(port));
  return r < 0 ? Qfalse : Qtrue;
}


VALUE server_listen_on_socket(VALUE x, VALUE server, VALUE socketpath)
{
  ebb_server *_server;
  Data_Get_Struct(server, ebb_server, _server);
  int r = ebb_server_listen_on_socket(_server, StringValuePtr(socketpath));
  return r < 0 ? Qfalse : Qtrue;
}


static void
oneshot_timeout (struct ev_loop *loop, struct ev_timer *w, int revents) {;}


VALUE server_process_connections(VALUE x, VALUE server)
{
  ebb_server *_server;
  VALUE host, port;
  
  Data_Get_Struct(server, ebb_server, _server);
  
  ev_timer timeout;
  ev_timer_init (&timeout, oneshot_timeout, 0.5, 0.);
  ev_timer_start (_server->loop, &timeout);
   
  ev_loop(_server->loop, EVLOOP_ONESHOT);
  /* XXX: Need way to know when the loop is finished...
   * should return true or false */
   
  ev_timer_stop(_server->loop, &timeout);
  
  if(_server->open)
    return Qtrue;
  else
    return Qfalse;
}

VALUE server_unlisten(VALUE x, VALUE server)
{
  ebb_server *_server;
  Data_Get_Struct(server, ebb_server, _server);
  ebb_server_unlisten(_server);
  return Qnil;
}

/* Variables with an underscore are C-level variables */
VALUE env_field(struct ebb_env_item *item)
{
  VALUE f;
  switch(item->type) {
    case EBB_FIELD_VALUE_PAIR:
      f = rb_str_new(NULL, RSTRING_LEN(global_http_prefix) + item->field_length);
      memcpy( RSTRING_PTR(f)
            , RSTRING_PTR(global_http_prefix)
            , RSTRING_LEN(global_http_prefix)
            );
      int i;
      for(i = 0; i < item->field_length; i++) {
        char *ch = RSTRING_PTR(f) + RSTRING_LEN(global_http_prefix) + i;
        *ch = item->field[i] == '-' ? '_' : ASCII_UPPER(item->field[i]);
      }
      return f;
    case EBB_REQUEST_METHOD:  return global_request_method;
    case EBB_REQUEST_URI:     return global_request_uri;
    case EBB_FRAGMENT:        return global_fragment;
    case EBB_REQUEST_PATH:    return global_request_path;
    case EBB_QUERY_STRING:    return global_query_string;
    case EBB_HTTP_VERSION:    return global_http_version;
    case EBB_SERVER_NAME:     return global_server_name;
    case EBB_SERVER_PORT:     return global_server_port;
    case EBB_CONTENT_LENGTH:  return global_content_length;
  }
  assert(FALSE);
  return Qnil;
}

VALUE env_value(struct ebb_env_item *item)
{
  if(item->value_length > 0)
    return rb_str_new(item->value, item->value_length);
  else
    return Qnil;
}

VALUE client_env(VALUE x, VALUE client)
{
  ebb_client *_client;
  VALUE hash = rb_hash_new();
  int i;  
  Data_Get_Struct(client, ebb_client, _client);
  
  for(i=0; i < _client->env_size; i++) {
    rb_hash_aset(hash, env_field(&_client->env[i])
                     , env_value(&_client->env[i])
                     );
  }
  rb_hash_aset(hash, global_path_info, rb_hash_aref(hash, global_request_path));
  return hash;
}


VALUE client_read_input(VALUE x, VALUE client, VALUE size)
{
  ebb_client *_client;
  GString *_string;
  VALUE string;
  int _size = FIX2INT(size);
  Data_Get_Struct(client, ebb_client, _client);
  
  string = rb_str_buf_new( _size );
  int nread = ebb_client_read(_client, RSTRING_PTR(string), _size);
#if RUBY_VERSION_CODE < 190
  RSTRING(string)->len = nread;
#else
  rb_str_set_len(string, nread);
#endif
  
  if(nread < 0)
    rb_raise(rb_eRuntimeError,"There was a problem reading from input (bad tmp file?)");
  if(nread == 0)
    return Qnil;
  return string;
}


VALUE client_write(VALUE x, VALUE client, VALUE string)
{
  ebb_client *_client;
  Data_Get_Struct(client, ebb_client, _client);
  ebb_client_write(_client, RSTRING_PTR(string), RSTRING_LEN(string));
  return Qnil;
}


VALUE client_finished(VALUE x, VALUE client)
{
  ebb_client *_client;
  Data_Get_Struct(client, ebb_client, _client);
  ebb_client_finished(_client);
  return Qnil;
}


void Init_ebb_ext()
{
  VALUE mEbb = rb_define_module("Ebb");
  VALUE mFFI = rb_define_module_under(mEbb, "FFI");
  
  
  eParserError = rb_define_class_under(mEbb, "ParserError", rb_eIOError);
  
  
  /** Defines global strings in the init method. */
#define DEF_GLOBAL(N, val) global_##N = rb_obj_freeze(rb_str_new2(val)); rb_global_variable(&global_##N)
  DEF_GLOBAL(http_prefix, "HTTP_");
  DEF_GLOBAL(request_method, "REQUEST_METHOD");  
  DEF_GLOBAL(request_uri, "REQUEST_URI");
  DEF_GLOBAL(fragment, "FRAGMENT");
  DEF_GLOBAL(request_path, "REQUEST_PATH");
  DEF_GLOBAL(query_string, "QUERY_STRING");
  DEF_GLOBAL(http_version, "HTTP_VERSION");
  DEF_GLOBAL(request_body, "REQUEST_BODY");
  DEF_GLOBAL(server_name, "SERVER_NAME");
  DEF_GLOBAL(server_port, "SERVER_PORT");
  DEF_GLOBAL(path_info, "PATH_INFO");
  DEF_GLOBAL(content_length, "CONTENT_LENGTH");
  DEF_GLOBAL(http_host, "HTTP_HOST");
  
  cServer = rb_define_class_under(mEbb, "Server", rb_cObject);
  rb_define_alloc_func(cServer, server_alloc);
  // rb_define_singleton_method(mFFI, "server_initialize", server_initialize, 1);
  rb_define_singleton_method(mFFI, "server_process_connections", server_process_connections, 1);
  rb_define_singleton_method(mFFI, "server_listen_on_port", server_listen_on_port, 2);
  rb_define_singleton_method(mFFI, "server_listen_on_socket", server_listen_on_socket, 2);
  rb_define_singleton_method(mFFI, "server_unlisten", server_unlisten, 1);
  
  cClient = rb_define_class_under(mEbb, "Client", rb_cObject);
  rb_define_singleton_method(mFFI, "client_read_input", client_read_input, 2);
  rb_define_singleton_method(mFFI, "client_write", client_write, 2);
  rb_define_singleton_method(mFFI, "client_finished", client_finished, 1);
  rb_define_singleton_method(mFFI, "client_env", client_env, 1);

}
