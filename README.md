# 🎨 LibreSprite 2.0 (A_STUDI0)

> An enhanced fork of [LibreSprite](https://github.com/LibreSprite/LibreSprite) — a free pixel art editor with a **full JavaScript scripting API**.

![License](https://img.shields.io/badge/license-GPL--2.0-blue)
![Version](https://img.shields.io/badge/version-2.0-brightgreen)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)

---

## ✨ What's New in 2.0

| Feature | Description |
|---|---|
| 🟨 Full JS API | Access sprites, layers, images, selections, palettes via scripts |
| ⌨️ Script Hotkeys | Assign hotkeys to scripts from Edit → Keyboard Shortcuts |
| 💾 Save & Export | `app.save()`, `app.export()` |
| ↩️ Undo / Redo | `app.undo()`, `app.redo()` |
| 🖼️ Draw Primitives | `image.drawLine()`, `image.drawRect()`, `image.drawEllipse()` |
| 📋 Image Clone | `image.clone()` |
| 🗂️ Layer Management | `sprite.newLayer()`, `sprite.deleteLayer()` |
| 🔲 Selection Control | `sprite.select()`, `sprite.selectAll()`, `sprite.deselect()`, `sprite.invertSelection()` |
| 🎚️ Layer Properties | `layer.opacity`, `layer.isLocked` |
| 🪟 Dialog Widgets | `addColorEntry()`, `addCheckbox()`, `addSlider()`, `addComboBox()`, `addSeparator()` |

---

## 📜 JavaScript API Example

```javascript
// Replace red pixels with blue in selected area
var sprite = app.activeSprite;
var cel = app.activeCel;
var image = cel.image;
var sel = sprite.selection;

if (sel.isEmpty) {
    app.alert("Select a zone first!");
} else {
    var bounds = sel.bounds;
    app.transaction(function() {
        for (var y = bounds.y; y < bounds.y + bounds.height; y++) {
            for (var x = bounds.x; x < bounds.x + bounds.width; x++) {
                var px = image.getPixel(x - cel.position.x, y - cel.position.y);
                var r = app.pixelColor.rgbaR(px);
                if (r > 200) {
                    image.putPixel(
                        x - cel.position.x,
                        y - cel.position.y,
                        app.pixelColor.rgba(0, 0, 255, 255)
                    );
                }
            }
        }
    });
    app.refresh();
}
```

---

## 🔧 Building

### Requirements
- CMake
- Visual Studio Build Tools *(Desktop development with C++)*

### Steps

```bash
git clone https://github.com/YOUR_USERNAME/LibreSprite-2.0.git
cd LibreSprite-2.0
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

> 💡 Or use **GitHub Actions** — push your code and it builds automatically in the cloud for free.

---

## 📁 Scripts

Place your `.js` files in the scripts folder:

| Platform | Path |
|---|---|
| Windows | `%APPDATA%\LibreSprite\scripts\` |
| Linux | `~/.config/libresprite/scripts/` |
| macOS | `~/Library/Application Support/LibreSprite/scripts/` |

Run via **Scripts** menu or assign a hotkey in **Edit → Keyboard Shortcuts**.

---

## 🏆 Credits

- [Aseprite](https://github.com/aseprite/aseprite) — original editor by David Capello
- [LibreSprite](https://github.com/LibreSprite/LibreSprite) — open source fork contributors
- **LibreSprite 2.0 (A_STUDI0)** — JS API & improvements by A_STUDI0

---

## 📄 License

GNU General Public License v2.0 — see [LICENSE.txt](LICENSE.txt)
