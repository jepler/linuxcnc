import gtk

pins = ['siggen.0.amplitude', 'siggen.0.cosine', 'siggen.0.frequency', 'siggen.0.offset', 'siggen.0.sawtooth', 'siggen.0.sine', 'siggen.0.square', 'siggen.0.triangle', 'stepgen.0.counts', 'stepgen.0.enable', 'stepgen.0.phase-A', 'stepgen.0.phase-B', 'stepgen.0.position-fb', 'stepgen.0.velocity-cmd']

pinmod = gtk.ListStore(str)
for p in pins: pinmod.append([p])
em = gtk.EntryCompletion()
em.set_model(pinmod)
em.set_inline_completion(1)
em.set_inline_selection(1)
em.set_text_column(0)
em.set_minimum_key_length(0)

w = gtk.Window()
w.connect("destroy", gtk.main_quit)
v = gtk.HBox(w); w.add(v)
v.set_homogeneous(0)
c = gtk.ComboBoxEntry(); v.add(c)
c.child.set_completion(em)
c.set_model(pinmod)
c.set_text_column(0)
o = gtk.Button("OK"); v.add(o)
o.child.set_width_chars(3)
w.show_all()

def manipulate_ok(*args):
   o.set_sensitive(c.child.get_text() in pins)
manipulate_ok()

c.connect('changed', manipulate_ok)
gtk.main()
