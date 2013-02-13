Gem::Specification.new do |s|
  s.name = 'rblineprof'
  s.version = '0.2.7'
  s.homepage = 'http://github.com/tmm1/rblineprof'

  s.authors = 'Aman Gupta'
  s.email   = 'aman@tmm1.net'

  s.files = `git ls-files`.split("\n")
  s.extensions = 'ext/extconf.rb'

  s.summary = 'line-profiler for ruby'
  s.description = 'rblineprof shows you lines of code that are slow.'
end
