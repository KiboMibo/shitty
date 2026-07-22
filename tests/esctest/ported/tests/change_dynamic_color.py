import esccmd
import escio
from escutil import AssertEQ, knownBug

# Xlib converts device-independent color specifications through the target
# screen's CCC. Zutty's Wayland profile is explicitly sRGB/D65.

class ChangeDynamicColorTests(object):
  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_Multiple(self):
    """OSC 4 ; c1 ; spec1 ; s2 ; spec2 ; ST"""
    esccmd.ChangeDynamicColor("10",
                              "rgb:f0f0/f0f0/f0f0",
                              "rgb:f0f0/0000/0000")
    esccmd.ChangeDynamicColor("10", "?", "?")
    AssertEQ(escio.ReadOSC("10"), ";rgb:f0f0/f0f0/f0f0")
    AssertEQ(escio.ReadOSC("11"), ";rgb:f0f0/0000/0000")

    esccmd.ChangeDynamicColor("10",
                              "rgb:8080/8080/8080",
                              "rgb:8080/0000/0000")
    esccmd.ChangeDynamicColor("10", "?", "?")
    AssertEQ(escio.ReadOSC("10"), ";rgb:8080/8080/8080")
    AssertEQ(escio.ReadOSC("11"), ";rgb:8080/0000/0000")

  def doChangeDynamicColorTest(self, c, value, rgb):
    esccmd.ChangeDynamicColor(c, value)
    esccmd.ChangeDynamicColor(c, "?")
    s = escio.ReadOSC(c)
    AssertEQ(s, ";rgb:" + rgb)

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_RGB(self):
    self.doChangeDynamicColorTest("10", "rgb:f0f0/f0f0/f0f0", "f0f0/f0f0/f0f0")
    self.doChangeDynamicColorTest("10", "rgb:8080/8080/8080", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_Hash3(self):
    self.doChangeDynamicColorTest("10", "#fff", "f0f0/f0f0/f0f0")
    self.doChangeDynamicColorTest("10", "#888", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_Hash6(self):
    self.doChangeDynamicColorTest("10", "#f0f0f0", "f0f0/f0f0/f0f0")
    self.doChangeDynamicColorTest("10", "#808080", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_Hash9(self):
    self.doChangeDynamicColorTest("10", "#f00f00f00", "f0f0/f0f0/f0f0")
    self.doChangeDynamicColorTest("10", "#800800800", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_Hash12(self):
    self.doChangeDynamicColorTest("10", "#f000f000f000", "f0f0/f0f0/f0f0")
    self.doChangeDynamicColorTest("10", "#800080008000", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_RGBI(self):
    self.doChangeDynamicColorTest("10", "rgbi:1/1/1", "ffff/ffff/ffff")
    self.doChangeDynamicColorTest("10", "rgbi:0.5/0.5/0.5", "bcbc/bcbc/bcbc")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_CIEXYZ(self):
    self.doChangeDynamicColorTest("10", "CIEXYZ:1/1/1", "ffff/ffff/ffff")
    self.doChangeDynamicColorTest("10", "CIEXYZ:0.5/0.5/0.5", "cccc/b7b7/b4b4")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_CIEuvY(self):
    self.doChangeDynamicColorTest("10", "CIEuvY:1/1/1", "ffff/ffff/ffff")
    self.doChangeDynamicColorTest("10", "CIEuvY:0.5/0.5/0.5", "ffff/a2a2/acac")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_CIExyY(self):
    self.doChangeDynamicColorTest("10", "CIExyY:1/1/1", "ffff/ffff/ffff")
    self.doChangeDynamicColorTest("10", "CIExyY:0.5/0.5/0.5", "e8e8/b5b5/0000")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_CIELab(self):
    self.doChangeDynamicColorTest("10", "CIELab:1/1/1", "5e5e/5d5d/5d5d")
    self.doChangeDynamicColorTest("10", "CIELab:0.5/0.5/0.5", "4343/4242/4242")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_CIELuv(self):
    self.doChangeDynamicColorTest("10", "CIELuv:1/1/1", "0808/0303/0000")
    self.doChangeDynamicColorTest("10", "CIELuv:0.5/0.5/0.5", "0404/0101/0000")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeDynamicColor_TekHVC(self):
    self.doChangeDynamicColorTest("10", "TekHVC:1/1/1", "0b0b/0101/0303")
    self.doChangeDynamicColorTest("10", "TekHVC:0.5/0.5/0.5", "0606/0101/0101")

