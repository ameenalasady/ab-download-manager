import os, sys, dbus
from PIL import Image
bus = dbus.SessionBus()
shot = dbus.Interface(bus.get_object('org.kde.KWin', '/org/kde/KWin/ScreenShot2'), 'org.kde.KWin.ScreenShot2')
r, w = os.pipe()
meta = shot.CaptureActiveScreen({'native-resolution': dbus.Boolean(True)}, dbus.types.UnixFd(w))
os.close(w)
data = b''
while True:
    chunk = os.read(r, 1 << 20)
    if not chunk: break
    data += chunk
width, height, stride = int(meta['width']), int(meta['height']), int(meta['stride'])
image = Image.frombuffer('RGBA', (width, height), data, 'raw', 'BGRA', stride, 1)
image.convert('RGB').save(sys.argv[1])
print('captured', width, height, meta.get('format'))
