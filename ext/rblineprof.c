#include <ruby.h>
#include <ruby/version.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

#if defined(RUBY_VM)
  #include <ruby/re.h>
  #include <ruby/intern.h>

  #if defined(HAVE_RB_PROFILE_FRAMES)
    #include <ruby/debug.h>
  #else
    #include <vm_core.h>
    #include <iseq.h>

    // There's a compile error on 1.9.3. So:
    #ifdef RTYPEDDATA_DATA
    #define ruby_current_thread ((rb_thread_t *)RTYPEDDATA_DATA(rb_thread_current()))
    #endif
  #endif
#else
  #include <st.h>
  #include <re.h>
  #include <intern.h>
  #include <node.h>
  #include <env.h>
  typedef rb_event_t rb_event_flag_t;
#endif

#if defined(HAVE_RB_OS_ALLOCATED_OBJECTS) && defined(RUBY_VM)
size_t rb_os_allocated_objects(void);
#endif

static VALUE gc_hook;
static VALUE sym_total_allocated_objects;

/*
 * Time in microseconds
 */
typedef uint64_t prof_time_t;

/*
 * Profiling snapshot
 */
typedef struct snapshot {
  prof_time_t wall_time;
  prof_time_t cpu_time;
#if defined(HAVE_RB_OS_ALLOCATED_OBJECTS) || defined(HAVE_RB_GC_STAT)
  size_t allocated_objects;
#endif
} snapshot_t;

/*
 * A line of Ruby source code
 */
typedef struct sourceline {
  uint64_t calls; // total number of call/c_call events
  snapshot_t total;
} sourceline_t;

/*
 * Struct representing a single Ruby file.
 */
typedef struct sourcefile {
  char *filename;

  /* per line timing */
  long nlines;
  sourceline_t *lines;

  /* overall file timing */
  snapshot_t total;
  snapshot_t child;
  uint64_t depth;
  snapshot_t exclusive_start;
  snapshot_t exclusive;
} sourcefile_t;

/*
 * An individual stack frame used to track
 * calls and returns from Ruby methods
 */
typedef struct stackframe {
  // data emitted from Ruby to our profiler hook
  rb_event_flag_t event;
#if defined(HAVE_RB_PROFILE_FRAMES)
  VALUE thread;
#elif defined(RUBY_VM)
  rb_thread_t *thread;
#else
  NODE *node;
#endif
  VALUE self;
  ID mid;
  VALUE klass;

  char *filename;
  long line;

  snapshot_t start;
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

  // single file mode, store filename and line data directly
  char *source_filename;
  sourcefile_t file;

  // regex mode, store file data in hash table
  VALUE source_regex;
  st_table *files;

  // cache
  struct {
    char *file;
    sourcefile_t *srcfile;
  } cache;
}
rblineprof = {
  .enabled = false,
  .source_regex = Qfalse
};

static prof_time_t
cputime_usec()
{
#if defined(__linux__)
  struct timespec ts;

  if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) {
    return (prof_time_t)ts.tv_sec*1e6 +
           (prof_time_t)ts.tv_nsec*1e-3;
  }
#endif

#if defined(RUSAGE_SELF)
  struct rusage usage;

  getrusage(RUSAGE_SELF, &usage);
  return (prof_time_t)usage.ru_utime.tv_sec*1e6 +
         (prof_time_t)usage.ru_utime.tv_usec;
#endif

  return 0;
}

static prof_time_t
walltime_usec()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (prof_time_t)tv.tv_sec*1e6 +
         (prof_time_t)tv.tv_usec;
}

static inline snapshot_t
snapshot_diff(snapshot_t *t1, snapshot_t *t2)
{
  snapshot_t diff = {
    .wall_time         = t1->wall_time - t2->wall_time,
    .cpu_time          = t1->cpu_time  - t2->cpu_time,
#if defined(HAVE_RB_OS_ALLOCATED_OBJECTS) || defined(HAVE_RB_GC_STAT)
    .allocated_objects = t1->allocated_objects - t2->allocated_objects
#endif
  };

  return diff;
}

static inline void
snapshot_increment(snapshot_t *s, snapshot_t *inc)
{
  s->wall_time         += inc->wall_time;
  s->cpu_time          += inc->cpu_time;
#if defined(HAVE_RB_OS_ALLOCATED_OBJECTS) || defined(HAVE_RB_GC_STAT)
  s->allocated_objects += inc->allocated_objects;
#endif
}

static inline void
stackframe_record(stackframe_t *frame, snapshot_t now, stackframe_t *caller_frame)
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

  snapshot_t diff = snapshot_diff(&now, &frame->start);
  sourceline_t *srcline = &(srcfile->lines[line]);

  /* Line profiler metrics.
   */

  srcline->calls++;

  /* Increment current line's total_time.
   *
   * Skip the special case where the stack frame we're returning to
   * had the same file/line. This fixes double counting on crazy one-liners.
   */
  if (!(caller_frame && caller_frame->srcfile == frame->srcfile && caller_frame->line == frame->line))
    snapshot_increment(&srcline->total, &diff);

  /* File profiler metrics.
   */

  /* Increment the caller file's child_time.
   */
  if (caller_frame && caller_frame->srcfile != srcfile)
    snapshot_increment(&caller_frame->srcfile->child, &diff);

  /* Increment current file's total_time, but only when we return
   * to the outermost stack frame when we first entered the file.
   */
  if (srcfile->depth == 0)
    snapshot_increment(&srcfile->total, &diff);
}

static inline sourcefile_t*
sourcefile_lookup(char *filename)
{
  sourcefile_t *srcfile = NULL;

  if (rblineprof.source_filename) { // single file mode
#ifdef RUBY_VM
    if (strcmp(rblineprof.source_filename, filename) == 0) {
#else
    if (rblineprof.source_filename == filename) { // compare char*, not contents
#endif
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
      VALUE backref = rb_backref_get();
      rb_match_busy(backref);
      long rc = rb_reg_search(rblineprof.source_regex, rb_str_new2(filename), 0, 0);
      rb_backref_set(backref);

      if (rc >= 0) {
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

#if defined(RUBY_VM) && !defined(HAVE_RB_PROFILE_FRAMES)
/* Find the source of the current method call. This is based on rb_f_caller
 * in vm_eval.c, and replicates the behavior of `caller.first` from ruby.
 *
 * On method calls, ruby 1.9 sends an extra RUBY_EVENT_CALL event with mid=0. The
 * top-most cfp on the stack in these cases points to the 'def method' line, so we skip
 * these and grab the second caller instead.
 */
static inline
rb_control_frame_t *
rb_vm_get_caller(rb_thread_t *th, rb_control_frame_t *cfp, ID mid)
{
  int level = 0;

  while (!RUBY_VM_CONTROL_FRAME_STACK_OVERFLOW_P(th, cfp)) {
    if (++level == 1 && mid == 0) {
      // skip method definition line
    } else if (cfp->iseq != 0 && cfp->pc != 0) {
      return cfp;
    }

    cfp = RUBY_VM_PREVIOUS_CONTROL_FRAME(cfp);
  }

  return 0;
}

#ifdef HAVE_TYPE_RB_ISEQ_LOCATION_T
inline static int
calc_lineno(const rb_iseq_t *iseq, const VALUE *pc)
{
  return rb_iseq_line_no(iseq, pc - iseq->iseq_encoded);
}

int
rb_vm_get_sourceline(const rb_control_frame_t *cfp)
{
  int lineno = 0;
  const rb_iseq_t *iseq = cfp->iseq;

  if (RUBY_VM_NORMAL_ISEQ_P(iseq)) {
    lineno = calc_lineno(cfp->iseq, cfp->pc);
  }
  return lineno;
}
#endif
#endif

static void
#ifdef RUBY_VM
profiler_hook(rb_event_flag_t event, VALUE data, VALUE self, ID mid, VALUE klass)
#else
profiler_hook(rb_event_flag_t event, NODE *node, VALUE self, ID mid, VALUE klass)
#endif
{
  char *file;
  long line;
  stackframe_t *frame = NULL, *prev = NULL;
  sourcefile_t *srcfile;

  /* line profiler: maintain a stack of CALL events with timestamps. for
   * each corresponding RETURN, account elapsed time to the calling line.
   *
   * we use ruby_current_node here to get the caller's file/line info,
   * (as opposed to node, which points to the callee method being invoked)
   */
#if defined(HAVE_RB_PROFILE_FRAMES)
  VALUE path, iseq;
  VALUE iseqs[2];
  int lines[2];
  int i = 0, l, n = rb_profile_frames(0, 2, iseqs, lines);

  if (n == 0) return;
  if (mid == 0 && n == 2) /* skip empty frame on method definition line */
    i = 1;

  l = lines[i];
  iseq = iseqs[i];

  /* TODO: use fstring VALUE directly */
  path = rb_profile_frame_absolute_path(iseq);
  if (!RTEST(path)) path = rb_profile_frame_path(iseq);
  file = RSTRING_PTR(path);
  line = l;
#elif !defined(RUBY_VM)
  NODE *caller_node = ruby_frame->node;
  if (!caller_node) return;

  file = caller_node->nd_file;
  line = nd_line(caller_node);
#else
  rb_thread_t *th = ruby_current_thread;
  rb_control_frame_t *cfp = rb_vm_get_caller(th, th->cfp, mid);
  if (!cfp) return;

  #ifdef HAVE_TYPE_RB_ISEQ_LOCATION_T
    if (RTEST(cfp->iseq->location.absolute_path))
      file = StringValueCStr(cfp->iseq->location.absolute_path);
    else
      file = StringValueCStr(cfp->iseq->location.path);
  #else
    if (RTEST(cfp->iseq->filepath))
      file = StringValueCStr(cfp->iseq->filepath);
    else
      file = StringValueCStr(cfp->iseq->filename);
  #endif
  line = rb_vm_get_sourceline(cfp);
#endif

  if (!file) return;
  if (line <= 0) return;

  /* find the srcfile entry for the current file.
   *
   * first check the cache, in case this is the same file as
   * the previous invocation.
   *
   * if no record is found, we don't care about profiling this
   * file and return early.
   */
  if (rblineprof.cache.file == file)
    srcfile = rblineprof.cache.srcfile;
  else
    srcfile = sourcefile_lookup(file);
  rblineprof.cache.file = file;
  rblineprof.cache.srcfile = srcfile;
  if (!srcfile) return; /* skip line profiling for this file */

  snapshot_t now = {
    .wall_time         = walltime_usec(),
    .cpu_time          = cputime_usec(),
#if defined(HAVE_RB_OS_ALLOCATED_OBJECTS)
    .allocated_objects = rb_os_allocated_objects()
#elif defined(HAVE_RB_GC_STAT)
    .allocated_objects = rb_gc_stat(sym_total_allocated_objects)
#endif
  };

  switch (event) {
    case RUBY_EVENT_CALL:
    case RUBY_EVENT_C_CALL:
      /* Create a stack frame entry with this event,
       * the current file, and a snapshot of metrics.
       *
       * On a corresponding RETURN event later, we can
       * pop this stack frame and accumulate metrics to the
       * associated file and line.
       */
      rblineprof.stack_depth++;
      if (rblineprof.stack_depth > 0 && rblineprof.stack_depth < MAX_STACK_DEPTH) {
        frame = &rblineprof.stack[rblineprof.stack_depth-1];
        frame->event = event;
        frame->self = self;
        frame->mid = mid;
        frame->klass = klass;
        frame->line = line;
        frame->start = now;
        frame->srcfile = srcfile;
#if defined(HAVE_RB_PROFILE_FRAMES)
        frame->thread = rb_thread_current();
#elif defined(RUBY_VM)
        frame->thread = th;
#else
        frame->node = node;
#endif
      }

      /* Record when we entered this file for the first time.
       * The difference is later accumulated into exclusive_time,
       * e.g. on the next event if the file changes.
       */
      if (srcfile->depth == 0)
        srcfile->exclusive_start = now;
      srcfile->depth++;

      if (rblineprof.stack_depth > 1) { // skip if outermost frame
        prev = &rblineprof.stack[rblineprof.stack_depth-2];

        /* If we just switched files, record time that was spent in
         * the previous file.
         */
        if (prev->srcfile != frame->srcfile) {
          snapshot_t diff = snapshot_diff(&now, &prev->srcfile->exclusive_start);
          snapshot_increment(&prev->srcfile->exclusive, &diff);
          prev->srcfile->exclusive_start = now;
        }
      }
      break;

    case RUBY_EVENT_RETURN:
    case RUBY_EVENT_C_RETURN:
      /* Find the corresponding CALL for this event.
       *
       * We loop here instead of a simple pop, because in the event of a
       * raise/rescue several stack frames could have disappeared.
       */
      do {
        if (rblineprof.stack_depth > 0 && rblineprof.stack_depth < MAX_STACK_DEPTH) {
          frame = &rblineprof.stack[rblineprof.stack_depth-1];
          if (frame->srcfile->depth > 0)
            frame->srcfile->depth--;
        } else
          frame = NULL;

        if (rblineprof.stack_depth > 0)
          rblineprof.stack_depth--;
      } while (frame &&
#if defined(HAVE_RB_PROFILE_FRAMES)
               frame->thread != rb_thread_current() &&
#elif defined(RUBY_VM)
               frame->thread != th &&
#endif
               /* Break when we find a matching CALL/C_CALL.
                */
               frame->event != (event == RUBY_EVENT_CALL ? RUBY_EVENT_RETURN : RUBY_EVENT_C_RETURN) &&
               frame->self != self &&
               frame->mid != mid &&
               frame->klass != klass);

      if (rblineprof.stack_depth > 0) {
        // The new top of the stack (that we're returning to)
        prev = &rblineprof.stack[rblineprof.stack_depth-1];

        /* If we're leaving this frame to go back to a different file,
         * accumulate time we spent in this file.
         *
         * Note that we do this both when entering a new file and leaving to
         * a new file to ensure we only count time spent exclusively in that file.
         * Consider the following scenario:
         *
         *     call (a.rb:1)
         *       call (b.rb:1)         <-- leaving a.rb, increment into exclusive_time
         *         call (a.rb:5)
         *         return              <-- leaving a.rb, increment into exclusive_time
         *       return
         *     return
         */
        if (frame->srcfile != prev->srcfile) {
          snapshot_t diff = snapshot_diff(&now, &frame->srcfile->exclusive_start);
          snapshot_increment(&frame->srcfile->exclusive, &diff);
          frame->srcfile->exclusive_start = now;
          prev->srcfile->exclusive_start = now;
        }
      }

      if (frame)
        stackframe_record(frame, now, prev);

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

  rb_ary_store(ary, 0, rb_ary_new3(
#if defined(HAVE_RB_OS_ALLOCATED_OBJECTS) || defined(HAVE_RB_GC_STAT)
    7,
#else
    6,
#endif
    ULL2NUM(srcfile->total.wall_time),
    ULL2NUM(srcfile->child.wall_time),
    ULL2NUM(srcfile->exclusive.wall_time),
    ULL2NUM(srcfile->total.cpu_time),
    ULL2NUM(srcfile->child.cpu_time),
    ULL2NUM(srcfile->exclusive.cpu_time)
#if defined(HAVE_RB_OS_ALLOCATED_OBJECTS) || defined(HAVE_RB_GC_STAT)
    , ULL2NUM(srcfile->total.allocated_objects)
#endif
  ));

  for (i=1; i<srcfile->nlines; i++)
    rb_ary_store(ary, i, rb_ary_new3(
#if defined(HAVE_RB_OS_ALLOCATED_OBJECTS) || defined(HAVE_RB_GC_STAT)
      4,
#else
      3,
#endif
      ULL2NUM(srcfile->lines[i].total.wall_time),
      ULL2NUM(srcfile->lines[i].total.cpu_time),
      ULL2NUM(srcfile->lines[i].calls)
#if defined(HAVE_RB_OS_ALLOCATED_OBJECTS) || defined(HAVE_RB_GC_STAT)
      , ULL2NUM(srcfile->lines[i].total.allocated_objects)
#endif
    ));
  rb_hash_aset(ret, rb_str_new2(srcfile->filename), ary);

  return ST_CONTINUE;
}

static VALUE
lineprof_ensure(VALUE self)
{
  rb_remove_event_hook((rb_event_hook_func_t) profiler_hook);
  rblineprof.enabled = false;
  return self;
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
#ifdef RUBY_VM
    rblineprof.source_filename = (char *) (StringValuePtr(filename));
#else
    /* rb_source_filename will return a string we can compare directly against
     * node->file, without a strcmp()
     */
    rblineprof.source_filename = rb_source_filename(StringValuePtr(filename));
#endif
  } else if (filename_class == rb_cRegexp) {
    rblineprof.source_regex = filename;
    rblineprof.source_filename = NULL;
  } else {
    rb_raise(rb_eArgError, "argument must be String or Regexp");
  }

  // reset state
  st_foreach(rblineprof.files, cleanup_files, 0);
  if (rblineprof.file.lines) {
    xfree(rblineprof.file.lines);
    rblineprof.file.lines = NULL;
    rblineprof.file.nlines = 0;
  }
  rblineprof.cache.file = NULL;
  rblineprof.cache.srcfile = NULL;

  rblineprof.enabled = true;
#ifndef RUBY_VM
  rb_add_event_hook((rb_event_hook_func_t) profiler_hook, RUBY_EVENT_CALL|RUBY_EVENT_RETURN|RUBY_EVENT_C_CALL|RUBY_EVENT_C_RETURN);
#else
  rb_add_event_hook((rb_event_hook_func_t) profiler_hook, RUBY_EVENT_CALL|RUBY_EVENT_RETURN|RUBY_EVENT_C_CALL|RUBY_EVENT_C_RETURN, Qnil);
#endif

  rb_ensure(rb_yield, Qnil, lineprof_ensure, self);

  VALUE ret = rb_hash_new();

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
#if RUBY_API_VERSION_MAJOR == 2 && RUBY_API_VERSION_MINOR < 2
  sym_total_allocated_objects = ID2SYM(rb_intern("total_allocated_object"));
#else
  sym_total_allocated_objects = ID2SYM(rb_intern("total_allocated_objects"));
#endif
  gc_hook = Data_Wrap_Struct(rb_cObject, rblineprof_gc_mark, NULL, NULL);
  rb_global_variable(&gc_hook);

  rblineprof.files = st_init_strtable();
  rb_define_global_function("lineprof", lineprof, 1);
}

/* vim: set ts=2 sw=2 expandtab: */
