// arduino_tick.ino - minimal serial heartbeat for tier-1 emulator checks.
void setup() {
  Serial.begin(115200);
  Serial.println("arduino_tick boot");
}

void loop() {
  static int n = 0;
  Serial.printf("tick %d\n", n++);
  delay(1000);
}
