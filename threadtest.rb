require 'rblineprof'

def main
  lines = File.readlines(__FILE__)
  profile = log('total') { getprofile }
  line_profs = profile[File.expand_path(__FILE__)].drop(1).take(lines.length)
  line_profs.zip(lines).each do |(wall, cpu, calls, allocations), content|
    printf "%8.1fms / %8.1fms (% 5d) (% 6d) | %s", wall/1000.0, cpu/1000.0, calls, allocations, content
  end
end

def getprofile
  lineprof(/./) do
    t = Thread.new do
      log('thread') do
        thread_work
      end
    end
    log('main') do
      main_work
    end
    log('waitthread') do
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
  log('sleep') { sleep 0.01          }
  log('math')  { n.times { 2**1024 } }
end

def log(label)
  start = Time.now
  yield
ensure
  elapsed = Time.now - start
  printf "%s: %.1fms\n", label, elapsed*1000.0
end

main
