$:.unshift 'ext'
require 'rblineprof'

def inner
end

def outer
  inner
end

profile = lineprof(/./) do
  10.times do
    sleep 0.1
    outer
  end
end

File.readlines(__FILE__).each_with_index do |line, num|
  if (sample = profile[__FILE__][num+1]) > 0
    printf "% 8.1fms |  %s", sample/1000.0, line
  else
    printf "           | %s", line
  end
end
