# Axiometa Games

Welcome to the **Axiometa** project repository. This folder is used to store game development files, ports, and experiments built for the custom Axiometa microcontroller hardware.

## Project Structure

This repository currently hosts two classic arcade game recreations adapted for a portrait-oriented screen, rotary encoder control, and tactile interactions:

*   **[breakout.ino](file:///Users/hectorg/Data/Code/axiometa/breakout.ino)**: An Atari 2600-style brick-smashing game designed for a portrait display. Players turn the rotary encoder to slide the paddle and press the button to launch the ball. The ball speeds up each level and when the board is nearly cleared. The game ends when three balls are lost.
*   **[kaboom.ino](file:///Users/hectorg/Data/Code/axiometa/kaboom.ino)**: An Atari 2600-style bomb-catching game. A Mad Bomber moves back and forth across the top of the screen dropping bombs. The player must use the rotary encoder to slide a stack of water buckets to catch the bombs before they reach the ground. If a bomb is missed, all bombs on the screen explode, a bucket is lost, and the round restarts.

---

## Hardware Configuration

Both games run on an identical hardware rig consisting of the following components and pin connections:

| Port | Component | Part Number | Configuration / Pin Mapping |
| :--- | :--- | :--- | :--- |
| **P1** | **ST7735 LCD Display** | AX22-0034 | CS=`P1_IO0`, RST=`P1_IO1`, DC=`P1_IO2` + Shared SPI |
| **P2** | **Passive Buzzer** | AX22-0018 | Signal=`P2_IO1` |
| **P3** | **Tactile LED Button** | AX22-0050 | Button=`P3_IO1`, LED=`P3_IO2` |
| **P4** | **Rotary Encoder** | AX22-0003 | BTN=`P4_IO0`, CLK=`P4_IO1`, DT=`P4_IO2` |

## Dependencies

The code relies on standard Adafruit graphics libraries and a rotary encoder library:
- [Adafruit_GFX](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit_ST7735](https://github.com/adafruit/Adafruit-ST7735-Library)
- [RotaryEncoder](https://github.com/mathertel/RotaryEncoder) (by Matthias Hertel)
