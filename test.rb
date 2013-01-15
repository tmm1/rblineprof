$:.unshift 'ext'
require 'rblineprof'

def inner
  sleep 0.2 # this gets called a total of 3 times
  puts 'inner'
  4*5*6*20; sleep 0.3
end

def outer
  sleep 0.1 # this gets called a total of 2 times
  inner
end

profile = lineprof(/./) do
  10.times do
    sleep 0.1
  end
  outer
  inner
  outer
end

File.readlines(__FILE__).each_with_index do |line, num|
  if (sample = profile[__FILE__][num+1]) > 0
    printf "% 8.1fms |  %s", sample/1000.0, line
  else
    printf "           | %s", line
  end
end
