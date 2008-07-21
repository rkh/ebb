# Ruby Binding to the Ebb Web Server
# Copyright (c) 2008 Ry Dahl. This software is released under the MIT License.
# See README file for details.
require 'stringio'
module Ebb
  VERSION = "0.3.0"
  LIBDIR = File.dirname(__FILE__)
  autoload :Runner, LIBDIR + '/ebb/runner'
  autoload :FFI, LIBDIR + '/../src/ebb_ffi'
  
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
    log.puts "Ebb PID #{Process.pid}"
    
    @running = true
    trap('INT')  { stop_server }
    
    while @running
      FFI::server_process_connections()
      while request = FFI::server_waiting_requests.shift
        if app.respond_to?(:deferred?) and app.deferred?(request.env)
          Thread.new(request) { |req| process(app, req) }
        else
          process(app, request)
        end
      end
    end
    FFI::server_unlisten()
  end
  
  def self.running?
    FFI::server_open?
  end
  
  def self.stop_server()
    @running = false
  end
  
  def self.process(app, request)
    p request.env 
    #p request.env
    status, headers, body = app.call(request.env)
    status = status.to_i

    
    # Write the status
    # request.write_status(status)
    
    # Add Content-Length to the headers.
    if !headers.has_key?('Content-Length') and
       headers.respond_to?(:[]=) and
       status != 304
    then
      # for String just use "length" method
      if body.kind_of?(String)
        headers['Content-Length'] = body.respond_to?(:bytesize) ? body.bytesize.to_s : body.length.to_s
      else
        # for non-Array object call "each" and transform to Array
        unless body.kind_of?(Array)
          parts = []
          body.each {|p| parts << p}
          body = parts
        end
        # body is Array so calculate Content-Length as sum of length each part
        headers['Content-Length'] = body.inject(0) {|s, p| s + p.length }.to_s
      end
    end
    
    # Decide if we should keep the connection alive or not
    unless headers.has_key?('Connection')
      if headers.has_key?('Content-Length') and request.should_keep_alive?
        headers['Connection'] = 'Keep-Alive'
      else
        headers['Connection'] = 'close'
      end
    end
    
    # Write the headers
    #headers.each { |field, value| request.write_header(field, value) }
    
    # Write the body
    #if body.kind_of?(String)
    #  request.write_body(body)
    #else
    #  body.each { |p| request.write_body(p) }
    #end
    
  rescue => e
    log.puts "Ebb Error! #{e.class}  #{e.message}"
    log.puts e.backtrace.join("\n")
  ensure
    request.release
  end
  
  @@log = STDOUT
  def self.log=(output)
    @@log = output
  end
  def self.log
    @@log
  end
  
  class Request
    attr_reader :fd, :body_head, :content_length
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
        @env_ffi.update(BASE_ENV)
        #env['rack.input'] = RequestBody.new(self)
        #env
      end
    end

    def should_keep_alive?
      if env['HTTP_VERSION'] == 'HTTP/1.0'
        return env['HTTP_CONNECTION'] =~ /Keep-Alive/i
      else
        return env['HTTP_CONNECTION'] !~ /close/i
      end
    end
    
    def release
      #FFI::request_release(self)
    end
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


module Rack
  module Handler
    module Ebb
      def self.run(app, options={})
        ::Ebb.start_server(app, options)
      end
    end
  end
end

