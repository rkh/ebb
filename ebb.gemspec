Gem::Specification.new do |s|
  s.name = %q{ebb}
  s.version = "0.2.0"

  s.specification_version = 2 if s.respond_to? :specification_version=

  s.required_rubygems_version = Gem::Requirement.new(">= 0") if s.respond_to? :required_rubygems_version=
  s.authors = ["ry dahl"]
  s.date = %q{2008-04-19}
  s.default_executable = %q{ebb_rails}
  s.description = %q{}
  s.email = %q{ry at tiny clouds dot org}
  s.executables = ["ebb_rails"]
  s.extensions = ["src/extconf.rb"]
  s.files = ["src/ebb.c", "src/ebb.h", "src/parser.c", "src/parser.h", "libev/ev.c", "libev/ev.h", "libev/ev_epoll.c", "libev/ev_kqueue.c", "libev/ev_poll.c", "libev/ev_port.c", "libev/ev_select.c", "libev/ev_vars.h", "libev/ev_win32.c", "libev/ev_wrap.h", "README", "src/ebb_ruby.c", "src/extconf.rb", "ruby_lib/ebb", "ruby_lib/ebb/runner", "ruby_lib/ebb/runner/rails.rb", "ruby_lib/ebb/runner.rb", "ruby_lib/ebb.rb", "ruby_lib/rack", "ruby_lib/rack/adapter", "ruby_lib/rack/adapter/rails.rb", "benchmark/application.rb", "benchmark/server_test.rb", "bin/ebb_rails", "test/basic_test.rb", "test/ebb_rails_test.rb", "test/env_test.rb", "test/helper.rb"]
  s.homepage = %q{http://ebb.rubyforge.org}
  s.require_paths = ["ruby_lib"]
  s.required_ruby_version = Gem::Requirement.new(">= 1.8.4")
  s.rubyforge_project = %q{ebb}
  s.rubygems_version = %q{1.0.1}
  s.summary = %q{A Web Server}

  s.add_dependency(%q<rack>, [">= 0"])
end
