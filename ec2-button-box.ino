const int pins[] = {32, 33, 25, 26, 27, 14, 13, 4, 16, 17, 18, 19, 21, 22};
const int numberOfPins = sizeof(pins) / sizeof(pins[0]);
int buttonPreviousState[numberOfPins];

void setup() {
  Serial.begin(9600);

  for(int i = 0; i < numberOfPins; i++) {
    pinMode(pins[i], INPUT_PULLUP); 
    buttonPreviousState[i] = LOW;
  }
}

void loop() {
  for(int i = 0; i < numberOfPins; i++) {
    int btnCurrentState = digitalRead(pins[i]);

    if (btnCurrentState != buttonPreviousState[i]) {
      if (btnCurrentState == LOW) {
        Serial.println("button pressed");
      } else {
        Serial.println("button released");
      }
      buttonPreviousState[i] = btnCurrentState;
    }
  }
}
