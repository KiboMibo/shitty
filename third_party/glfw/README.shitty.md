# Shitty GLFW fork

This directory is a source snapshot of the upstream GLFW `master` branch.
The exact base revision is recorded in `UPSTREAM`.

Shitty builds GLFW with the same backend selection as the IX package:
`GLFW_BUILD_WAYLAND=ON` and `GLFW_BUILD_X11=OFF`. The application creates a
`GLFW_NO_API` window and uses GLFW only for its Wayland window/input backend
and Vulkan surface support.

Local GLFW source changes should be kept as small standalone commits based on
the revision in `UPSTREAM`, so they can be submitted upstream.
