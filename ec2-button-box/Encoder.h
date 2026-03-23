#pragma once
#include <Arduino.h>

namespace Enc {
  enum Move { cw, ccw, none };

  class Encoder {
    byte clk, dt;
    int  lastClk;
    unsigned long lastDebounce = 0;
    unsigned long debounceUs;

  public:
    Encoder() : clk(0), dt(0), lastClk(HIGH), debounceUs(1000) {}
    Encoder(byte clk, byte dt, unsigned long debounceUs)
      : clk(clk), dt(dt), lastClk(HIGH), debounceUs(debounceUs) {}

    void begin() {
      pinMode(clk, INPUT_PULLUP);
      pinMode(dt,  INPUT_PULLUP);
      lastClk = digitalRead(clk);
    }

    Move read() {
      int newClk = digitalRead(clk);
      if (newClk == lastClk) return none;

      unsigned long now = micros();
      if ((now - lastDebounce) > debounceUs) {
        lastDebounce = now;
        lastClk = newClk;
        if (newClk == LOW)
          return (digitalRead(dt) == HIGH) ? cw : ccw;
      } else {
        lastClk = newClk; // update to avoid getting stuck
      }
      return none;
    }
  };
}
