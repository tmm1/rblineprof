#include <stdio.h>
#include <ruby.h>
#include <node.h>
#include <intern.h>

static char *source_filename;
uint64_t start_time;
long last_line;
#define MAX_LINES 4096
uint64_t per_line[MAX_LINES];

static uint64_t
timeofday_usec()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec*1e6 +
         (uint64_t)tv.tv_usec;
}

static void
profiler_hook(rb_event_t event, NODE *node, VALUE self, ID mid, VALUE klass)
{
  char *file = node->nd_file;
  long line  = nd_line(node);

  if (source_filename == file) {
    uint64_t now = timeofday_usec();

    if (start_time) {
      if (last_line < MAX_LINES)
        per_line[last_line] += now - start_time;
    }

    start_time = now;
    last_line = line;
  }
}

VALUE
lineprof(VALUE self, VALUE filename)
{
  VALUE ret = rb_hash_new();
  int i;

  start_time = 0;
  source_filename = rb_source_filename(StringValuePtr(filename));
  memset(per_line, 0, sizeof(per_line));

  rb_add_event_hook(profiler_hook, RUBY_EVENT_LINE);
  rb_yield(Qnil);
  rb_remove_event_hook(profiler_hook);

  for (i=0; i<MAX_LINES; i++)
    if (per_line[i])
      rb_hash_aset(ret, INT2FIX(i), ULL2NUM(per_line[i]));

  return ret;
}

void
Init_rblineprof()
{
  rb_define_global_function("lineprof", lineprof, 1);
}
