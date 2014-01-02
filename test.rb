$:.unshift 'lib'
require 'rblineprof'

class Obj
  define_method(:inner_block) do
    sleep 0.001
  end

  def another(options={})
    sleep 0.001
  end

  def out=(*)
  end

  def with_defaults(arg=self.object_id.to_s)
    another
    list = [1,2,3]
    # for cookie in list
    #   self.out=(
        dummy(
          1, "str
          ing")
        dummy <<-EOS
          hi
        EOS
        dummy \
          1234
        dummy :a => 'b',
          :c => 'd',
          :e => 1024**1024,
          'something' => dummy(:ok)
    #   )
    # end
  end

  def dummy(*args)
    args.inspect
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
  o.with_defaults
end

def outer
  sleep 0.01

  3000.times{ 2**1024 }
  for i in 1..3000 do 2**1024 end

  for i in 1..3000
    2**1024
  end

  (fibonacci = Hash.new{ |h,k| h[k] = k < 2 ?  k : h[k-1] + h[k-2] })[500]

  (fibonacci = Hash.new{ |h,k|
    h[k] = k < 2 ?
      k :
      h[k-1] +
      h[k-2]
  })
  fibonacci[500]

  100.times do
    inner
  end

  inner

  (0..10).map do |i|
    Thread.new(i) do
      inner
    end
  end.each(&:join)
end

file = RUBY_VERSION > '1.9' ? File.expand_path(__FILE__) : __FILE__

# profile = lineprof(file) do
profile = lineprof(/./) do
  outer

  100.times{ 1 }
  100.times{ 1 + 1 }
  100.times{ 1.1 }
  100.times{ 1.1 + 1 }
  100.times{ 1.1 + 1.1 }
  100.times{ "str" }
  ('a'..'z').to_a
end

allocation_mode = false

File.readlines(file).each_with_index do |line, num|
  wall, cpu, calls, allocations = profile[file][num+1]

  if allocation_mode
    if allocations > 0
      printf "% 10d objs | %s", allocations, line
    else
      printf "                | %s", line
    end

    next
  end

  if calls && calls > 0
    printf "% 8.1fms + % 8.1fms (% 5d) | %s", cpu/1000.0, (wall-cpu)/1000.0, calls, line
    # printf "% 8.1fms (% 5d) | %s", wall/1000.0, calls, line
  else
    printf "                                | %s", line
    # printf "                   | %s", line
  end
end

puts
profile.each do |file, data|
  total, child, exclusive, allocations = data[0]
  puts file
  printf "  % 10.1fms in this file\n", exclusive/1000.0
  printf "  % 10.1fms in this file + children\n", total/1000.0
  printf "  % 10.1fms in children\n", child/1000.0
  puts
end
