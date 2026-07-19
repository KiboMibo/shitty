# Zutty integration tests

Build Zutty, then run the black-box tests:

```sh
./build
python3 -m unittest discover -s tests -v
```

The harness starts the regular `zutty` binary in headless test mode. It sends
terminal output and control events over an inherited Unix socket and reads
logical screen snapshots from the same socket.
