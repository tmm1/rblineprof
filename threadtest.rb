require 'rblineprof'

$line_times = Hash.new(0.0)

def main
  profile = log(__LINE__) { getprofile }
  profile = profile.values.first
  File.readlines(__FILE__).each_with_index do |content, i|
    lineno = i + 1
    line_time =
      if $line_times.has_key?(lineno)
        sprintf('%6.3fms', $line_times[lineno] * 1000.0)
      else
        (' '*8)
      end
    prof_time =
      if profile[lineno] && profile[lineno].any? { |x| x > 0 }
        wall, cpu, _ = profile[lineno]
        sprintf('%6.3fms (%6.3fms cpu)', wall / 1000.0, cpu / 1000.0)
      else
        ' '*23
      end
    puts "#{line_time} vs #{prof_time} | #{'%2d' % lineno} | #{content}"
  end
end

def getprofile
  lineprof(File.expand_path(__FILE__)) do
    t = Thread.new do
      log(__LINE__) do
        thread_work
      end
    end
    log(__LINE__) do
      main_work
    end
    log(__LINE__) do
      t.join
    end
  end
end

def thread_work
  work(2000)
end

def main_work
  work(3000)
end

def work(n)
  log(__LINE__) { sleep 0.01          }
  log(__LINE__) { n.times { 2**1024 } }
end

def log(line)
  start = Time.now
  yield
ensure
  $line_times[line] += Time.now - start
end

main
