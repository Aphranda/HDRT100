import pathlib
p = pathlib.Path("out/origin-handshake.py")
s = p.read_text(encoding="utf-8")
s = s.replace('BUILD = "20261008092856"', 'BUILD = "20261008101353"')
p.write_text(s, encoding="utf-8")
print("build id synced")