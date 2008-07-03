# Ruby Binding to the Ebb Web Server
# Copyright (c) 2008 Ry Dahl. This software is released under the MIT License.
# See README file for details.
require 'stringio'
module Ebb
  VERSION = "0.2.0"
  LIBDIR = File.dirname(__FILE__)
  autoload :Runner, LIBDIR + '/ebb/runner'
  autoload :FFI, LIBDIR + '/../src/ebb_ext'
  
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
      port = (options[:port] || 4001).to_i
      FFI::server_listen_on_port(port)
      log.puts "Ebb is listening at http://0.0.0.0:#{port}/"
    end
    log.puts "Ebb PID #{Process.pid}"
    
    @running = true
    trap('INT')  { stop_server }
    
    while @running
      FFI::server_process_connections()
      while client = FFI::server_waiting_clients.shift
        if app.respond_to?(:deferred?) and app.deferred?(client.env)
          Thread.new(client) { |c| process(app, c) }
        else
          process(app, client)
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
  
  def self.process(app, client)
    #p client.env
    status, headers, body = app.call(client.env)
    status = status.to_i
    
    # Write the status
    client.write_status(status)
    
    # Add Content-Length to the headers.
    if headers['Content-Length'].nil? and
       headers.respond_to?(:[]=) and 
       body.respond_to?(:length) and 
       status != 304
    then
      headers['Content-Length'] = body.length.to_s
    end
    
    # Decide if we should keep the connection alive or not
    if headers['Connection'].nil?
      if headers['Content-Length'] and client.should_keep_alive?
        headers['Connection'] = 'Keep-Alive'
      else
        headers['Connection'] = 'close'
      end
    end
    
    # Write the headers
    headers.each { |field, value| client.write_header(field, value) }
    
    # Write the body
    if body.kind_of?(String)
      client.write_body(body)
    else
      body.each { |p| client.write_body(p) }
    end
    
  rescue => e
    log.puts "Ebb Error! #{e.class}  #{e.message}"
    log.puts e.backtrace.join("\n")
  ensure
    client.release
  end
  
  @@log = STDOUT
  def self.log=(output)
    @@log = output
  end
  def self.log
    @@log
  end
  
  class Client
    attr_reader :fd, :body_head, :content_length
    BASE_ENV = {
      'SERVER_NAME' => '0.0.0.0',
      'SCRIPT_NAME' => '',
      'SERVER_SOFTWARE' => "Ebb-Ruby #{Ebb::VERSION}",
      'SERVER_PROTOCOL' => 'HTTP/1.1',
      'rack.version' => [0, 1],
      'rack.errors' => STDERR,
      'rack.url_scheme' => 'http',
      'rack.multiprocess' => false,
      'rack.run_once' => false
    }
    
    def env
      @env ||= begin
        env = FFI::client_env(self).update(BASE_ENV)
        env['rack.input'] = RequestBody.new(self)
        env
      end
    end
    
    def write_status(status)
      FFI::client_write_status(self, status, HTTP_STATUS_CODES[status])
    end
    
    def write_body(data)
      FFI::client_write_body(self, data)
    end
    
    def write_header(field, value)
      value.send(value.is_a?(String) ? :each_line : :each) do |v| 
        FFI::client_write_header(self, field, v.chomp)
      end
    end
    
    def release
      FFI::client_release(self)
    end
    
    def set_keep_alive
      FFI::client_set_keep_alive(self)
    end
    
    def should_keep_alive?
      if env['HTTP_VERSION'] == 'HTTP/1.0' 
        return true if env['HTTP_CONNECTION'] =~ /Keep-Alive/i
      else
        return true unless env['HTTP_CONNECTION'] =~ /close/i
      end
      false
    end
  end
  
  class RequestBody
    def initialize(client)
      @content_length = client.content_length
      if client.body_head
        @body_head = StringIO.new(client.body_head)
        if @body_head.length < @content_length
          @socket = IO.new(client.fd)
        end
      end
      @total_read = 0
    end
    
    def read(len = nil)
      to_read =  len.nil? ? @content_length - @total_read : min(len, @content_length - @total_read)
      return nil if to_read == 0 or @body_head.nil?
      unless out = @body_head.read(to_read)
        return nil if @socket.nil?
        out = @socket.read(to_read)
      end
      @total_read += out.length
      out
    end
    
    def gets
      raise NotImplemented
    end
    
    def each(&block)
      raise NotImplemented
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

# cause i don't want to create an array
def min(a,b)
  a > b ? b : a
end
