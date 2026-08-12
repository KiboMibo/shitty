# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.


# Exact input/output vectors from VTE src/utf8-test.cc, itself copied from
# encoding_rs. Both columns are hexadecimal: bytes on the left, Unicode scalar
# values on the right.
REPLACEMENT_DATA = """
:
00:0
6162:61,62
61c3a45a:61,e4,5a
61e298835a:61,2603,5a
61f09f92a95a:61,1f4a9,5a
61c35a:61,fffd,5a
61c3:61,fffd
61e2985a:61,fffd,5a
61e298:61,fffd
61f09f925a:61,fffd,5a
61f09f92:61,fffd
61bf5a:61,fffd,5a
61bf:61,fffd
61bfbf5a:61,fffd,fffd,5a
61bfbf:61,fffd,fffd
61c3a4805a:61,e4,fffd,5a
61c3a480:61,e4,fffd
61c3a4bf5a:61,e4,fffd,5a
61c3a4bf:61,e4,fffd
61e29883805a:61,2603,fffd,5a
61e2988380:61,2603,fffd
61e29883bf5a:61,2603,fffd,5a
61e29883bf:61,2603,fffd
61f09f92a9805a:61,1f4a9,fffd,5a
61f09f92a980:61,1f4a9,fffd
61f09f92a9bf5a:61,1f4a9,fffd,5a
61f09f92a9bf:61,1f4a9,fffd
5a00:5a,0
5a005a:5a,0,5a
61c080:61,fffd,fffd
61c0805a:61,fffd,fffd,5a
61e08080:61,fffd,fffd,fffd
61e080805a:61,fffd,fffd,fffd,5a
61f0808080:61,fffd,fffd,fffd,fffd
61f08080805a:61,fffd,fffd,fffd,fffd,5a
61ff:61,fffd
61ff5a:61,fffd,5a
617f:61,7f
617f5a:61,7f,5a
61c1bf:61,fffd,fffd
61c1bf5a:61,fffd,fffd,5a
61e081bf:61,fffd,fffd,fffd
61e081bf5a:61,fffd,fffd,fffd,5a
61f08081bf:61,fffd,fffd,fffd,fffd
61f08081bf5a:61,fffd,fffd,fffd,fffd,5a
61805a:61,fffd,5a
6180:61,fffd
6180805a:61,fffd,fffd,5a
618080:61,fffd,fffd
618080805a:61,fffd,fffd,fffd,5a
61808080:61,fffd,fffd,fffd
61808080805a:61,fffd,fffd,fffd,fffd,5a
6180808080:61,fffd,fffd,fffd,fffd
61c280:61,80
61c2805a:61,80,5a
61e08280:61,fffd,fffd,fffd
61e082805a:61,fffd,fffd,fffd,5a
61f0808280:61,fffd,fffd,fffd,fffd
61f08082805a:61,fffd,fffd,fffd,fffd,5a
61c180:61,fffd,fffd
61c1805a:61,fffd,fffd,5a
61c27f:61,fffd,7f
61c27f5a:61,fffd,7f,5a
61dfbf:61,7ff
61dfbf5a:61,7ff,5a
61e09fbf:61,fffd,fffd,fffd
61e09fbf5a:61,fffd,fffd,fffd,5a
61f0809fbf:61,fffd,fffd,fffd,fffd
61f0809fbf5a:61,fffd,fffd,fffd,fffd,5a
61e0a080:61,800
61e0a0805a:61,800,5a
61f080a080:61,fffd,fffd,fffd,fffd
61f080a0805a:61,fffd,fffd,fffd,fffd,5a
61ed9fbf:61,d7ff
61ed9fbf5a:61,d7ff,5a
61f08d9fbf:61,fffd,fffd,fffd,fffd
61f08d9fbf5a:61,fffd,fffd,fffd,fffd,5a
61eda080:61,fffd,fffd,fffd
61eda0805a:61,fffd,fffd,fffd,5a
61f08da080:61,fffd,fffd,fffd,fffd
61f08da0805a:61,fffd,fffd,fffd,fffd,5a
61edbfbf:61,fffd,fffd,fffd
61edbfbf5a:61,fffd,fffd,fffd,5a
61f08dbfbf:61,fffd,fffd,fffd,fffd
61f08dbfbf5a:61,fffd,fffd,fffd,fffd,5a
61ee8080:61,e000
61ee80805a:61,e000,5a
61f08e8080:61,fffd,fffd,fffd,fffd
61f08e80805a:61,fffd,fffd,fffd,fffd,5a
61efbfbf:61,ffff
61efbfbf5a:61,ffff,5a
61f08fbfbf:61,fffd,fffd,fffd,fffd
61f08fbfbf5a:61,fffd,fffd,fffd,fffd,5a
61f0908080:61,10000
61f09080805a:61,10000,5a
61f48fbfbf:61,10ffff
61f48fbfbf5a:61,10ffff,5a
61f4908080:61,fffd,fffd,fffd,fffd
61f49080805a:61,fffd,fffd,fffd,fffd,5a
61f48fbfff:61,fffd,fffd
61f48fbfff5a:61,fffd,fffd,5a
f880808080:fffd,fffd,fffd,fffd,fffd
f8bfbfbfbf:fffd,fffd,fffd,fffd,fffd
fc8080808080:fffd,fffd,fffd,fffd,fffd,fffd
fdbfbfbfbfbf:fffd,fffd,fffd,fffd,fffd,fffd
fe808080808080:fffd,fffd,fffd,fffd,fffd,fffd,fffd
febfbfbfbfbfbf:fffd,fffd,fffd,fffd,fffd,fffd,fffd
"""


def replacement_vectors():
    result = []
    for line in REPLACEMENT_DATA.strip("\n").splitlines():
        encoded, expected = line.split(":")
        codepoints = tuple(
            int(value, 16) for value in expected.split(",") if value
        )
        result.append((bytes.fromhex(encoded), codepoints))
    return tuple(result)


def case_names():
    return ("decode", "replacement")
