# rblineprof

```
% ruby -C ext extconf.rb
% make -C ext
% ruby test.rb 
       | $:.unshift 'ext'
       | require 'rblineprof'
       | 
       | per_line = lineprof(__FILE__) do
     1 |    1000.times do
       | 
   269 |      1*2*3
   243 |      4*5*6
   840 |      7*8*9*10*11*12*13*14*15
   204 |      2**32
   655 |      2**128
       | 
       |   end
       | end
       | 
       | File.readlines(__FILE__).each_with_index do |line, num|
       |   sample = per_line[num+1]
       |   if sample
       |     printf "% 6d |  %s", per_line[num+1], line
       |   else
       |     printf "       | %s", line
       |   end
       | end
```
