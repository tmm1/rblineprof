$:.unshift 'ext'
require 'rblineprof'

class Obj
  define_method(:inner_block) do
    sleep 0.001
  end

  def another(options={})
    sleep 0.001
  end
end

def inner
  sleep 0.001
  1*2*3
  4*5*6
  7*8*9*10*11*12*13*14*15
  2**32
  2**128

  o = Obj.new
  o.inner_block
  o.another
end

def outer
  sleep 0.001

  100.times do
    inner
  end

  inner
end

profile = lineprof(/./) do
  outer
end

file = File.expand_path(__FILE__)
File.readlines(file).each_with_index do |line, num|
  time, calls = profile[file][num+1]
  if calls && calls > 0
    printf "% 8.1fms (% 5d) | %s", time/1000.0, calls, line
  else
    printf "                   | %s", line
  end
end
