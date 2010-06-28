#!/usr/bin/python
import sys
from scope2 import *
import time

s = Scope()
print "pins", s.list_pins()

s.attach_thread('thread1')
s.set_channel_pin(0, 'charge-pump.out')
s.set_channel_pin(1, 'siggen.0.cosine')
s.start_capture()

r = []
N=10
while len(r) < N:
    r.extend(s.get_samples(N))
    time.sleep(.001)
s.stop_capture()
o = s.check_overflow()

for i in r: print "%d %f" % (i[0], i[1])
if o: print "lost %d samples due to overflow" % o
