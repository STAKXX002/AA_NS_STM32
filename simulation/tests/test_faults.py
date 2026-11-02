"""Fault policy: while faulted only RST, OFF and STOP are accepted."""


def test_min_active_latches_fault(sim):
  sim.press((1, 'min'))
  sim.drain(0.3)
  sim.uart('HOME 1')
  sim.expect('FAULT: A1 MIN ACTIVE')


def test_on_rejected_while_faulted(faulted):
  faulted.uart('ON')
  faulted.expect('FAULT - RST REQUIRED')


def test_off_allowed_while_faulted(faulted):
  faulted.uart('OFF')
  faulted.expect('LIGHT OFF')


def test_stop_allowed_while_faulted(faulted):
  faulted.uart('STOP')
  faulted.expect('HATCH STOPPED')


def test_motion_and_hatch_blocked_while_faulted(faulted):
  faulted.uart('HOME 2')
  faulted.expect('FAULT - RST REQUIRED')
  faulted.uart('OPEN')
  faulted.expect('FAULT - RST REQUIRED')
  faulted.uart('MOVE 1 1')
  faulted.expect('FAULT - RST REQUIRED')


def test_rst_clears_fault(faulted):
  faulted.uart('RST')
  faulted.expect('HOME REQUIRED')
  faulted.uart('ON')
  faulted.expect('LIGHT ON')
