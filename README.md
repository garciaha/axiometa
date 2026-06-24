# Axiometa Projects

Welcome to the **Axiometa** project repository. This folder hosts software programs, ports, custom systems, and experiments built for the Axiometa microcontroller ecosystem.

## Projects

### 🕹️ Breakout ([breakout.ino](file:///Users/hectorg/Data/Code/axiometa/breakout.ino))

An Atari 2600-style brick-smashing game adapted for a portrait-oriented screen. Clear all the bricks to advance to the next level while the ball speeds up over time.

*   **Gameplay**: Turn the rotary encoder to move the paddle, and press the tactile button to launch the ball. You start with three lives.
*   **Hardware Setup**:
    *   **P1**: ST7735 LCD Display (`CS=P1_IO0`, `RST=P1_IO1`, `DC=P1_IO2`)
    *   **P2**: Passive Buzzer (`Signal=P2_IO1`)
    *   **P3**: Tactile LED Button (`Button=P3_IO1`, `LED=P3_IO2`)
    *   **P4**: Rotary Encoder (`BTN=P4_IO0`, `CLK=P4_IO1`, `DT=P4_IO2`)

---

### 💣 Kaboom! ([kaboom.ino](file:///Users/hectorg/Data/Code/axiometa/kaboom.ino))

An Atari 2600-style bomb-catching game. A Mad Bomber roams the top of the screen dropping bombs, and you must catch them before they hit the ground.

*   **Gameplay**: Turn the rotary encoder to move a stack of water buckets to catch falling bombs. Press the tactile button to start or restart a round.
*   **Hardware Setup**:
    *   **P1**: ST7735 LCD Display (`CS=P1_IO0`, `RST=P1_IO1`, `DC=P1_IO2`)
    *   **P2**: Passive Buzzer (`Signal=P2_IO1`)
    *   **P3**: Tactile LED Button (`Button=P3_IO1`, `LED=P3_IO2`)
    *   **P4**: Rotary Encoder (`BTN=P4_IO0`, `CLK=P4_IO1`, `DT=P4_IO2`)

---

## Global Dependencies

These firmware projects rely on the following standard Arduino libraries:
- [Adafruit_GFX](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit_ST7735](https://github.com/adafruit/Adafruit-ST7735-Library)
- [RotaryEncoder](https://github.com/mathertel/RotaryEncoder) (by Matthias Hertel)
