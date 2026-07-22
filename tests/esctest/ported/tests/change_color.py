import esccmd
import escio
from escutil import AssertEQ, knownBug

# Xlib converts device-independent color specifications through the target
# screen's CCC. Shitty has no X11 screen profile and uses an explicit sRGB/D65
# CCC, so these expected RGB values intentionally differ from esctest's old
# X-server-specific captures.

class ChangeColorTests(object):
  """The color numbers correspond to the ANSI colors 0-7, their bright versions
  8-15, and if supported, the remainder of the 88-color or 256-color table."""

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_Multiple(self):
    """OSC 4 ; c1 ; spec1 ; s2 ; spec2 ; ST"""
    esccmd.ChangeColor("0",
                       "rgb:f0f0/f0f0/f0f0",
                       "1",
                       "rgb:f0f0/0000/0000")
    esccmd.ChangeColor("0", "?", "1", "?")
    AssertEQ(escio.ReadOSC("4"), ";0;rgb:f0f0/f0f0/f0f0")
    AssertEQ(escio.ReadOSC("4"), ";1;rgb:f0f0/0000/0000")

    esccmd.ChangeColor("0",
                       "rgb:8080/8080/8080",
                       "1",
                       "rgb:8080/0000/0000")
    esccmd.ChangeColor("0", "?", "1", "?")
    AssertEQ(escio.ReadOSC("4"), ";0;rgb:8080/8080/8080")
    AssertEQ(escio.ReadOSC("4"), ";1;rgb:8080/0000/0000")

  def doChangeColorTest(self, c, value, rgb):
    esccmd.ChangeColor(c, value)
    esccmd.ChangeColor(c, "?")
    s = escio.ReadOSC("4")
    AssertEQ(s, ";" + c + ";rgb:" + rgb)

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_RGB(self):
    self.doChangeColorTest("0", "rgb:f0f0/f0f0/f0f0", "f0f0/f0f0/f0f0")
    self.doChangeColorTest("0", "rgb:8080/8080/8080", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_Hash3(self):
    self.doChangeColorTest("0", "#fff", "f0f0/f0f0/f0f0")
    self.doChangeColorTest("0", "#888", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_Hash6(self):
    self.doChangeColorTest("0", "#f0f0f0", "f0f0/f0f0/f0f0")
    self.doChangeColorTest("0", "#808080", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_Hash9(self):
    self.doChangeColorTest("0", "#f00f00f00", "f0f0/f0f0/f0f0")
    self.doChangeColorTest("0", "#800800800", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_Hash12(self):
    self.doChangeColorTest("0", "#f000f000f000", "f0f0/f0f0/f0f0")
    self.doChangeColorTest("0", "#800080008000", "8080/8080/8080")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_RGBI(self):
    self.doChangeColorTest("0", "rgbi:1/1/1", "ffff/ffff/ffff")
    self.doChangeColorTest("0", "rgbi:0.5/0.5/0.5", "bcbc/bcbc/bcbc")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_CIEXYZ(self):
    self.doChangeColorTest("0", "CIEXYZ:1/1/1", "ffff/ffff/ffff")
    self.doChangeColorTest("0", "CIEXYZ:0.5/0.5/0.5", "cccc/b7b7/b4b4")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_CIEuvY(self):
    self.doChangeColorTest("0", "CIEuvY:1/1/1", "ffff/ffff/ffff")
    self.doChangeColorTest("0", "CIEuvY:0.5/0.5/0.5", "ffff/a2a2/acac")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_CIExyY(self):
    self.doChangeColorTest("0", "CIExyY:1/1/1", "ffff/ffff/ffff")
    self.doChangeColorTest("0", "CIExyY:0.5/0.5/0.5", "e8e8/b5b5/0000")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_CIELab(self):
    self.doChangeColorTest("0", "CIELab:1/1/1", "5e5e/5d5d/5d5d")
    self.doChangeColorTest("0", "CIELab:0.5/0.5/0.5", "4343/4242/4242")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_CIELuv(self):
    self.doChangeColorTest("0", "CIELuv:1/1/1", "0808/0303/0000")
    self.doChangeColorTest("0", "CIELuv:0.5/0.5/0.5", "0404/0101/0000")

  @knownBug(terminal="iTerm2", reason="Color reporting not implemented.", shouldTry=False)
  def test_ChangeColor_TekHVC(self):
    self.doChangeColorTest("0", "TekHVC:1/1/1", "0b0b/0101/0303")
    self.doChangeColorTest("0", "TekHVC:0.5/0.5/0.5", "0606/0101/0101")

