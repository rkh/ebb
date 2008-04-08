"""ebb

A WSGI web server
"""
import ebb_ffi
import re
from headers import *
from signal import *

# TODO fix me
def interupt_handler(signum, frame):
  ebb_ffi.server_stop()
  
signal(SIGINT, interupt_handler)
signal(SIGTERM, interupt_handler)

def body_length(body):
  if len(body) == 1:
    return len(body[0])
  else:
    # TODO
    raise Exception, "Not implemented"
    
def should_keep_alive(env):
  if env['HTTP_VERSION'] == 'HTTP/1.0':
    if env.has_key('HTTP_CONNECTION') and env['HTTP_CONNECTION'].upper() == 'KEEP-ALIVE':
      return True
  else:
    if env.has_key('HTTP_CONNECTION'):
      if env['HTTP_CONNECTION'].upper() != 'CLOSE': 
        return True
    else:
      return True
  return False

class StartResponse:
  def __init__(self, client):
    self.client = client
  
  def __call__(self, status_string, response_headers, exc_info=None):
    # status_string should be something like "200 OK" or "404 Not Found"
    # need to split this into an integer and human readable string for ebb
    match = re.search('(\d+) (.*)', status_string)
    status = int(match.group(1))
    reason_phrase = match.group(2)
    
    # write the status
    self.client.write_status(status, reason_phrase)
    
    headers = Headers(response_headers)
    
    # Decide if we should keep the connection alive or not
    if not headers.has_key('Connection'):
      if headers.has_key('Content-Length') and should_keep_alive(self.client.env()):
        headers['Connection'] = 'Keep-Alive'
      else:
        headers['Connection'] = 'close'

    # write the headers
    for field, value in headers.items():
      self.client.write_header(field, value)
    
    # This should return a write() object. but it's stupid and I don't want
    # to support it
    return None

def request_wsgi1(app, client):
  body = app.__call__(client.env(), StartResponse(client))
  
  # write the body
  for part in body:
    client.write_body(part)
  
  client.release()
  
# For WSGI 2.0
def request_wsgi2(app, client):  
  status_string, header_list, body = app.__call__(client.env())
  
  # status_string should be something like "200 OK" or "404 Not Found"
  # need to split this into an integer and human readable string for ebb
  match = re.search('(\d+) (.*)', status_string)
  status = int(match.group(1))
  reason_phrase = match.group(2)
  
  # write the status
  client.write_status(status, reason_phrase)
  
  headers = Headers(header_list)
  if not headers.has_key('Content-Length'):
    headers['Content-Length'] = str(body_length(body))
  
  # Decide if we should keep the connection alive or not
  if not headers.has_key('Connection'):
    if headers.has_key('Content-Length') and should_keep_alive(client.env()):
      headers['Connection'] = 'Keep-Alive'
    else:
      headers['Connection'] = 'close'
  
  # write the headers
  for field, value in headers.items():
    client.write_header(field, value)
  
  # write the body
  for part in body:
    client.write_body(part)
  
  client.release()
  
  
def start_server(app, args = {}):
  if args.has_key('unix_socket'):
    socketfile = args['unix_socket']
    ebb_ffi.listen_on_unix_socket(socketfile)
    print "Ebb is listening on unix socket %s" % socketfile
  elif args.has_key('fileno'):
    fileno = int(args['fileno'])
    ebb_ffi.listen_on_fd(fileno)
    print "Ebb is listening on fd %d" % fileno
  else:
    if args.has_key('port'):
      port = int(args['port']) 
    else:
      port = 4001
    ebb_ffi.listen_on_port(port)
    print "Ebb is listening at http://0.0.0.0:%d/" % port
  
  if args.has_key('wsgi_version') and args['wsgi_version'] >= (2,0):
    cb = request_wsgi2
  else:
    cb = request_wsgi1
  
  ebb_ffi.process_connections(app, cb)