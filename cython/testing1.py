# Licensed CC0 Public Domain: http://creativecommons.org/publicdomain/zero/1.0
import Rapicorn, collections
# python -ic "import sys, os; sys.path.insert (0, os.path.abspath ('.libs')) ; import Rapicorn"

# verify that pycallable() raises Exception
def assert_raises (Exception, pycallable, *args, **kwds):
  try:    pycallable (*args, **kwds)
  except Exception: return
  raise AssertionError('%s not raised' % Exception.__name__)

# test MainLoop and EventLoop object identities
ml = Rapicorn.MainLoop()
sl = ml.create_slave()
sm = sl.main_loop()
m3 = Rapicorn.MainLoop()
assert ml == ml and sl == sl and sm == sm and m3 == m3
assert ml != sl and sl != ml and sm != sl and sl != sm
assert ml == sm and sm == ml and ml != m3 and m3 != ml
assert ml.main_loop() == ml  and m3.main_loop() == m3

# test exec_callback
ect_counter = 5
ect_value = ""
def bhandler():
  global ect_counter, ect_value
  if ect_counter:
    ect_value += "*"
    ect_counter -= 1
  return ect_counter
ml = Rapicorn.MainLoop()
ml.exec_callback (bhandler)
ml.iterate_pending()
assert ect_counter == 0
assert ect_value == "*****"

# test exec_dispatcher
prepared   = [ None, None, None ]
checked    = [ None, None, None ]
dispatched = [ None, None, None ]
counter    = 0
ml = Rapicorn.MainLoop()
def dispatcher (loop_state):
  global counter, prepared, checked, dispatched
  if   loop_state.phase == loop_state.PREPARE:
    prepared[counter] = True
    return True
  elif loop_state.phase == loop_state.CHECK:
    checked[counter] = "Yes"
    return True
  elif loop_state.phase == loop_state.DISPATCH:
    dispatched[counter] = counter
    counter += 1
    return counter <= 2
ml.exec_dispatcher (dispatcher)
ml.iterate_pending()
summary = [ counter ] + prepared + checked + dispatched
assert summary == [3, True, True, True, 'Yes', 'Yes', 'Yes', 0, 1, 2]

# Enum Tests
assert Rapicorn.FocusDirType
assert Rapicorn.FocusDirType.FOCUS_UP and Rapicorn.FOCUS_UP
assert Rapicorn.FocusDirType.FOCUS_DOWN and Rapicorn.FOCUS_DOWN
assert Rapicorn.FOCUS_UP.name == 'FOCUS_UP'
assert Rapicorn.FOCUS_DOWN.name == 'FOCUS_DOWN'
assert Rapicorn.FocusDirType[Rapicorn.FOCUS_UP.name] == Rapicorn.FOCUS_UP
assert Rapicorn.FocusDirType[Rapicorn.FOCUS_DOWN.name] == Rapicorn.FOCUS_DOWN
assert Rapicorn.FOCUS_UP < Rapicorn.FOCUS_DOWN and Rapicorn.FOCUS_DOWN > Rapicorn.FOCUS_UP
assert Rapicorn.FOCUS_UP != Rapicorn.FOCUS_DOWN
assert Rapicorn.FOCUS_UP.value > 0 and Rapicorn.FOCUS_DOWN.value > 0
assert Rapicorn.FOCUS_UP.value + Rapicorn.FOCUS_DOWN.value > 0

# List Base Tests
s = Rapicorn.BoolSeq ([ True, False, "A", [], 88, None ])
s = Rapicorn.StringSeq ([ '1', 'B' ])
# assert_raises (TypeError, Rapicorn.StringSeq, [ None ])

# Record Tests
# Pixbuf
p = Rapicorn.Pixbuf()
p.row_length = 2
p.pixels = [ 0x00000000, 0xff000000 ]
p.variables = [ 'meta=foo' ]
assert p.row_length == 2
assert p.pixels == [ 0x00000000, -16777216 ]
assert p.variables == [ 'meta=foo' ]
# UpdateSpan
u = Rapicorn.UpdateSpan()
u.start = 7
u.length = 20
assert u.start == 7 and u.length == 20
assert u._asdict() == collections.OrderedDict ([ ('start', 7), ('length', 20) ])
def invalid_assignment (u): u.no_such_dummy = 999
assert_raises (AttributeError, invalid_assignment, u)
# UpdateRequest
r = Rapicorn.UpdateRequest()
r.kind = Rapicorn.UPDATE_CHANGE
r.rowspan = u
r.colspan = u
r.variables = p.variables
assert r.kind == Rapicorn.UPDATE_CHANGE
assert r.colspan == u
assert r.rowspan == r.colspan
assert r.variables == p.variables
# FIXME: r._asdict needs Any
# Requisition
q = Rapicorn.Requisition (width = 7, height = 8)
assert q.width == 7 and q.height == 8
assert list (q) == [ 7, 8 ]
assert q._asdict() == collections.OrderedDict ([ ('width', 7), ('height', 8) ])
assert q[0] == 7 and q[1] == 8
def indexed_access (o, i): return o[i]
assert_raises (IndexError, indexed_access, q, 2)
def R (w, h):
  return Rapicorn.Requisition (width = w, height = h)
a = R (17, 33) ; b = R (17, 33) ; assert a == b and a <= b and a >= b and not (a != b) and not (a >  b) and not (a <  b)
a = R (22, 11) ; b = R (22, 11) ; assert a == b and a <= b and a >= b and not (a != b) and not (a >  b) and not (a <  b)
a = R (39, 99) ; b = [ 39, 99 ] ; assert a == b and a <= b and a >= b and not (a != b) and not (a >  b) and not (a <  b)
a = [ 44, 40 ] ; b = R (44, 40) ; assert a == b and a <= b and a >= b and not (a != b) and not (a >  b) and not (a <  b)
a = R (11, 22) ; b = R (11, 21) ; assert a != b and a >  b and a >= b and not (a == b) and not (a <= b) and not (a <  b)
a = R (11, 22) ; b = R (11, 23) ; assert a != b and a <= b and a <  b and not (a == b) and not (a >  b) and not (a >= b)
a = R (11, 22) ; b = [ 11 ]     ; assert a != b and a >= b and a >  b and not (a == b) and not (a <  b) and not (a <= b)
a = R (1, 2)  ; b = [ 1, 2, 3 ] ; assert a != b and a <= b and a <  b and not (a == b) and not (a >  b) and not (a >= b)
a = R (1, 2)  ; b = [ 1 ]       ; assert a != b and a >= b and a >  b and not (a == b) and not (a <  b) and not (a <= b)
a = R (1, 99) ; b = [ 1, 1, 1 ] ; assert a != b and a >= b and a >  b and not (a == b) and not (a <  b) and not (a <= b)
a = R (1, 0)  ; b = [ 1, 1, 1 ] ; assert a != b and a <= b and a <  b and not (a == b) and not (a >  b) and not (a >= b)

# Application
assert_raises (TypeError, Rapicorn.Object)
assert_raises (TypeError, Rapicorn.Widget)
assert_raises (TypeError, Rapicorn.Container)
assert_raises (TypeError, Rapicorn.Window)
assert_raises (TypeError, Rapicorn.Application)

app = Rapicorn.init ('testing1.py')
w = app.create_window ('Window', '')
l = w.create_widget ('Label', ['markup-text=Hello Cython World!'])
seen_window_display = False
def displayed():
  global seen_window_display
  print "Hello, window is being displayed"
  seen_window_display = True
  app.quit()
w.sig_displayed.connect (displayed)
w.show()
app.main_loop().exec_timer (app.quit, 2500) # ensures test doesn't hang

wl = app.query_windows ("")
assert wl == []

app.run()
assert seen_window_display

# all done
print '  %-6s' % 'CHECK', '%-67s' % __file__, 'OK'
