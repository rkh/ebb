# Ruby Binding to the Ebb Web Server
# Copyright (c) 2008 Ry Dahl. This software is released under the MIT License.
# See README file for details.
require 'stringio'
module Ebb
  VERSION = "0.3.0"
  LIBDIR = File.dirname(__FILE__)
  autoload :Runner, LIBDIR + '/ebb/runner'
  autoload :FFI, LIBDIR + '/../src/ebb_ffi'
  
  def self.running?
    FFI::server_open?
  end
  
  def self.stop_server()
    @running = false
  end
  
  def self.start_server(app, options={})
    if options.has_key?(:fileno)
      fd = options[:fileno].to_i
      FFI::server_listen_on_fd(fd)
      log.puts "Ebb is listening on file descriptor #{fd}"
    elsif options.has_key?(:unix_socket)
      socketfile = options[:unix_socket]
      FFI::server_listen_on_unix_socket(socketfile)
      log.puts "Ebb is listening on unix socket #{socketfile}"
    else
      port = (options[:Port] || options[:port] || 4001).to_i
      FFI::server_listen_on_port(port)
      log.puts "Ebb is listening at http://0.0.0.0:#{port}/"
    end

    if options.has_key?(:ssl_cert) and options.has_key?(:ssl_key)
      unless FFI.respond_to?(:server_set_secure)
        puts "ebb compiled without ssl support. get gnutls" 
      else
        cert_file = options[:ssl_cert]
        key_file = options[:ssl_key]
        if FileTest.readable?(cert_file) and FileTest.readable?(cert_file)
          FFI::server_set_secure(cert_file, key_file);
        else 
          puts "error opening certificate or key file"
        end
      end
    end

    log.puts "Ebb PID #{Process.pid}"
    
    @running = true
    Connection.reset_write_queues
    trap('INT')  { stop_server }

    while @running
      FFI::server_process_connections()
      while request = FFI::server_waiting_requests.shift
        if app.respond_to?(:deferred?) and !app.deferred?(request.env)
          process(app, request)
        else
          Thread.new(request) { |req| process(app, req) }
        end
      end
    end
    FFI::server_unlisten()
  end
  
  def self.process(app, req)
    # p req.env
    status, headers, body = app.call(req.env)
    res = Response.new(status, headers, body)
    res.last = !req.keep_alive?

    # I use a non-rack body.shift method.
    # because its not very useful in an evented manor
    # i hope chris will change this soon
    unless body.respond_to?(:shift)
      if body.kind_of?(String)
        body = [body]
      else
        b = []
        body.each { |chunk| b << chunk }
        body = c
      end
    end

    # TODO chunk encode the response if have chunked encoding

    response_queue = Connection.write_queues[req.connection]
    response_queue << res  
    req.connection.start_writing if response_queue.length == 1 
  end

  class Connection
    def self.reset_write_queues
      @@write_queues = {}
    end

    def self.write_queues
      @@write_queues
    end

    def start_writing
      res = queue.first
      FFI::connection_write(self, res.chunk)
    end

    def queue
      @@write_queues[self]
    end

    def append_request(req)
      @requests.push req
    end

    def on_open
      @requests = []
      @@write_queues[self] = []
    end

    def on_close
      # garbage collection ! 
      @requests.each { |req| req.connection = nil }
      @@write_queues.delete(self)
    end

    def on_writable
      if queue.empty?
        @@write_queues.delete(self)
      else
        res = queue.first
        if chunk = res.shift
          FFI::connection_write(self, chunk)
        else
          if res.last
            FFI::connection_schedule_close(self) 
            @@write_queues.delete(self)
          else
            queue.shift # write nothing. we'll get'em next time
          end
        end
      end
    end
  end
    
  class Response
    attr_reader :chunk
    attr_accessor :last
    def initialize(status, headers, body)
      @body = body
      @chunk = "HTTP/1.1 #{status} #{HTTP_STATUS_CODES[status.to_i]}\r\n"
      headers.each { |field, value| @chunk << "#{field}: #{value}\r\n" }
      @chunk << "\r\n#{@body.shift}" 
      @last = false
    end

    # if returns nil, there is nothing else to write
    # otherwise returns then next chunk needed to write.
    # on writable call connection.write(response.shift) 
    def shift
      @chunk = @body.shift
    end

    HTTP_STATUS_CODES = {
      100  => 'Continue', 
      101  => 'Switching Protocols', 
      200  => 'OK', 
      201  => 'Created', 
      202  => 'Accepted', 
      203  => 'Non-Authoritative Information', 
      204  => 'No Content', 
      205  => 'Reset Content', 
      206  => 'Partial Content', 
      300  => 'Multiple Choices', 
      301  => 'Moved Permanently', 
      302  => 'Moved Temporarily', 
      303  => 'See Other', 
      304  => 'Not Modified', 
      305  => 'Use Proxy', 
      400  => 'Bad Request', 
      401  => 'Unauthorized', 
      402  => 'Payment Required', 
      403  => 'Forbidden', 
      404  => 'Not Found', 
      405  => 'Method Not Allowed', 
      406  => 'Not Acceptable', 
      407  => 'Proxy Authentication Required', 
      408  => 'Request Time-out', 
      409  => 'Conflict', 
      410  => 'Gone', 
      411  => 'Length Required', 
      412  => 'Precondition Failed', 
      413  => 'Request Entity Too Large', 
      414  => 'Request-URI Too Large', 
      415  => 'Unsupported Media Type', 
      500  => 'Internal Server Error', 
      501  => 'Not Implemented', 
      502  => 'Bad Gateway', 
      503  => 'Service Unavailable', 
      504  => 'Gateway Time-out', 
      505  => 'HTTP Version not supported'
    }.freeze
  end
  
  @@log = STDOUT

  def self.log=(output)
    @@log = output
  end

  def self.log
    @@log
  end
  
  class Request
    attr_accessor :connection
    BASE_ENV = {
      'SERVER_NAME' => '0.0.0.0',
      'SCRIPT_NAME' => '',
      'QUERY_STRING' => '',
      'SERVER_SOFTWARE' => "Ebb #{Ebb::VERSION}",
      'SERVER_PROTOCOL' => 'HTTP/1.1',
      'rack.version' => [0, 1],
      'rack.errors' => STDERR,
      'rack.url_scheme' => 'http',
      'rack.multiprocess' => false,
      'rack.run_once' => false
    }

    def keep_alive?
      FFI::request_should_keep_alive?(self)
    end
    
    def env
      @env ||= begin
        env = @env_ffi.update(BASE_ENV)
        env['CONTENT_LENGTH'] = env['HTTP_CONTENT_LENGTH']
        env['rack.input'] = self 
        env
      end
    end

    def read(want = 1024)
      FFI::request_read(self, want)
    end

    def should_keep_alive?
      if env['HTTP_VERSION'] == 'HTTP/1.0'
        return env['HTTP_CONNECTION'] =~ /Keep-Alive/i
      else
        return env['HTTP_CONNECTION'] !~ /close/i
      end
    end
  end
end


module Rack
  module Handler
    module Ebb
      def self.run(app, options={})
        ::Ebb.start_server(app, options)
      end
    end
  end
end

