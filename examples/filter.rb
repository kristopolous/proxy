#!/usr/bin/env ruby

require 'rubygems'
require 'json'

filter = {}
extract = []

ARGV.each do | arg |
  type, directive = arg.split ':'

  if type == 'filter'
    key, value = directive.split '='
    filter[key] = value
    puts "Filtering on #{key} = #{value}"

  elsif type == 'extract'
    extract << directive
    puts "Extracting #{directive}"
  end
end

STDIN.each_line do | line |
  obj = JSON.parse(line)

  catch "MovingOn" do
    filter.each do | key, value |
      if obj.has_key? key and obj[key] == value

        if extract.length > 0
          extract.each do | key |
            puts obj[key]
          end
        else
          puts line
        end

        throw "MovingOn"
      end
    end
  end
end
