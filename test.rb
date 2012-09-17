$:.unshift 'ext'
require 'rblineprof'

profile = lineprof(/./) do
  1000.times do

    1*2*3
    4*5*6
    7*8*9*10*11*12*13*14*15
    2**32
    2**128

  end
end

File.readlines(__FILE__).each_with_index do |line, num|
  if (sample = profile[__FILE__][num+1]) > 0
    printf "% 6d |  %s", sample, line
  else
    printf "       | %s", line
  end
end
