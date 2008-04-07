"""ebb

A WSGI web server!
"""
import ebb_ffi
from signal import *

def interupt_handler(signum, frame):
  ebb_ffi.server_stop()
  
signal(SIGINT, interupt_handler)
signal(SIGTERM, interupt_handler)


def wsgi2_request_cb(app, client):  
  status_string, headers, body = app.__call__(client.env())
  
  status, status_human = status_string.split(" ")
  
  client.write_status(int(status), status_human)
  
  for field, value in headers:
    client.write_header(field, value)
    
  for part in body:
    client.write_body(part)
  
  client.release()
  return True
  
  
def start_server(app, args = {}):
  if args['port']:
    ebb_ffi.listen_on_port(int(args['port']))
  else:
    print "no port given!"
    exit(1)
  
  print "Ebb listening on port %d" % args['port']
  ebb_ffi.process_connections(app, wsgi2_request_cb)