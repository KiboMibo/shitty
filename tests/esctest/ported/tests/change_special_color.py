import esccmd
import escio
from esclog import LogInfo
from escutil import AssertTrue, knownBug

class ChangeSpecialColorTests(object):
  """OSC 5 reports the special-color index in the same form it accepts.

  Current xterm's ReportAnsiColorRequest emits the request opcode and subtracts
  NUM_ANSI_COLORS from the reported slot for opcode 5.  The original esctest
  expectation described an older, explicitly undocumented OSC 4 reply.
  Xlib's device-independent spaces are converted through the screen CCC;
  Zutty uses an explicit sRGB/D65 profile instead of an X11 screen profile.
  """

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_Multiple(self):
    """OSC 4 ; c1 ; spec1 ; s2 ; spec2 ; ST"""
    esccmd.ChangeSpecialColor("0",
                              "rgb:f0f0/f0f0/f0f0",
                              "1",
                              "rgb:f0f0/0000/0000")
    esccmd.ChangeSpecialColor("0", "?", "1", "?")
    AssertTrue(escio.ReadOSC("5") == ";0;rgb:f0f0/f0f0/f0f0")
    AssertTrue(escio.ReadOSC("5") == ";1;rgb:f0f0/0000/0000")

    esccmd.ChangeSpecialColor("0",
                              "rgb:8080/8080/8080",
                              "1",
                              "rgb:8080/0000/0000")
    esccmd.ChangeSpecialColor("0", "?", "1", "?")
    AssertTrue(escio.ReadOSC("5") == ";0;rgb:8080/8080/8080")
    s = escio.ReadOSC("5")
    LogInfo("Read: " + s)
    AssertTrue(s == ";1;rgb:8080/0000/0000")

  def doChangeSpecialColorTest(self, c, value, rgb):
    esccmd.ChangeSpecialColor(c, value)
    esccmd.ChangeSpecialColor(c, "?")
    s = escio.ReadOSC("5")
    AssertTrue(s == ";" + c + ";rgb:" + rgb)

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_RGB(self):
    self.doChangeSpecialColorTest("0", "rgb:f0f0/f0f0/f0f0", "f0f0/f0f0/f0f0")
    self.doChangeSpecialColorTest("0", "rgb:8080/8080/8080", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_Hash3(self):
    self.doChangeSpecialColorTest("0", "#fff", "f0f0/f0f0/f0f0")
    self.doChangeSpecialColorTest("0", "#888", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_Hash6(self):
    self.doChangeSpecialColorTest("0", "#f0f0f0", "f0f0/f0f0/f0f0")
    self.doChangeSpecialColorTest("0", "#808080", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_Hash9(self):
    self.doChangeSpecialColorTest("0", "#f00f00f00", "f0f0/f0f0/f0f0")
    self.doChangeSpecialColorTest("0", "#800800800", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_Hash12(self):
    self.doChangeSpecialColorTest("0", "#f000f000f000", "f0f0/f0f0/f0f0")
    self.doChangeSpecialColorTest("0", "#800080008000", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_RGBI(self):
    self.doChangeSpecialColorTest("0", "rgbi:1/1/1", "ffff/ffff/ffff")
    self.doChangeSpecialColorTest("0", "rgbi:0.5/0.5/0.5", "bcbc/bcbc/bcbc")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_CIEXYZ(self):
    self.doChangeSpecialColorTest("0", "CIEXYZ:1/1/1", "ffff/ffff/ffff")
    self.doChangeSpecialColorTest("0", "CIEXYZ:0.5/0.5/0.5", "cccc/b7b7/b4b4")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_CIEuvY(self):
    self.doChangeSpecialColorTest("0", "CIEuvY:1/1/1", "ffff/ffff/ffff")
    self.doChangeSpecialColorTest("0", "CIEuvY:0.5/0.5/0.5", "ffff/a2a2/acac")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_CIExyY(self):
    self.doChangeSpecialColorTest("0", "CIExyY:1/1/1", "ffff/ffff/ffff")
    self.doChangeSpecialColorTest("0", "CIExyY:0.5/0.5/0.5", "e8e8/b5b5/0000")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_CIELab(self):
    self.doChangeSpecialColorTest("0", "CIELab:1/1/1", "5e5e/5d5d/5d5d")
    self.doChangeSpecialColorTest("0", "CIELab:0.5/0.5/0.5", "4343/4242/4242")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_CIELuv(self):
    self.doChangeSpecialColorTest("0", "CIELuv:1/1/1", "0808/0303/0000")
    self.doChangeSpecialColorTest("0", "CIELuv:0.5/0.5/0.5", "0404/0101/0000")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeSpecialColor_TekHVC(self):
    self.doChangeSpecialColorTest("0", "TekHVC:1/1/1", "0b0b/0101/0303")
    self.doChangeSpecialColorTest("0", "TekHVC:0.5/0.5/0.5", "0606/0101/0101")
