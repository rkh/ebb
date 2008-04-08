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

ebb.start_server(simple_app, {'unix_socket': '/tmp/ebb.sock'})