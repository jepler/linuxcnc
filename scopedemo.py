#!/usr/bin/python
from scope2 import *
import cairo
import gobject
import goocanvas
import gtk
import itertools
import sys
import time
import weakref

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

    def expire_samples(self, max_samples):
	if len(self.data) > max_samples:
	    del self.data[:-max_samples]
	    self.expire_cache()

    def expire_cache(self):
	self._cache = None

    def get_pathdata(self, slicer = None):
	if not self.data: return ""
	if not self._cache:
	    if slicer:
		pi = iter(enumerate(itertools.islice(self.data, slicer.indices(len(self.data)))))
	    else:
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

cap = Capture()
t1 = Trace([], .2, 20); cap.add_pin(t1, 'charge-pump.out')
t2 = Trace([], .2, 20); cap.add_pin(t2, 'siggen.0.sine')
t3 = Trace([], .2, 20); cap.add_pin(t3, 'siggen.0.cosine')
cap.attach_thread("thread1")
cap.start_capture()

w = gtk.Window()
w.connect("destroy", gtk.main_quit)
canv = goocanvas.Canvas()
canv.set_size_request(640,480)
w.add(canv)
w.show_all()

cr = canv.get_root_item()

path1 = goocanvas.Path(parent=cr, data=t1.get_pathdata(), stroke_color="green", line_width=2.0)
path1.translate(0, 64)
path2 = goocanvas.Path(parent=cr, data=t2.get_pathdata(), stroke_color="blue", line_width=2.0)
path2.translate(0, 128)
path3 = goocanvas.Path(parent=cr, data=t2.get_pathdata(), stroke_color="red", line_width=2.0)
path3.translate(0, 128)

def painter():
    if cap.poll():
	t1.expire_samples(3200)
	t2.expire_samples(3200)
	t3.expire_samples(3200)
	print len(t2.data)
	print t2.get_pathdata()[:76]+"..."
	st = time.time()
	path1.set_property('data', t1.get_pathdata()); path1.request_update()
	path2.set_property('data', t2.get_pathdata()); path2.request_update()
	path3.set_property('data', t3.get_pathdata()); path3.request_update()
	en = time.time()
	print "time", en-st
    return True
gobject.idle_add(painter)

gtk.main()
