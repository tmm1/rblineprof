$:.unshift 'ext'
require 'rblineprof'

class Obj
  define_method(:inner_block) do
    sleep 0.001
  end

  def another(options={})
    sleep 0.001
  end

  class_eval <<-RUBY, 'otherfile.rb', 1
    def other_file
      another
    end
  RUBY
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
  o.other_file
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

puts
profile.each do |file, data|
  total, child, exclusive = data[0]
  puts file
  printf "  % 10.1fms in this file\n", exclusive/1000.0
  printf "  % 10.1fms in this file + children\n", total/1000.0
  printf "  % 10.1fms in children\n", child/1000.0
  puts
end
