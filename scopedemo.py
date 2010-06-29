#!/usr/bin/python
import sys
from scope2 import *
import time

s = Scope()
print "pins", s.list_pins()

s.attach_thread('thread1')
period = s.get_thread_period()
s.set_channel_pin(0, 'charge-pump.out')
s.set_channel_pin(1, 'siggen.0.cosine')
s.set_channel_pin(2, 'siggen.0.sine')
s.start_capture()

r = []
N=4000
c=0
while len(r) < N:
    c += 1
    r.extend(s.get_samples(N-len(r)))
    time.sleep(.01)
s.stop_capture()
o = s.check_overflow()

print "Got %d samples in %d queries" % (len(r), c)
if o: print "lost %d samples due to overflow" % o

import gtk
import goocanvas
import cairo
class Capture:
    def __init__(self):
	self.s = Scope()
	self.channels = {}
	print self, dir(self)

    def attach_thread(self, thread):
	self.s.attach_thread(thread)

    def start_capture(self): self.s.start_capture()
    def stop_capture(self): self.s.stop_capture()
    def capture_state(self): return self.s.capture_state()

    def get_thread_period(self, *opt_name):
	return self.s.get_thread_period(*opt_name)

    def get_available_channel(self):
	print self, dir(self)
	for c in range(NCHANNELS):
	    if c not in self.channels: return c
	raise ValueError, "all channels in use"	    

    def use_channel(self, trace, channel):
	self.channels[channel] = weakref.proxy(trace)

    def add_pin(self, trace, pin):
	channel = self.get_available_channel()
	self.s.set_channel_pin(channel, pin)
	self.use_channel(trace, channel)

    def add_param(self, trace, param):
	channel = self.get_available_channel()
	self.s.set_channel_param(channel, pin)
	self.use_channel(trace, channel)

    def add_sig(self, trace, sig):
	channel = self.get_available_channel()
	self.s.set_channel_sig(channel, pin)
	self.use_channel(trace, channel)

    def poll(self):
	assert self.capture_state(), "must be capturing to poll"
	samples = self.s.get_samples()
	print "poll()", len(samples)
	for k, v in self.channels.items():
	    v.extend(sample[k] for sample in samples)
	return len(samples)

    def check_overflow(self):
	return self.s.check_overflow()

class Trace:
    def __init__(self, data, hscale=1.0, vscale=1.0):
	self.data = list(data)
	self.vscale = vscale
	self.hscale = hscale
	self._cache = None

    def set_data(self, data):
	self.data = list(data)
	self.expire_cache()

    def set_vscale(self, vscale):
	self.vscale = vscale
	self.expire_cache()

    def set_hscale(self, hscale):
	self.hscale = hscale
	self.expire_cache()

    def extend(self, newdata):
	self.data.extend(newdata)
	self.expire_cache()

    def expire_cache(self):
	self._cache = None

    def get_pathdata(self):
	if not self.data: return ""
	if not self._cache:
	    pi = iter(enumerate(self.data))
	    xs = self.hscale
	    ys = self.vscale
	    x, y = pi.next()
	    data = ["M % f % f L" % (x*xs, y*ys)]
	    for x,y in pi:
		data.append("% f % f" % (x*xs, y*ys))
	    self._cache = " ".join(data)
	return self._cache

def trace2path(parent, points, xo, xs, yo, ys, **kw):
    t = Trace(points, xs, ys)
    data = t.get_pathdata()
    print data[:76]+"..."
    r = goocanvas.Path(parent=parent, data=data, **kw)
    r.translate(xo,yo)
    return r

w = gtk.Window()
w.connect("destroy", gtk.main_quit)
c = goocanvas.Canvas()
c.set_size_request(640,480)
w.add(c)
w.show_all()

path1 = trace2path(c.get_root_item(), (i[0] for i in r),
    xs=.1, ys=10, xo=0, yo=64,
    stroke_color="green", line_width=2.0)
path2 = trace2path(c.get_root_item(), (i[1] for i in r),
    xs=.1, ys=10, xo=0, yo=128,
    stroke_color="blue", line_width=2.0)
path3 = trace2path(c.get_root_item(), (i[2] for i in r),
    xs=.1, ys=10, xo=0, yo=128,
    stroke_color="black", line_width=2.0)

gtk.main()
