$:.unshift 'ext'
require 'rblineprof'

profile = lineprof(/./) do
  sleep 0.001

  100.times do

    sleep 0.001
    1*2*3
    4*5*6
    7*8*9*10*11*12*13*14*15
    2**32
    2**128

  end
end

File.readlines(__FILE__).each_with_index do |line, num|
  if (sample = profile[__FILE__][num+1]) > 0
    # printf "% 7d |  %s", sample, line
    printf "% 8.1fms |  %s", sample/1000.0, line
  else
    # printf "        | %s", line
    printf "           | %s", line
  end
end
