# CAD data

[日本語](README.md) | **English**

3D models of the hardware I use with Clientre 5 on the M5Stack Tab5.

| File | Contents |
|---|---|
| [Tab5_battery_case.step](Tab5_battery_case.step) | Modified battery case for the Tab5 (single body) |
| [Tab5_keyboard_JIS.step](Tab5_keyboard_JIS.step) | Customised Tab5 keyboard with a JIS layout (multi-body) |

- Format: STEP AP214, exported from Autodesk Fusion. Units are millimetres.
- These are unofficial parts. M5Stack and Tab5 are trademarks of M5Stack Technology Co., Ltd.,
  which does not endorse or support these models.
- Check the fit against your own unit before printing or machining; there is no warranty.

## Battery case

> [!CAUTION]
> Modifying lithium-ion cells is very dangerous and can cause short circuits, fire or explosion.
> Do this and use the result **entirely at your own risk**.

The case requires a modified NP-F battery:

1. Open an NP-F battery and keep only its circuit board.
2. Cut the NP-F housing down to the same height as the Tab5 battery slot.
3. Disconnect the original two 18650 cells (in series) from the board.
4. Wire two 18500 cells in series and connect them to the board at the same three points as the
   original pack: **+, − and the middle tap**.

- Use two cells of the same model, charged to the same voltage.
- Take care not to short the terminals, including with your tools, while working.

## Keyboard

![The customised JIS-layout Tab5 keyboard](../docs/photos/tab5-jis-keyboard.jpg)

- Printing the custom buttons requires a **0.2 mm nozzle**.
- When taking the keyboard apart, be careful not to lose the springs at the latches.

## License

The CAD data in this directory is licensed under
[Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).
You may share and adapt it, including commercially, as long as you give credit:

> Clientre 5 CAD data by Mozgi512 (https://github.com/Mozgi512/Clientre5), CC BY 4.0

The rest of the repository keeps its own licenses (see the top-level [README](../README_en.md#license)).
