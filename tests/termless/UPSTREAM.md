# Termless

Source: local checkout of `https://github.com/termless-dev/termless`

Revision: `aaa0bbf712297318f11539dfc1bea54d3a94dd79`

License: MIT; the verbatim upstream license is in `upstream/LICENSE`.

The relevant upstream TypeScript tests and public backend types are preserved
verbatim below `upstream/`. `backend.py` maps their observable model onto
Zutty's local control socket, and `adapter.py` runs one logical upstream case
per build target. The suite has no runtime network access and needs no package
manager, JavaScript runtime, downloaded module, or foreign terminal binary.
