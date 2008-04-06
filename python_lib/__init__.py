"""ebb

A WSGI web server!
"""
import ebb_ffi

def start_server(app, args = {}):
  if args['port']:
    ebb_ffi.listen_on_port(int(args['port']))
  else:
    print "bad!"
  
  ebb_ffi.process_connections(app)