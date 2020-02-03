# rblineprof
Ruby profiler detecting and analyzing slowest lines of your code.

## Installation

`gem install rblineprof`

Or in your Gemfile:

`gem 'rblineprof'`

## Usage

```
require 'rblineprof'

profile = lineprof(/./) do
  sleep 0.001

  100.times do
    sleep 0.001

    1 * 2 * 3
    4 * 5 * 6
    7 * 8 * 9 * 10 * 11 * 12 * 13 * 14 * 15
    2 ** 32
    2 ** 128
  end
end

file = profile.keys.first

File.readlines(file).each_with_index do |line, num|
  wall, cpu, calls, allocations = profile[file][num + 1]

  if wall > 0 || cpu > 0 || calls > 0
    printf(
      "% 5.1fms + % 6.1fms (% 4d) | %s",
      cpu / 1000.0,
      (wall - cpu) / 1000.0,
      calls,
      line
    )
  else
    printf "                          | %s", line
  end
end
```

Will give you:

```
                          | require 'rblineprof'
                          |
                          | profile = lineprof(/./) do
  0.1ms +    1.4ms (   1) |   sleep 0.001
                          |
  2.7ms +  132.2ms (   1) |   100.times do
  1.3ms +  131.7ms ( 100) |     sleep 0.001
                          |
                          |     1 * 2 * 3
                          |     4 * 5 * 6
                          |     7 * 8 * 9 * 10 * 11 * 12 * 13 * 14 * 15
  0.1ms +    0.1ms ( 100) |     2 ** 32
  0.6ms +    0.1ms ( 100) |     2 ** 128
                          |   end
                          | end
                          |
                          | file = profile.keys.first
                          |
                          | File.readlines(file).each_with_index do |line, num|
                          |   wall, cpu, calls, allocations = profile[file][num + 1]
                          |
                          |   if wall > 0 || cpu > 0 || calls > 0
                          |     printf(
                          |       "% 5.1fms + % 6.1fms (% 4d) | %s",
                          |       cpu / 1000.0,
                          |       (wall - cpu) / 1000.0,
                          |       calls,
                          |       line
                          |     )
                          |   else
                          |     printf "                          | %s", line
                          |   end
                          | end

```

### Rails integration

* [peek-rblineprof](https://github.com/peek/peek-rblineprof#peekrblineprof)

## Other profilers

* [PLine](https://github.com/soba1104/PLine) line-profiler for ruby 1.9
* [LineProfiler](http://blade.nagaokaut.ac.jp/cgi-bin/scat.rb/ruby/ruby-talk/18997?help-en) for ruby 1.6
* [method_profiler](https://github.com/change/method_profiler)
* [ruby-prof](https://github.com/rdp/ruby-prof)
* [perftools.rb](https://github.com/tmm1/perftools.rb)
* [zenprofile](https://github.com/seattlerb/zenprofile)
* [rack-lineprof](https://github.com/kainosnoema/rack-lineprof)
* [rblineprof](https://github.com/tmm1/rblineprof)

## License

rblineprof is released under the [MIT License](http://www.opensource.org/licenses/MIT).
