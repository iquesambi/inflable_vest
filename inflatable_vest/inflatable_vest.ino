#include <MIDIUSB.h>

const int enA = 9;
const int enB = 6;
const int valve1 = 2;
const int valve2 = 3;

// Set your board ID from 0 to 6
const int boardID = 0;

// Derived note numbers

const byte inflateNote = boardID * 2 + 1+36;
const byte deflateNote = boardID * 2 + 2+36;

void setup() {
  pinMode(enA, OUTPUT);
  pinMode(enB, OUTPUT);
  pinMode(valve1, OUTPUT);
  pinMode(valve2, OUTPUT);

  Serial.begin(9600); // for MIDI forwarding
}

void loop() {
  checkIncomingMIDI();
}

void checkIncomingMIDI() {
  midiEventPacket_t rx = MidiUSB.read();
  if (rx.header == 0) {
    forwardSerialMIDI(); // Handle serial input if no USB MIDI
    return;
  }

  handleMIDIMessage(rx); // Process USB MIDI
}

void forwardSerialMIDI() {
  if (Serial.available() >= 4) {
    midiEventPacket_t serialPacket;
    serialPacket.header = Serial.read();
    serialPacket.byte1 = Serial.read();
    serialPacket.byte2 = Serial.read();
    serialPacket.byte3 = Serial.read();
    handleMIDIMessage(serialPacket);
  }
}

void handleMIDIMessage(midiEventPacket_t msg) {
  if (msg.byte1 == 0x90 && msg.byte3 > 0) {  // Note On
    if (msg.byte2 == inflateNote) {
      inflate(map(msg.byte3, 0, 127, 0, 255));
    } else if (msg.byte2 == deflateNote) {
      deflate(map(msg.byte3, 0, 127, 0, 255));
    } else {
      forwardMIDI(msg);
    }
  } else if (msg.byte1 == 0x80) {  // Note Off
    if (msg.byte2 == inflateNote || msg.byte2 == deflateNote) {
      stop();
    } else {
      forwardMIDI(msg);
    }
  }
}

void forwardMIDI(midiEventPacket_t msg) {
  Serial.write(msg.header);
  Serial.write(msg.byte1);
  Serial.write(msg.byte2);
  Serial.write(msg.byte3);
  Serial.flush();
}

void inflate(int speed) {
  analogWrite(enB, speed);
  digitalWrite(valve1, HIGH);
  digitalWrite(valve2, HIGH);
}

void deflate(int speed) {
  analogWrite(enA, speed);
  digitalWrite(valve1, LOW);
  digitalWrite(valve2, LOW);
}

void stop() {
  analogWrite(enA, 0);
  analogWrite(enB, 0);
  digitalWrite(valve1, LOW);
  digitalWrite(valve2, LOW);
}
