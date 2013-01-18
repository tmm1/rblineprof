#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <ruby.h>

#ifndef RUBY_VM
#include <ruby/node.h>
#include <node.h>

#include <node.h>
#include <env.h>
#include <intern.h>
#include <st.h>
#include <re.h>

typedef rb_event_t rb_event_flag_t;
#endif

typedef uint64_t prof_time_t;

static VALUE gc_hook;

/*
 * Struct representing an individual line of
 * Ruby source code
 */
typedef struct sourceline {
  uint64_t calls; // total number of calls
  prof_time_t total_time;
  prof_time_t max_time;
} sourceline_t;

/*
 * Struct representing a single Ruby file.
 */
typedef struct sourcefile {
  char *filename;
  long nlines;
  sourceline_t *lines;

  prof_time_t exclusive_start;
  prof_time_t exclusive_time;
} sourcefile_t;

/*
 * An individual stack frame used to track
 * calls and returns from Ruby methods
 */
typedef struct stackframe {
  // data emitted from Ruby to our profiler hook
  rb_event_flag_t event;
#ifdef RUBY_18
  NODE *node;
#else
  VALUE *node;
#endif
  VALUE self;
  ID mid;
  VALUE klass;

  char *filename;
  long line;

  prof_time_t start;
  sourcefile_t *srcfile;
} stackframe_t;

/*
 * Static properties and rbineprof configuration
 */
static struct {
  bool enabled;

  // stack
  #define MAX_STACK_DEPTH 32768
  stackframe_t stack[MAX_STACK_DEPTH];
  uint64_t stack_depth;

  // current file
  sourcefile_t *curr_srcfile;

  // single file mode, store filename and line data directly
  char *source_filename;
  sourcefile_t file;

  // regex mode, store file data in hash table
  VALUE source_regex;
  st_table *files;
}
rblineprof = {
  .enabled = false,
  .source_regex = Qfalse
};

static prof_time_t
timeofday_usec()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (prof_time_t)tv.tv_sec*1e6 +
         (prof_time_t)tv.tv_usec;
}

static inline void
stackframe_record(stackframe_t *frame, prof_time_t now)
{
  sourcefile_t *srcfile = frame->srcfile;
  long line = frame->line;

  /* allocate space for per-line data the first time */
  if (srcfile->lines == NULL) {
    srcfile->nlines = line + 100;
    srcfile->lines = ALLOC_N(sourceline_t, srcfile->nlines);
    MEMZERO(srcfile->lines, sourceline_t, srcfile->nlines);
  }

  /* grow the per-line array if necessary */
  if (line >= srcfile->nlines) {
    long prev_nlines = srcfile->nlines;
    srcfile->nlines = line + 100;

    REALLOC_N(srcfile->lines, sourceline_t, srcfile->nlines);
    MEMZERO(srcfile->lines + prev_nlines, sourceline_t, srcfile->nlines - prev_nlines);
  }

  /* record the sample */
  prof_time_t diff = now - frame->start;
  sourceline_t *srcline = &(srcfile->lines[line]);
  srcline->calls++;
  srcline->total_time += diff;
  if (diff > srcline->max_time)
    srcline->max_time = diff;
}

static inline sourcefile_t*
sourcefile_lookup(char *filename)
{
  sourcefile_t *srcfile = NULL;

  if (rblineprof.source_filename) { // single file mode
    if (rblineprof.source_filename == filename) { // compare char*, not contents
      srcfile = &rblineprof.file;
      srcfile->filename = filename;
    } else {
      return NULL;
    }

  } else { // regex mode
    st_lookup(rblineprof.files, (st_data_t)filename, (st_data_t *)&srcfile);

    if ((VALUE)srcfile == Qnil) // known negative match, skip
      return NULL;

    if (!srcfile) { // unknown file, check against regex
      if (rb_reg_search(rblineprof.source_regex, rb_str_new2(filename), 0, 0) >= 0) {
        srcfile = ALLOC_N(sourcefile_t, 1);
        MEMZERO(srcfile, sourcefile_t, 1);
        srcfile->filename = strdup(filename);
        st_insert(rblineprof.files, (st_data_t)srcfile->filename, (st_data_t)srcfile);
      } else { // no match, insert Qnil to prevent regex next time
        st_insert(rblineprof.files, (st_data_t)strdup(filename), (st_data_t)Qnil);
        return NULL;
      }
    }
  }

  return srcfile;
}

static void
#ifndef RUBY_18
profiler_hook(rb_event_flag_t event, VALUE *node, VALUE self, ID mid, VALUE klass)
#else
profiler_hook(rb_event_flag_t event, NODE *node, VALUE self, ID mid, VALUE klass)
#endif
{
  char *file;
  long line;
  stackframe_t *frame = NULL;
  sourcefile_t *srcfile, *curr_srcfile;
  prof_time_t now = timeofday_usec();

  /* file profiler: when invoking a method in a new file, account elapsed
   * time to the current file and start a new timer.
   */
  if (!node) return;

#ifndef RUBY_18
  file = "wat";
#else
  file = node->nd_reserved;
#endif

  line = nd_line(node);
  if (!file) return;
  if (line <= 0) return;

  srcfile = sourcefile_lookup(file);
  curr_srcfile = rblineprof.curr_srcfile;

  if (curr_srcfile != srcfile) {
    if (curr_srcfile)
      curr_srcfile->exclusive_time += now - curr_srcfile->exclusive_start;

    if (srcfile)
      srcfile->exclusive_start = now;

    rblineprof.curr_srcfile = srcfile;
  }

  /* line profiler: maintain a stack of CALL events with timestamps. for
   * each corresponding RETURN, account elapsed time to the calling line.
   *
   * we use ruby_current_node here to get the caller's file/line info,
   * (as opposed to node, which points to the callee method being invoked)
   */

#ifndef RUBY_18
  VALUE *caller_node = ruby_frame->node;
#else
  NODE *caller_node = ruby_frame->node;
#endif

  if (!caller_node) return;

#ifndef RUBY_18
  file = "fixme";
#else
  file = caller_node->nd_file;
#endif

  line = nd_line(caller_node);
  if (!file) return;
  if (line <= 0) return;

#ifdef RUBY_18
  if (caller_node->nd_file != node->nd_file)
#else
  if (caller_node != "fixme")
#endif

    srcfile = sourcefile_lookup(file);
  if (!srcfile) return; /* skip line profiling for this file */

  switch (event) {
    case RUBY_EVENT_CALL:
    case RUBY_EVENT_C_CALL:
      rblineprof.stack_depth++;
      if (rblineprof.stack_depth > 0 && rblineprof.stack_depth < MAX_STACK_DEPTH) {
        frame = &rblineprof.stack[rblineprof.stack_depth-1];
        frame->event = event;
        frame->node = node;
        frame->self = self;
        frame->mid = mid;
        frame->klass = klass;
        frame->line = line;
        frame->start = now;
        frame->srcfile = srcfile;
      }
      break;

    case RUBY_EVENT_RETURN:
    case RUBY_EVENT_C_RETURN:
      do {
        if (rblineprof.stack_depth > 0 && rblineprof.stack_depth < MAX_STACK_DEPTH)
          frame = &rblineprof.stack[rblineprof.stack_depth-1];
        else
          frame = NULL;
        rblineprof.stack_depth--;
      } while (frame && frame->self != self && frame->mid != mid && frame->klass != klass);

      if (frame)
        stackframe_record(frame, now);

      break;
  }
}

static int
cleanup_files(st_data_t key, st_data_t record, st_data_t arg)
{
  xfree((char *)key);

  sourcefile_t *sourcefile = (sourcefile_t*)record;
  if (!sourcefile || (VALUE)sourcefile == Qnil) return ST_DELETE;

  if (sourcefile->lines)
    xfree(sourcefile->lines);
  xfree(sourcefile);

  return ST_DELETE;
}

static int
summarize_files(st_data_t key, st_data_t record, st_data_t arg)
{
  sourcefile_t *srcfile = (sourcefile_t*)record;
  if (!srcfile || (VALUE)srcfile == Qnil) return ST_CONTINUE;

  VALUE ret = (VALUE)arg;
  VALUE ary = rb_ary_new();
  long i;

  rb_ary_store(ary, 0, ULL2NUM(srcfile->exclusive_time));
  for (i=1; i<srcfile->nlines; i++)
    rb_ary_store(ary, i, rb_ary_new3(2, ULL2NUM(srcfile->lines[i].total_time), ULL2NUM(srcfile->lines[i].calls)));

  rb_hash_aset(ret, rb_str_new2(srcfile->filename), ary);

  return ST_CONTINUE;
}

static VALUE
lineprof_ensure(VALUE self)
{
  rb_remove_event_hook(profiler_hook);
  rblineprof.enabled = false;
}

VALUE
lineprof(VALUE self, VALUE filename)
{
  if (!rb_block_given_p())
    rb_raise(rb_eArgError, "block required");

  if (rblineprof.enabled)
    rb_raise(rb_eArgError, "profiler is already enabled");

  VALUE filename_class = rb_obj_class(filename);

  if (filename_class == rb_cString) {
    rblineprof.source_filename = rb_source_filename(StringValuePtr(filename));
  } else if (filename_class == rb_cRegexp) {
    rblineprof.source_regex = filename;
    rblineprof.source_filename = NULL;
  } else {
    rb_raise(rb_eArgError, "argument must be String or Regexp");
  }

  // reset state
  rblineprof.curr_srcfile = NULL;
  st_foreach(rblineprof.files, cleanup_files, 0);
  if (rblineprof.file.lines) {
    xfree(rblineprof.file.lines);
    rblineprof.file.lines = NULL;
    rblineprof.file.nlines = 0;
  }

  rblineprof.enabled = true;
#ifdef RUBY_VM
  rb_add_event_hook(profiler_hook, RUBY_EVENT_CALL|RUBY_EVENT_RETURN|RUBY_EVENT_C_CALL|RUBY_EVENT_C_RETURN, self);
#else
  rb_add_event_hook(profiler_hook, RUBY_EVENT_CALL|RUBY_EVENT_RETURN|RUBY_EVENT_C_CALL|RUBY_EVENT_C_RETURN);
#endif

  rb_ensure(rb_yield, Qnil, lineprof_ensure, self);

  sourcefile_t *curr_srcfile = rblineprof.curr_srcfile;
  if (curr_srcfile)
    curr_srcfile->exclusive_time += timeofday_usec() - curr_srcfile->exclusive_start;

  VALUE ret = rb_hash_new();
  VALUE ary = Qnil;

  if (rblineprof.source_filename) {
    summarize_files(Qnil, (st_data_t)&rblineprof.file, ret);
  } else {
    st_foreach(rblineprof.files, summarize_files, ret);
  }

  return ret;
}

static void
rblineprof_gc_mark()
{
  if (rblineprof.enabled)
    rb_gc_mark_maybe(rblineprof.source_regex);
}

void
Init_rblineprof()
{
  gc_hook = Data_Wrap_Struct(rb_cObject, rblineprof_gc_mark, NULL, NULL);
  rb_global_variable(&gc_hook);

  rblineprof.files = st_init_strtable();
  rb_define_global_function("lineprof", lineprof, 1);
}

/* vim: ts=2,sw=2,expandtab */
