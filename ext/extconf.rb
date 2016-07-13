require 'mkmf'

have_func('rb_os_allocated_objects')

if RUBY_VERSION >= "2.1"
  have_func('rb_gc_stat')
  have_func('rb_profile_frames')
  have_func('rb_tracepoint_new')
  create_makefile 'rblineprof'
elsif RUBY_VERSION >= "1.9"
  require "debugger/ruby_core_source"

  hdrs = proc {
    have_type("rb_iseq_location_t", "vm_core.h")

    have_header("vm_core.h") and
    (have_header("iseq.h") or have_header("iseq.h", ["vm_core.h"]))
  }

  unless Debugger::RubyCoreSource::create_makefile_with_core(hdrs, "rblineprof")
    STDERR.puts "\nDebugger::RubyCoreSource::create_makefile failed"
    exit(1)
  end
else
  create_makefile 'rblineprof'
end
