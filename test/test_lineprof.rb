$:.unshift File.expand_path('../../lib', __FILE__)
require 'rblineprof'
require 'test/unit'

class LineProfTest < Test::Unit::TestCase
  def test_real
    profile = lineprof(/./) do
      sleep 0.001
    end

    line = profile[__FILE__][__LINE__-3]
    assert_in_delta 1000, line[0], 600
    assert_equal 1, line[2]
  end

  def test_cpu
    profile = lineprof(/./) do
      (fibonacci = Hash.new{ |h,k| h[k] = k < 2 ?  k : h[k-1] + h[k-2] })[500]
    end

    line = profile[__FILE__][__LINE__-3]
    assert_operator line[1], :>=, 800
  end

  def test_objects
    profile = lineprof(/./) do
      100.times{ "str" }
    end

    line = profile[__FILE__][__LINE__-3]
    assert_equal 100, line[3]
  end

  def test_method
    profile = lineprof(/./) do
      100.times{ helper_method }
    end

    m = method(:helper_method)
    line = profile[__FILE__][m.source_location.last]
    assert_equal 0, line[0]
  end

  def helper_method
    sleep 0.001
  end
end
