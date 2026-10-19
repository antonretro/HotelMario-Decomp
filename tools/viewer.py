"""Browse images decoded from your own disc (by tools/cdi_gfx.py) in a PC window.

Usage: python tools/viewer.py [folder]      (default: build/eu/gfx)
Keys: Left/Right = previous/next, Home/End = first/last, 1/2/3 = zoom, Esc = quit.
"""
import os
import sys
import tkinter as tk

from PIL import Image, ImageTk


def find_images(root):
    out = []
    for dirpath, _, files in os.walk(root):
        out += [os.path.join(dirpath, f) for f in sorted(files) if f.lower().endswith(".png")]
    return out


class Viewer:
    def __init__(self, files):
        self.files, self.i, self.zoom = files, 0, 2
        self.win = tk.Tk()
        self.win.configure(bg="#120b2e")
        self.label = tk.Label(self.win, bg="#120b2e")
        self.label.pack(padx=8, pady=8)
        self.info = tk.Label(self.win, bg="#120b2e", fg="#e8b84a", font=("Consolas", 10))
        self.info.pack(pady=(0, 8))
        for key, fn in (("<Right>", lambda e: self.step(1)), ("<Left>", lambda e: self.step(-1)),
                        ("<Home>", lambda e: self.go(0)), ("<End>", lambda e: self.go(len(self.files) - 1)),
                        ("<Escape>", lambda e: self.win.destroy())):
            self.win.bind(key, fn)
        for z in (1, 2, 3):
            self.win.bind(str(z), lambda e, z=z: self.set_zoom(z))
        self.show()

    def set_zoom(self, z):
        self.zoom = z
        self.show()

    def step(self, d):
        self.go((self.i + d) % len(self.files))

    def go(self, i):
        self.i = i
        self.show()

    def show(self):
        path = self.files[self.i]
        img = Image.open(path)
        img = img.resize((img.width * self.zoom, img.height * self.zoom), Image.NEAREST)
        self.photo = ImageTk.PhotoImage(img)
        self.label.configure(image=self.photo)
        name = os.path.relpath(path)
        self.info.configure(text=f"{self.i + 1}/{len(self.files)}  {name}  ({img.width // self.zoom}x{img.height // self.zoom})")
        self.win.title(f"Hotel Mario decomp viewer - {os.path.basename(path)}")


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "eu", "gfx")
    files = find_images(root)
    if not files:
        sys.exit(f"no images in {root}; run tools/cdi_gfx.py on a .rtf file first")
    Viewer(files).win.mainloop()


if __name__ == "__main__":
    main()
