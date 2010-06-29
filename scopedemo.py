#!/usr/bin/python
from scope2 import *
import cairo
import gobject
import gtk
import itertools
import sys
import time
import weakref

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
    def __init__(self, data, hscale=1.0, vscale=1.0, voff=0.0, color=(1,1,1)):
	self.data = list(data)
	self.hscale = hscale
	self.vscale = vscale
	self.voff = voff
	self.color = color
	self.selected = False
	self.cache = None

    def set_data(self, data):
	self.data = list(data)
	self.kill_cache()

    def set_hscale(self, hscale):
	self.hscale = hscale
	self.kill_cache()

    def extend(self, newdata):
	self.data.extend(newdata)
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

    def draw(self, canvas, xo, width, height):
        self.update_cache(width)

        canvas.save()

        scale = height / self.vscale / 10.
        voff = -self.voff*height/10.
        def Y(y): return height-y*scale+voff
        canvas.set_line_width(1)
        try:
            if self.sparse: return self.draw_sparse(canvas, xo, width, Y)
            else: return self.draw_dense(canvas, xo, width, Y)
        finally:
            canvas.restore()
       

    def draw_dense(self, canvas, xo, width, Y):
        cache = self.cache
        if xo > 0: r = range(width)
        else: r = range(-xo, width)

        self.set_color(canvas, .80)
        for row in r:
            crow = row + xo
            if crow >= len(cache): break
            data = cache[crow]
            canvas.move_to(row, Y(data[1]))
            canvas.line_to(row, Y(data[2]))
        canvas.stroke()

        self.set_color(canvas, .80)
        for row in r:
            crow = row + xo
            if crow >= len(cache): break
            data = cache[crow]
            if row == r[0]:
                canvas.move_to(row, Y(data[0]))
            else:
                canvas.line_to(row, Y(data[0]))
        canvas.stroke()

    def draw_sparse(self, canvas, xo, width, Y):
        cache = self.cache
        first = True

        self.set_color(canvas, .80)
        for x, y in cache:
            if x < xo: continue
            if x > xo+width: break

            if first:
                first = False
                canvas.move_to(x, Y(y))
            else:
                canvas.line_to(x, Y(y))
        canvas.stroke()

    def set_color(self, canvas, alpha):
        r, g, b = self.color
        canvas.set_source_rgba(r*alpha, g*alpha, b*alpha, alpha)



class Ddt(Trace):
    def __init__(self, base, *args):
	Trace.__init__(self, [], *args)
	self.base = base

    def update(self):
	if self.base.data and not self.data:
	    self.data = [0]
	    self.last = self.base.data[0]
	append = self.data.append
	newdata = self.base.data[len(self.data):]
	if not newdata: return

	last = self.last
	print "update", len(newdata)
	for d in newdata:
	    append((last-d)/fperiod)
	    last = d
	self.last = last
	self.kill_cache()

cap = Capture()
t1 = Trace([], .01, 1., 1., (1,0,0)); cap.add_pin(t1, 'stepgen.0.step')
t2 = Trace([], .01, 1., 4., (0,1,0)); cap.add_pin(t2, 'siggen.0.sine')
t3 = Trace([], .01, 1., 4., (0,0,1)); cap.add_pin(t3, 'siggen.0.cosine')
t4 = Ddt(t2, .01, 10., 6., (0,1,1))
traces = [t1, t2, t3, t4]
cap.attach_thread("thread1")
cap.start_capture()
period = cap.get_thread_period()
fperiod = period*1.e-9
rfperiod = 1./fperiod

def draw_reticle(canvas, width, height):
    d = .01 * min(width, height)
    canvas.set_line_width(2)
    for row in range(11):
        for col in range(11):
            x = width * col / 10.
            y = height * row / 10.
            if row == 5 or col == 5:
                canvas.set_source_rgba(.8, .8, .8)
            else:
                canvas.set_source_rgba(.3, .3, .3)
            canvas.move_to(x-d, y)
            canvas.line_to(x+d, y)
            canvas.move_to(x, y-d)
            canvas.line_to(x, y+d)
            canvas.stroke()

    canvas.set_source_rgba(.1, .1, .1)
    d = d / 3.
    if height > 500:
        for col in range(11):
            for row in range(101):
                x = width * col / 10.
                y = height * row / 100.
                canvas.move_to(x-d, y)
                canvas.line_to(x+d, y)

    canvas.set_line_width(1)
    if width > 500:
        for col in range(101):
            for row in range(11):
                x = width * col / 100.
                y = height * row / 10.
                canvas.move_to(x, y-d)
                canvas.line_to(x, y+d)
    canvas.stroke()

def draw_traces_to_canvas(canvas, traces, width, height):
    canvas.set_source_rgb(0,0,0)
    canvas.set_operator(cairo.OPERATOR_SOURCE)
    canvas.paint()

    canvas.set_operator(cairo.OPERATOR_ADD)
    draw_reticle(canvas, width, height)
    for t in traces:
        t.draw(canvas, 0, width, height)

class Screen(gtk.DrawingArea):

    # Draw in response to an expose-event
    __gsignals__ = { "expose-event": "override" }

    # Handle the expose-event by drawing
    def do_expose_event(self, event):

        # Create the cairo context
        cr = self.window.cairo_create()

        # Restrict Cairo to the exposed area; avoid extra work
        cr.rectangle(event.area.x, event.area.y,
                event.area.width, event.area.height)
        cr.clip()

        self.draw(cr, *self.window.get_size())

    def draw(self, cr, width, height):
        draw_traces_to_canvas(cr, traces, width, height)

w = gtk.Window()
w.connect("destroy", gtk.main_quit)
screen = Screen()
screen.set_size_request(640,480)
w.add(screen)
w.show_all()

def painter():
    st = time.time()
    if cap.poll():
	t4.update()
	t1.expire_samples(3200)
	t2.expire_samples(3200)
	t3.expire_samples(3200)
	t4.expire_samples(3200)
	screen.queue_draw()
    en = time.time()
    print "time", en-st
    return True
gobject.timeout_add(50, painter)

gtk.main()

# vim:sw=4:sts=4:smarttab:smartindent:noet:
