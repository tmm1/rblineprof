require 'mkmf'

if RUBY_VERSION >= "1.9"
  begin
    require "debugger/ruby_core_source"
  rescue LoadError
    require 'rubygems/dependency_installer'
    installer = Gem::DependencyInstaller.new
    installer.install 'debugger-ruby_core_source'
    require "debugger/ruby_core_source"
  end

  hdrs = proc {
    have_header("vm_core.h") and
    have_header("iseq.h")
  }

  unless Debugger::RubyCoreSource::create_makefile_with_core(hdrs, "rblineprof")
    STDERR.puts "\nDebugger::RubyCoreSource::create_makefile failed"
    exit(1)
  end
else
  create_makefile 'rblineprof'
end
