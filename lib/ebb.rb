# Ruby Binding to the Ebb Web Server
# Copyright (c) 2008 Ry Dahl. This software is released under the MIT License.
# See README file for details.
require 'stringio'
module Ebb
  VERSION = "0.3.0"
  LIBDIR = File.dirname(__FILE__)
  autoload :Runner, LIBDIR + '/ebb/runner'
  autoload :FFI, LIBDIR + '/../ext/ebb_ffi'
  
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
        log.puts "ebb compiled without ssl support. get gnutls" 
      else
        cert_file = options[:ssl_cert]
        key_file = options[:ssl_key]
        if FileTest.readable?(cert_file) and FileTest.readable?(cert_file)
          FFI::server_set_secure(cert_file, key_file);
        else 
          log.puts "error opening certificate or key file"
        end
      end
    end

    log.puts "Ebb PID #{Process.pid}"
    
    @running = true
    Connection.reset_responses
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
    res = req.response
    req.connection.responses << res

    # p req.env
    status = headers = body = nil
    catch(:async) do 
      status, headers, body = app.call(req.env)
    end

    # James Tucker's async response scheme
    # check out
    # http://github.com/raggi/thin/tree/async_for_rack/example/async_app.ru
    res.call(status, headers, body) if status != 0 
    # if status == 0 then the application promises to call
    # env['async.callback'].call(status, headers, body) 
    # later on...

  end

  class Connection
    def self.reset_responses
      @@responses = {} # used for memory management :|
    end

    def self.responses
      @@responses
    end

    def responses
      @@responses[self]
    end

    # called from c
    def append_request(req)
      @requests.push req
    end

    def on_open
      @requests = []
      @@responses[self] = []
    end

    def on_close
      # garbage collection ! 
      @requests.each { |req| req.connection = nil }
      responses.each { |res| res.connection = nil }
      @@responses.delete(self)
    end

    def writing?
      ! @being_written.nil?
    end

    def write
      return if writing?
      return unless res = responses.first
      return if res.output.empty?
      # NOTE: connection_write does not buffer!
      chunk = res.output.shift
      @being_written = chunk # need to store this so ruby doesn't gc it
      FFI::connection_write(self, chunk) 
    end

    # called after FFI::connection_write if complete
    def on_writable
      @being_written = nil
      return unless res = responses.first
      if res.finished?
        responses.shift
        if res.last 
          FFI::connection_schedule_close(self) 
          return
        end
      end 
      write
    end
  end
    
  class Response
    attr_reader :output
    attr_accessor :last, :connection
    def initialize(connection, last)
      @connection = connection
      @last = last
      @output = []
      @finished = false
      @chunked = false 
    end

    def call(status, headers, body)
      @head = "HTTP/1.1 #{status} #{HTTP_STATUS_CODES[status.to_i]}\r\n"
      headers.each { |field, value| @head << "#{field}: #{value}\r\n" }
      @head << "\r\n"

      # XXX i would prefer to do
      # @chunked = true unless body.respond_to?(:length)
      @chunked = true if headers["Transfer-Encoding"] == "chunked"
      # I also don't like this
      @last = true if headers["Connection"] == "close"

      # Note: not setting Content-Length. do it yourself.
      
      body.each do |chunk| 
        if @head.nil?
          write(chunk) 
        else
          write(@head + chunk) 
          @head = nil
        end
        @connection.write
      end

      body.on_error { close } if body.respond_to?(:on_error)

      if body.respond_to?(:on_eof)
        body.on_eof { finish }
      else
        finish
      end

      # deferred requests SHOULD NOT respond to close
      body.close if body.respond_to?(:close)
    end

    def finished?
      @output.empty? and @finished
    end

    def finish
      @finished = true 
      if @chunked
        write("") 
        @connection.write
      end
    end
    
    def write(chunk)
      encoded = @chunked ? "#{chunk.length.to_s(16)}\r\n#{chunk}\r\n" : chunk
      @output << encoded
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
    
    def env
      @env ||= begin
        env = BASE_ENV.merge(@env_ffi)
        env['rack.input'] = self 
        env['CONTENT_LENGTH'] = env['HTTP_CONTENT_LENGTH']
        env['async.callback'] = response
        env
      end
    end

    def keep_alive?
      FFI::request_should_keep_alive?(self)
    end

    def response
      @response ||= begin
        last = !keep_alive? # this is the last response if the request isnt keep-alive
        Response.new(@connection, last)
      end
    end

    def read(want = 1024)
      FFI::request_read(self, want)
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

