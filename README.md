# rblineprof

```
% ruby -C ext extconf.rb
% make -C ext
% ruby test.rb 
           | $:.unshift 'ext'
           | require 'rblineprof'
           | 
           | profile = lineprof(/./) do
     1.2ms |    sleep 0.001
           | 
     0.1ms |    100.times do
           | 
   119.6ms |      sleep 0.001
     0.7ms |      1*2*3
     0.2ms |      4*5*6
     0.4ms |      7*8*9*10*11*12*13*14*15
     0.2ms |      2**32
     1.4ms |      2**128
           | 
           |   end
           | end
           | 
           | File.readlines(__FILE__).each_with_index do |line, num|
           |   if (sample = profile[__FILE__][num+1]) > 0
           |     printf "% 8.1fms |  %s", sample/1000.0, line
           |   else
           |     printf "           | %s", line
           |   end
           | end
```

### Rails integration

* [peek-rblineprof](https://github.com/peek/peek-rblineprof#peekrblineprof)

## Other profilers

* [PLine](https://github.com/soba1104/PLine) line-profiler for ruby 1.9
* pure-ruby [LineProfiler](http://blade.nagaokaut.ac.jp/cgi-bin/scat.rb/ruby/ruby-talk/18997?help-en) for ruby 1.6
* [method_profiler](https://github.com/change/method_profiler)
* [ruby-prof](https://github.com/rdp/ruby-prof)
* [perftools.rb](https://github.com/tmm1/perftools.rb)
* [zenprofile](https://github.com/seattlerb/zenprofile)

## License

rblineprof is released under the [MIT License](http://www.opensource.org/licenses/MIT).
