import esc
import escargs
import esccmd
import escio
from escutil import AssertEQ, AssertGE

class DA2Tests(object):
  def handleDA2Response(self):
    params = escio.ReadCSI('c', expected_prefix='>')
    if getattr(escargs.args, "annotation_terminal", None) == "shitty":
      # DA2 is terminal type, firmware version, and cartridge registration.
      # https://vt100.net/docs/vt510-rm/DA2.html
      AssertEQ(params, [41, 14, 0])
    elif escargs.args.expected_terminal == "xterm":
      if esc.vtLevel == 5:
        AssertEQ(params[0], 64)
      elif esc.vtLevel == 4:
        AssertEQ(params[0], 41)
      elif esc.vtLevel == 3:
        AssertEQ(params[0], 24)
      elif esc.vtLevel == 2:
        AssertEQ(params[0], 1)
      elif esc.vtLevel == 2:
        AssertEQ(params[0], 0)
      AssertGE(999, params[1])
      AssertGE(params[1], 314)
      AssertEQ(len(params), 3)
    elif escargs.args.expected_terminal == "iTerm2":
      AssertEQ(params[0], 1)
      AssertEQ(params[1], 314)
      AssertEQ(len(params), 3)

  def test_DA2_NoParameter(self):
    esccmd.DA2()
    self.handleDA2Response()

  def test_DA2_0(self):
    esccmd.DA2(0)
    self.handleDA2Response()



