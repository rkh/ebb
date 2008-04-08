import sys
sys.path.append('/Users/ry/projects/ebb/build/lib.macosx-10.3-ppc-2.5')
import ebb
# os.path.abspath(__file__)

def simple_app(environ):
  """Simplest possible application object"""
  status = '200 OK'
  headers = [('Content-type','text/plain')]
  body = ["Hello world!\n"]
  return([status, headers, body])


def simple_wsgi1_app(environ, start_response):
  status = '200 OK'
  headers = [('Content-type','text/plain')]
  body = ["Hello world!\n"]
  
  start_response(status, headers)
  return body


ebb.start_server(simple_wsgi1_app)