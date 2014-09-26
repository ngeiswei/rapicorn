# Copyright (C) 2010 Tim Janik
#
# Licensed CC0 Public Domain: http://creativecommons.org/publicdomain/zero/1.0
"""
Simple Python test for Rapicorn
"""

import sys
from Rapicorn1307 import Rapicorn # Rapicorn modules are versioned

# Define main window Widget Tree
simple_window_widgets = """
  <tmpl:define id="simple-window" inherit="Window">
    <Button on-click="CLICK">
      <Label markup-text="Hello Simple World!" />
    </Button>
  </tmpl:define>
"""

# setup application
app = Rapicorn.app_init ("Simple-Python-Test")  # unique application name
app.load_string (simple_window_widgets)         # loads widget tree
window = app.create_window ("simple-window")    # creates main window

# signal connection testing
def assert_unreachable (*args):
  assert "code unreachable" == True
cid1 = window.sig_commands.connect (assert_unreachable)
cid2 = window.sig_commands.connect (assert_unreachable)
assert cid1 != 0
assert cid2 != 0
assert cid1 != cid2
disconnected = window.sig_commands.disconnect (cid2)
assert disconnected == True
disconnected = window.sig_commands.disconnect (cid2)
assert disconnected == False
window.sig_commands -= cid1
disconnected = window.sig_commands.disconnect (cid1)
assert disconnected == False

# window command handling
seen_click_command = False
def command_handler (cmdname, args):
  global seen_click_command
  seen_click_command |= cmdname == "CLICK"
  ## print "in signal handler, args:", cmdname, args
  return True # handled
# need one signal connection to test for click
window.sig_commands += command_handler

# show window on screen
window.show()

# run synthesized tests
if not max (opt in sys.argv for opt in ('-i','--interactive')):
  testname = "  Simple-Window-Test:"
  print testname,
  # enter window to allow input events
  b = window.synthesize_enter()
  assert b
  # process pending events
  while app.iterate (False, True): pass
  # find button
  button = window.query_selector_unique ('.Button')
  assert button
  # click button
  assert seen_click_command == False
  window.synthesize_click (button, 1)
  window.synthesize_leave()
  while app.iterate (False, True): pass
  if 1:
    import time
    time.sleep (0.1) # FIXME: hack to ensure click is processed remotely
    while app.iterate (False, True): pass
  assert seen_click_command == True
  # delete window
  assert window.closed() == False
  window.synthesize_delete()
  while app.iterate (False, True): pass
  ## assert window.closed() == True
  # FIXME: window.closed() can't be asserted here, because remote
  # object references (from python) are not yet implemented
  print " " * max (0, 75 - len (testname)), "OK"

# event loop to process window events (exits when all windows are gone)
app.loop()
