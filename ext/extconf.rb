require 'mkmf'
require 'fileutils'

if RUBY_VERSION >= "1.9"
  begin
    require "debugger/ruby_core_source"
  rescue LoadError
    require 'rubygems/user_interaction' # for 1.9.1
    require 'rubygems/dependency_installer'
    installer = Gem::DependencyInstaller.new
    installer.install 'debugger-ruby_core_source'

    Gem.refresh
    Gem.activate('debugger-ruby_core_source') # for 1.9.1

    require "debugger/ruby_core_source"
  end
end

def add_define(name)
  $defs.push("-D#{name}")
end

if RUBY_VERSION >= "1.9"
  add_define 'RUBY19'

  hdrs = proc {
    have_header("vm_core.h") and
    have_header("iseq.h")
  }

  unless Debugger::RubyCoreSource::create_makefile_with_core(hdrs, "rblineprof")
    STDERR.puts "\nDebugger::RubyCoreSource::create_makefile failed"
    exit(1)
  end
else
  add_define 'RUBY18'
  create_makefile 'rblineprof'
end

