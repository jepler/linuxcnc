#!/usr/bin/python
from scope2 import *
import cairo
import gobject
import gtk
import itertools
import sys
import time
import weakref
import array

class Capture:
    def __init__(self):
	self.s = Scope()
	self.channels = {}

    def attach_thread(self, thread):
	self.s.attach_thread(thread)

    def start_capture(self): self.s.start_capture()
    def stop_capture(self): self.s.stop_capture()
    def capture_state(self): return self.s.capture_state()

    def get_thread_period(self, *opt_name):
	return self.s.get_thread_period(*opt_name)

    def get_available_channel(self):
	for c in range(NCHANNELS):
	    if c not in self.channels: return c
	raise ValueError, "all channels in use"	    

    def use_channel(self, trace, channel):
	self.channels[channel] = weakref.proxy(trace, lambda unused: self.free_channel(channel))
	
    def free_channel(self, channel):
	print "free_channel", channel
	del self.channels[channel]
	self.s.channel_off(channel)

    def add_pin(self, trace, pin):
	channel = self.get_available_channel()
	r = self.s.set_channel_pin(channel, pin)
	trace.set_data(r)
	self.use_channel(trace, channel)

    def add_param(self, trace, param):
	channel = self.get_available_channel()
	self.s.set_channel_param(channel, pin)
	trace.set_data(r)
	self.use_channel(trace, channel)

    def add_sig(self, trace, sig):
	channel = self.get_available_channel()
	self.s.set_channel_sig(channel, pin)
	trace.set_data(r)
	self.use_channel(trace, channel)

    def poll(self):
	assert self.capture_state(), "must be capturing to poll"
	new_samples, overruns = self.s.get_samples()
#	print "poll()", new_samples, overruns
	return new_samples

    def check_overflow(self):
	return self.s.check_overflow()

class Trace:
    def __init__(self, hscale=1.0, vscale=1.0, voff=0.0, color=(1,1,1)):
	self.data = []
	self.hscale = hscale
	self.vscale = vscale
	self.voff = voff
	self.color = color
	self.selected = False
	self.cache = None

    def set_data(self, data):
	self.data = data

    def update(self):
	self.kill_cache()

    def set_hscale(self, hscale):
	self.hscale = hscale
	self.kill_cache()

    def expire_samples(self, max_samples):
	if len(self.data) > max_samples:
	    del self.data[:-max_samples]
	    self.kill_cache()

    def update_cache(self, width):
	if self.cache and width == self.cache_width:
	    return self.cache
	self.cache_width = width
	samples_per_pixel = 10. / self.hscale / width

	if samples_per_pixel > 1:
	    self.sparse = False
	    self.update_cache_dense(samples_per_pixel)
	else:
	    self.sparse = True
	    self.cache = [(i/samples_per_pixel, v)
			    for i, v in enumerate(self.data)]

    def update_cache_dense(self, samples_per_pixel):
	cache = []

	def stats(s):
	    s.sort()
	    return s[len(s)/2], s[0], s[-1] 

	def _putcache(i, p):
	    while len(self.cache) <= i:
		self.cache.append([])
	    self.cache[i].append(p)

	for i, v in enumerate(self.data):
	    ci = int(i/samples_per_pixel)
	    while len(cache) <= ci:
		cache.append([])
	    cache[ci].append(v)

	self.cache = [stats(s) for s in cache]

    def kill_cache(self):
	self.cache = None

    def draw(self, drw, xo, width, height):
	scale = height / self.vscale / 10.
	voff = -self.voff*height/10.

	gc = drw.new_gc()
	gc.set_rgb_fg_color(gtk.gdk.Color('#ccc'))
	drw.draw_line(gc, 0, int(round(height+voff)), width, int(round(height+voff)))

	samples_per_pixel = 10. / self.hscale / width

#	print samples_per_pixel
	gc.set_rgb_fg_color(gtk.gdk.Color(*[int(round(c*65535)) for c in self.color]))
	draw_trace(drw, gc, self.data, xo, samples_per_pixel, width, height, scale, voff, *self.color)
       
class Ddt(Trace):
    def __init__(self, base, *args):
	Trace.__init__(self, *args)
	self.data = array.array('d')
	self.base = base

    def update(self):
	if self.base.data and not self.data:
	    self.data.append(0)
	    self.last = self.base.data[0]
	append = self.data.append
	newdata = self.base.data[len(self.data):]
	if not newdata: return

	last = self.last
	for d in newdata:
	    append((last-d)/fperiod)
	    last = d
	self.last = last
	self.kill_cache()

cap = Capture()
#print cap.s.list_pins()
t1 = Trace(.002, 1., 1., (1,0,0)); cap.add_pin(t1, 'stepgen.0.phase-A')
t2 = Trace(.002, 1., 4., (0,1,0)); cap.add_pin(t2, 'siggen.0.sine')
t3 = Trace(.002, 1., 4., (0,0,1)); cap.add_pin(t3, 'siggen.0.cosine')
t4 = Ddt(t2, .002, 10., 6., (0,1,1))
traces = [t1, t2, t3, t4]
cap.attach_thread("thread1")
cap.start_capture()
period = cap.get_thread_period()
fperiod = period*1.e-9
rfperiod = 1./fperiod

def draw_reticle(drw, width, height):
    d = int(round(.01 * min(width, height)))
    gc_bright = drw.new_gc()
    gc_bright.set_rgb_fg_color(gtk.gdk.Color('#ccc'))
    gc_dim = drw.new_gc()
    gc_dim.set_rgb_fg_color(gtk.gdk.Color('#222'))
    #gc.set_line_attributes(2, gtk.gdk.LINE_SOLID, gtk.gdk.CAP_BUTT, gtk.gdk.JOIN_MITER);
    for row in range(11):
	for col in range(11):
	    x = int(round(width * col / 10.))
	    y = int(round(height * row / 10.))
	    if row == 5 or col == 5:
		gc = gc_bright
	    else:
		gc = gc_dim
	    drw.draw_line(gc, x-d, y, x+d, y)
	    drw.draw_line(gc, x, y-d, x, y+d)

    if height > 200:
	for col in range(11):
	    for row in range(101):
		x = int(round(round(width * col / 10.)))
		y = int(round(round(height * row / 100.)))
		drw.draw_point(gc_dim, x, y)

    if width > 200:
	for col in range(101):
	    for row in range(11):
		x = int(round(width * col / 100.))
		y = int(round(height * row / 10.))
		drw.draw_point(gc_dim, x, y)

def draw_traces_to_drawable(drw, traces, width, height):
    draw_reticle(drw, width, height)
    for t in traces:
	t.draw(drw, 0, width, height)

class Screen(gtk.DrawingArea):

    # Draw in response to an expose-event
    __gsignals__ = { "expose-event": "override" }

    # Handle the expose-event by drawing
    def do_expose_event(self, event):
	d = self.get_window()
	self.draw(d, *self.window.get_size())

    def draw(self, d, width, height):
	draw_traces_to_drawable(d, traces, width, height)

w = gtk.Window()
w.connect("destroy", gtk.main_quit)
screen = Screen()
screen.set_size_request(640,480)
screen.modify_bg(state=0, color=gtk.gdk.Color('#000'))
screen.set_double_buffered(1)
w.add(screen)
w.show_all()

def painter():
    if cap.poll():
	for t in traces: t.update()
	for t in traces: t.expire_samples(5000)
#	for t in traces: print t.data[-1],
#	print
	screen.queue_draw()
    return True
gobject.timeout_add(50, painter)
#gobject.idle_add(painter)

gtk.main()

# vim:sw=4:sts=4:smarttab:smartindent:noet:
