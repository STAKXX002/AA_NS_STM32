"""Fast checks: boot, command parser, light, un-homed guards."""


def test_boot_banner(sim):
  assert 'READY' in sim.transcript
  assert 'HOME REQUIRED' in sim.transcript


def test_unknown_command(sim):
  sim.uart('FOO')
  sim.expect('UNKNOWN')


def test_bad_arguments(sim):
  sim.uart('HOME')
  sim.expect('INVALID')
  sim.uart('HOME 9')
  sim.expect('INVALID')
  sim.uart('MOVE 1 abc')
  sim.expect('INVALID')


def test_light_on_off(sim):
  sim.uart('ON')
  sim.expect('LIGHT ON')
  sim.uart('OFF')
  sim.expect('LIGHT OFF')


def test_move_requires_home(sim):
  sim.uart('MOVE 1 5')
  sim.expect('NO HOME')


def test_pos_reports_all_axes(sim):
  sim.uart('POS')
  sim.expect('A4 UNHOMED')
