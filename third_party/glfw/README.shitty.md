# Shitty GLFW fork

This directory is a source snapshot of the upstream GLFW `master` branch.
The exact base revision is recorded in `UPSTREAM`.

Shitty builds GLFW with the same backend selection as the IX package:
`GLFW_BUILD_WAYLAND=ON` and `GLFW_BUILD_X11=OFF`. The application creates a
`GLFW_NO_API` window and uses GLFW only for its Wayland window/input backend
and Vulkan surface support.

The only local GLFW source change is the compositor-reported `GLFW_TILED`
window attribute, kept as a small standalone diff for submission upstream.
