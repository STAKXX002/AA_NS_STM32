"""Homing and moves through the real firmware in Renode. The precise logic
(ranges, CAL maths, fault stops) is covered much faster by host_tests/;
these check that the build, pins and timer wiring work in the simulator."""
import pytest

from harness import T_MOVE


@pytest.mark.slow
def test_home_then_move(homed):
  homed.uart('MOVE 1 2')
  homed.expect('A1 DONE 2.000', timeout=T_MOVE)


@pytest.mark.slow
def test_range_checked(homed):
  homed.uart('MOVE 1 999')
  homed.expect('RANGE')


@pytest.mark.slow
def test_unexpected_max_is_a_fault(homed):
  homed.uart('MOVE 1 10')
  homed.expect('A1 MOVE 10.000')
  homed.press((1, 'max'))
  homed.expect('FAULT: A1 MAX HIT', timeout=T_MOVE)
