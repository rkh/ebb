/* A python binding to the ebb web server
 * Copyright (c) 2008 Ry Dahl. This software is released under the MIT 
 * License. See README file for details.
 */
#include <Python.h>
#include "ebb.h"
#include <ev.h>
#include <assert.h>

#define PyDict_SetStringString(dict, str1, str2) PyDict_SetItemString(dict, str1, PyString_FromString(str2))
#define ASCII_UPPER(ch) ('a' <= ch && ch <= 'z' ? ch - 'a' + 'A' : ch)

/* Why is there a global ebb_server variable instead of a wrapping it in a 
 * class? Because you would for no conceivable reason want to run more than
 * one ebb_server per python VM instance.
 */
static ebb_server server;
struct ev_loop *loop;
struct ev_timer check_stop_watcher;

static PyThreadState *libev_thread_state;

static PyObject *py_request_cb;
static PyObject *application;
static int server_running;
static int server_error;

static PyObject *base_env;
static PyObject *global_http_prefix;
static PyObject *global_request_method;
static PyObject *global_request_uri;
static PyObject *global_fragment;
static PyObject *global_request_path;
static PyObject *global_query_string;
static PyObject *global_http_version;
static PyObject *global_request_body;
static PyObject *global_server_name;
static PyObject *global_server_port;
static PyObject *global_path_info;
static PyObject *global_content_length;
static PyObject *global_http_host;

/* A callable type called Client. __call__(status, response_headers)
 * is the second argument to an appplcation
 * Client.env is the first argument.
 */
typedef struct {
  PyObject_HEAD
  ebb_client *client;
  PyObject *env;
} py_client;

static void py_client_dealloc(PyObject *obj)
{
  py_client *self = (py_client*) obj;
  ebb_client_release(self->client);
  Py_DECREF(self->env);
  obj->ob_type->tp_free(obj);
}

static PyObject *
write_status(py_client *self, PyObject *args)
{
  char *status_human;
  int status;
  
  if(!PyArg_ParseTuple(args, "is", &status, &status_human))
    return NULL;
  ebb_client_write_status(self->client, status, status_human);
  Py_RETURN_NONE;
}

static PyObject *
write_header(py_client *self, PyObject *args)
{
  char *field, *value;
  
  if(!PyArg_ParseTuple(args, "ss", &field, &value))
    return NULL;
  ebb_client_write_header(self->client, field, value);
  Py_RETURN_NONE;
}

static PyObject *
write_body(py_client *self, PyObject *args)
{
  char *body;
  int body_length;
  
  if(!PyArg_ParseTuple(args, "s#", &body, &body_length))
    return NULL;
  ebb_client_write_body(self->client, body, body_length);
  Py_RETURN_NONE;
}

static PyObject *
client_release(py_client *self)
{
  ebb_client_release(self->client);
  Py_RETURN_NONE;
}

static PyObject *
client_env(py_client *self)
{
  Py_INCREF(self->env);
  return self->env;
}


static PyMethodDef client_methods[] = 
  { {"write_status", (PyCFunction)write_status, METH_VARARGS, NULL }
  , {"write_header", (PyCFunction)write_header, METH_VARARGS, NULL }
  , {"write_body", (PyCFunction)write_body, METH_VARARGS, NULL }
  , {"release", (PyCFunction)client_release, METH_NOARGS, NULL }
  , {"env", (PyCFunction)client_env, METH_NOARGS, NULL }
  , {NULL, NULL, 0, NULL}
  };

static PyTypeObject py_client_t = 
  { ob_refcnt: 1
  , tp_name: "ebb_ffi.Client"
  , tp_doc: "a wrapper around ebb_client"
  , tp_basicsize: sizeof(py_client)
  , tp_flags: Py_TPFLAGS_DEFAULT
  , tp_methods: client_methods
  , tp_dealloc: py_client_dealloc
  };

static PyObject* env_field(struct ebb_env_item *item)
{
  PyObject* f = NULL;
  int i;
  
  if(item->field) {
    f = PyString_FromStringAndSize(NULL, PyString_GET_SIZE(global_http_prefix) + item->field_length);
    memcpy( PyString_AS_STRING(f)
          , PyString_AS_STRING(global_http_prefix)
          , PyString_GET_SIZE(global_http_prefix)
          );
    for(i = 0; i < item->field_length; i++) {
      char *ch = PyString_AS_STRING(f) + PyString_GET_SIZE(global_http_prefix) + i;
      *ch = item->field[i] == '-' ? '_' : ASCII_UPPER(item->field[i]);
    }
  } else {
    switch(item->type) {
      case MONGREL_REQUEST_METHOD:  f = global_request_method; break;
      case MONGREL_REQUEST_URI:     f = global_request_uri; break;
      case MONGREL_FRAGMENT:        f = global_fragment; break;
      case MONGREL_REQUEST_PATH:    f = global_request_path; break;
      case MONGREL_QUERY_STRING:    f = global_query_string; break;
      case MONGREL_HTTP_VERSION:    f = global_http_version; break;
      case MONGREL_CONTENT_LENGTH:  f = global_content_length; break;
      default: assert(FALSE);
    }
  }
  Py_INCREF(f);
  return f;
}


static PyObject* env_value(struct ebb_env_item *item)
{
  if(item->value_length > 0)
    return PyString_FromStringAndSize(item->value, item->value_length);
  else
    return Py_None; /* XXX need to increase ref count? :*/
}


static PyObject* py_client_env(ebb_client *client)
{
  PyObject *env = PyDict_Copy(base_env);
  int i;
  
  for(i=0; i < client->env_size; i++) {
    PyDict_SetItem(env, env_field(&client->env[i])
                      , env_value(&client->env[i])
                      );
  }
  // PyDict_SetStringString(hash, global_path_info, rb_hash_aref(hash, global_request_path));
  
  return env;
}

static py_client* py_client_new(ebb_client *client)
{
  py_client *self = PyObject_New(py_client, &py_client_t);
  if(self == NULL) return NULL;
  self->client = client;
  self->env = py_client_env(client);
  
  return self;
}

void request_cb(ebb_client *client, void *_)
{
  PyEval_RestoreThread(libev_thread_state); /* acquire GIL for this thread */
    
  py_client *pclient = py_client_new(client);
  assert(pclient != NULL);
  assert(application != NULL);
  
  PyObject *arglist = Py_BuildValue("(OO)", application, pclient);
  assert(arglist != NULL);
  PyObject *rv = PyEval_CallObject(py_request_cb, arglist);
  if(rv == NULL) {
    server_error = TRUE;
    ebb_client_close(pclient->client);
    return;
  }
  
  Py_DECREF(arglist);
  Py_DECREF(pclient);
  
  libev_thread_state = PyEval_SaveThread(); /* allow other python threads to run again */
}

static void
check_stop (struct ev_loop *loop, struct ev_timer *w, int revents)
{
  if(PyErr_CheckSignals() < 0 || server_running == FALSE || server_error == TRUE) {
    ev_timer_stop(loop, w);
    ev_unloop(loop, EVUNLOOP_ALL);
  }
}



static PyObject *process_connections(PyObject *_, PyObject *args)
{
  assert(py_request_cb == NULL);
  if(!PyArg_ParseTuple(args, "OO", &application, &py_request_cb))
    return NULL;
  if(!PyCallable_Check(application)) {
    PyErr_SetString(PyExc_TypeError, "parameter must be callable");
    return NULL;
  }
  if(!PyCallable_Check(py_request_cb)) {
    PyErr_SetString(PyExc_TypeError, "parameter must be callable");
    return NULL;
  }
  Py_XINCREF(application);
  Py_XINCREF(py_request_cb);
  
  server_running = TRUE;
  server_error = FALSE;
  
  ev_timer_init (&check_stop_watcher, check_stop, 0.5, 0.5);
  ev_timer_start (loop, &check_stop_watcher);
  
  libev_thread_state = PyEval_SaveThread(); /* allow other threads to run */
  ev_loop(loop, 0);
  PyEval_RestoreThread(libev_thread_state); /* reacquire GIL */
  
  /* TODO: exit properly */
  if(server_error) return NULL;
  //ebb_server_unlisten(&server);
  
  Py_XDECREF(py_request_cb);
  Py_XDECREF(application);
  py_request_cb = NULL;
  application = NULL;
  Py_RETURN_NONE;
}

static PyObject *server_stop(PyObject *_)
{
  server_running = FALSE;
  Py_RETURN_NONE;
}

static PyObject *listen_on_port(PyObject *_, PyObject *args)
{
  int port;
  if(!PyArg_ParseTuple(args, "i", &port)) return NULL;  
  ebb_server_listen_on_port(&server, port);
  Py_RETURN_NONE;
}

static PyObject *listen_on_fd(PyObject *_, PyObject *args)
{
  int fd;
  if(!PyArg_ParseTuple(args, "i", &fd)) return NULL;  
  ebb_server_listen_on_fd(&server, fd);
  Py_RETURN_NONE;
}

static PyObject *listen_on_unix_socket(PyObject *_, PyObject *args)
{
  char *socketfile;
  if(!PyArg_ParseTuple(args, "s", &socketfile)) return NULL;  
  ebb_server_listen_on_unix_socket(&server, socketfile);
  Py_RETURN_NONE;
}

static PyMethodDef ebb_module_methods[] = 
  { {"listen_on_port", (PyCFunction)listen_on_port, METH_VARARGS, NULL}
  , {"listen_on_fd", (PyCFunction)listen_on_fd, METH_VARARGS, NULL}
  , {"listen_on_unix_socket", (PyCFunction)listen_on_unix_socket, METH_VARARGS, NULL}
  , {"process_connections", (PyCFunction)process_connections, METH_VARARGS, NULL}
  , {"server_stop", (PyCFunction)server_stop, METH_NOARGS, NULL}
  , {NULL, NULL, 0, NULL}
  };

PyMODINIT_FUNC initebb_ffi(void) 
{
  PyObject *m = Py_InitModule("ebb_ffi", ebb_module_methods);
  
  base_env = PyDict_New();
  PyDict_SetStringString(base_env, "SCRIPT_NAME", "");
  PyDict_SetStringString(base_env, "SERVER_SOFTWARE", EBB_VERSION);
  PyDict_SetStringString(base_env, "SERVER_NAME", "0.0.0.0");
  PyDict_SetStringString(base_env, "SERVER_PROTOCOL", "HTTP/1.1");
  PyDict_SetStringString(base_env, "wsgi.url_scheme", "http");
  PyDict_SetItemString(base_env, "wsgi.multithread", Py_False);
  PyDict_SetItemString(base_env, "wsgi.multiprocess", Py_False);
  PyDict_SetItemString(base_env, "wsgi.run_once", Py_False);
  //PyDict_SetItemString(base_env, "wsgi.version", (0,1));
  //PyDict_SetItemString(base_env, "wsgi.errors", STDERR);
  
  
  /* StartResponse */
  py_client_t.tp_new = PyType_GenericNew;
  if (PyType_Ready(&py_client_t) < 0) return;
  Py_INCREF(&py_client_t);
  PyModule_AddObject(m, "Client", (PyObject *)&py_client_t);
  
#define DEF_GLOBAL(N, val) global_##N = PyString_FromString(val)
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
  
  loop = ev_default_loop(0);
  ebb_server_init(&server, loop, request_cb, NULL);
}