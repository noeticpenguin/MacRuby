require File.expand_path('../../../../spec_helper', __FILE__)
require 'set'
require File.expand_path('../shared/difference', __FILE__)

describe "SortedSet#-" do
  it_behaves_like :sorted_set_difference, :-
end
