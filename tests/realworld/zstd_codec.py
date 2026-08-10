import subprocess


def _run_zstd(arguments, data):
    return subprocess.run(
        ["zstd", "--quiet", "--stdout", *arguments],
        input=data,
        stdout=subprocess.PIPE,
        check=True,
    ).stdout


try:
    from compression import zstd as _stdlib_zstd
except ImportError:
    def compress(data, *, level):
        return _run_zstd([f"-{level}"], data)

    def decompress(data):
        return _run_zstd(["--decompress"], data)
else:
    compress = _stdlib_zstd.compress
    decompress = _stdlib_zstd.decompress
