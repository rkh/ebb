require 'rake'
require 'rake/testtask'
require 'rake/gempackagetask'
require 'rake/clean'

def dir(path)
  File.expand_path File.join(File.dirname(__FILE__), path)
end

require dir('lib/ebb')

DISTFILES = FileList.new('lib/**/*.rb', 'src/**/*.{rb,rl,c,h}', 'bin/*', 'README', 'Rakefile')

CLEAN.add ["**/*.{o,bundle,so,obj,pdb,lib,def,exp}", "benchmark/*.dump", 'site/index.html', ]

CLOBBER.add ['src/Makefile', 'src/ebb_request_parser.c', 'src/mkmf.log']

Rake::TestTask.new do |t|
  t.test_files = FileList.new("test/*.rb")
  t.verbose = true
end

task(:default => [:compile])

task(:compile => ['src/Makefile','src/ebb_request_parser.c'] + DISTFILES) do
  sh "cd #{dir('src')} && make"
end

file('src/Makefile' => 'src/extconf.rb') do
    sh "cd #{dir('src')} && ruby extconf.rb"
end

file('src/ebb_request_parser.c' => 'src/ebb_request_parser.rl') do
  sh 'ragel -s -G2 src/ebb_request_parser.rl'
end

task(:wc) { sh "wc -l ruby_lib/*.rb src/ebb*.{c,h}" }

task(:test => DISTFILES)
Rake::TestTask.new do |t|
  t.test_files = 'test/basic_test.rb'
  t.verbose = true
end

task(:site_upload => :site) do
  sh 'scp -r site/* rydahl@rubyforge.org:/var/www/gforge-projects/ebb/'
end
task(:site => 'site/index.html')
file('site/index.html' => %w{README site/style.css}) do
  require 'BlueCloth'
  doc = BlueCloth.new(File.read(dir('README')))
  template = <<-HEREDOC
<html>
  <head>
    <meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
    <title>Ebb</title>
    <link rel="alternate" href="http://max.kanat.us/tag-syndicate/?user=four&tag=ebb" title="RSS Feed" type="application/rss+xml" />
    <link type="text/css" rel="stylesheet" href="style.css" media="screen"/>
  </head>
  <body>  
    <div id="content">CONTENT</div>
  </body>
</html>
HEREDOC
  
  File.open(dir('site/index.html'), "w+") do |f|
    f.write template.sub('CONTENT', doc.to_html)
  end
end

spec = Gem::Specification.new do |s|
  s.platform = Gem::Platform::RUBY
  s.summary = "A Web Server"
  s.description = ''
  s.name = 'ebb'
  s.author = 'ry dahl'
  s.email = 'ry at tiny clouds dot org'
  s.homepage = 'http://ebb.rubyforge.org'
  s.version = Ebb::VERSION
  s.rubyforge_project = 'ebb'
  
  s.add_dependency('rack')
  s.required_ruby_version = '>= 1.8.4'
  
  s.require_path = 'ruby_lib'
  s.extensions = 'src/extconf.rb'
  s.bindir = 'bin'
  s.executables = %w(ebb_rails)
  
  s.files = DISTFILES
end

Rake::GemPackageTask.new(spec) do |pkg|
  pkg.need_zip = true
end
