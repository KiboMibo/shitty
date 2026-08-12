#!/usr/bin/env python3

CASES = (
    "controls",
    "escape_invalid",
    "escape_nf",
    "escape_fpes",
    "csi",
    "csi_parameters",
    "csi_clear",
    "csi_max",
    "csi_misc",
    "dcs",
    "dcs_misc",
    "osc_lengths",
    "osc_oversize",
    *(f"osc_controls_{c1}_{introducer}_{terminator}"
      for introducer in ("default", "c0", "c1")
      for terminator in ("default", "c0", "c1", "bel")
      for c1 in (0, 1)),
)


def case_names():
    return tuple(CASES)
