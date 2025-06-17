#include <MIDIUSB.h> // For USB MIDI (computer <-> Arduino)
#include <MIDI.h>    // For Serial MIDI (Arduino <-> Arduino)

// Pin Definitions
const int deflation_pump = 9;
const int inflation_pump = 6;
const int valve1 = 2;
const int valve2 = 3;

// Set your board ID from 0 to 6.
const int boardID = 1; // IMPORTANT: Change this for each board in your chain!

// Derived note numbers
const byte inflateNote = boardID * 2 + 36;
const byte deflateNote = boardID * 2 + 36 + 1;

// Define the MIDI channel to listen on.
const byte midiChannel = 1; // MIDI channels are 1-16, but in code it's 0-15.

// Create a MIDI object that uses Serial1 for its communication
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
// This creates an instance named 'MIDI' that communicates over 'Serial1'

void setup() {
  pinMode(inflation_pump, OUTPUT);
  pinMode(deflation_pump, OUTPUT);
  pinMode(valve1, OUTPUT);
  pinMode(valve2, OUTPUT);

  // Initialize USB Serial for debugging output to the computer
  Serial.begin(115200);

  // Initialize the MIDI.h library for Serial1.
  // It automatically handles Serial1.begin(31250) internally.
  MIDI.begin(MIDI_CHANNEL_OMNI); // Listen to all channels for forwarding
  MIDI.turnThruOff(); // Important: Prevent MIDI.h from auto-forwarding directly

  Serial.print("--- Board ID: ");
  Serial.print(boardID);
  Serial.println(" initialized ---");
  Serial.print("Listening for Inflate Note: ");
  Serial.println(inflateNote);
  Serial.print("Listening for Deflate Note: ");
  Serial.println(deflateNote);
  Serial.println("USB MIDI via MIDIUSB, Serial MIDI via MIDI.h on Serial1.");
}

void loop() {
  // 1. Handle USB MIDI input (from computer)
  midiEventPacket_t rxUsb = MidiUSB.read();
  if (rxUsb.header != 0) {
    Serial.println("Received USB MIDI.");
    handleMIDIMessage(rxUsb);
  }

  // 2. Handle Serial MIDI input (from previous Arduino in chain)
  // The MIDI.read() function from MIDI.h will handle incoming bytes on Serial1
  // and trigger its internal parsing logic.
  if (MIDI.read()) {
    // If MIDI.read() returns true, a complete MIDI message was just parsed.
    byte command = MIDI.getType();
    byte channel = MIDI.getChannel();
    byte note = MIDI.getData1();
    byte velocity = MIDI.getData2();

    // Reconstruct the midiEventPacket_t for handleMIDIMessage for consistency
    // Note: This reconstruction is a bit of a hack as MIDI.h simplifies the message.
    // For full fidelity forwarding, it's generally better to pass the raw bytes.
    // However, for processing, this is fine.
    midiEventPacket_t rxSerial;
    // For NoteOn/Off, command will be 0x90/0x80. byte1 needs command | channel
    if (command == midi::NoteOn) rxSerial.byte1 = 0x90 | (channel - 1);
    else if (command == midi::NoteOff) rxSerial.byte1 = 0x80 | (channel - 1);
    else if (command == midi::ControlChange) rxSerial.byte1 = 0xB0 | (channel - 1);
    // ... add other types if needed

    rxSerial.header = (command >> 4); // Basic approximation for header
    rxSerial.byte2 = note;
    rxSerial.byte3 = velocity;

    Serial.println("Received Serial1 MIDI via MIDI.h.");
    handleMIDIMessage(rxSerial);

    // After processing, you still need to decide if this message should be forwarded.
    // The MIDI.h library has send functions, but for a daisy chain, it's easier
    // to just re-send the original raw packet data if it wasn't consumed.
    // We'll slightly adjust the handleMIDIMessage to handle forwarding.
  }
}

// Function to process a MIDI message.
// This function will decide if the message is for this board or needs forwarding.
void handleMIDIMessage(midiEventPacket_t msg) {
  byte command = msg.byte1 & 0xF0; // Extract the command (e.g., Note On, Note Off)
  byte channel = msg.byte1 & 0x0F; // Extract the channel (0-15)
  byte note = msg.byte2;
  byte velocity = msg.byte3;

  Serial.print("Processing MIDI message: Command=0x");
  Serial.print(command, HEX);
  Serial.print(", Channel=");
  Serial.print(channel + 1); // Display channel as 1-16
  Serial.print(", Note=");
  Serial.print(note);
  Serial.print(", Velocity=");
  Serial.println(velocity);

  // Check if the message is a Note On event and not a velocity of 0
  if (command == 0x90 && velocity > 0) { // Note On
    // Check if it's for this board's inflate note AND the correct channel
    if (note == inflateNote && channel == (midiChannel - 1)) {
      Serial.print("Activating INFLATE for this board with speed: ");
      Serial.println(map(velocity, 0, 127, 0, 255));
      inflate(map(velocity, 0, 127, 0, 255));
    }
    // Check if it's for this board's deflate note AND the correct channel
    else if (note == deflateNote && channel == (midiChannel - 1)) {
      Serial.print("Activating DEFLATE for this board with speed: ");
      Serial.println(map(velocity, 0, 127, 0, 255));
      deflate(map(velocity, 0, 127, 0, 255));
    } else {
      // If the note is not for this board, forward it to the next.
      Serial.println("Note On not for this board, forwarding...");
      forwardMIDI(msg); // Forward the raw message
    }
  }
  // Check if it's a Note Off event (0x80) or a Note On with velocity 0
  else if (command == 0x80 || (command == 0x90 && velocity == 0)) { // Note Off
    if ((note == inflateNote || note == deflateNote) && channel == (midiChannel - 1)) {
      Serial.println("Stopping action for this board.");
      stop();
    } else {
      // If the note is not for this board, forward it to the next.
      Serial.println("Note Off not for this board, forwarding...");
      forwardMIDI(msg); // Forward the raw message
    }
  }
  // For any other MIDI message type (Control Change, Program Change, etc.)
  // or if the channel doesn't match for specific commands, just forward it.
  else {
    Serial.println("Other MIDI message type or wrong channel, forwarding...");
    forwardMIDI(msg); // Forward the raw message
  }
}

// Function to forward a MIDI message to the next Arduino in the chain via Serial1.
// We use MIDI.h's send functions here for simplicity and robustness.
void forwardMIDI(midiEventPacket_t msg) {
  byte command = msg.byte1 & 0xF0;
  byte channel = (msg.byte1 & 0x0F) + 1; // Convert 0-15 to 1-16 for MIDI.h functions

  if (command == 0x90) { // Note On
    MIDI.sendNoteOn(msg.byte2, msg.byte3, channel);
  } else if (command == 0x80) { // Note Off
    MIDI.sendNoteOff(msg.byte2, msg.byte3, channel);
  } else if (command == 0xB0) { // Control Change
    MIDI.sendControlChange(msg.byte2, msg.byte3, channel);
  }
  // Add other MIDI message types (Program Change, Pitch Bend, etc.) as needed for forwarding
  // Example for Program Change (0xC0):
  // else if (command == 0xC0) { MIDI.sendProgramChange(msg.byte2, channel); }

  Serial.println("Message forwarded via Serial1 using MIDI.h.");
}

void inflate(int speed) {
  analogWrite(inflation_pump, speed);
  digitalWrite(deflation_pump, LOW);
  digitalWrite(valve1, HIGH);
  digitalWrite(valve2, HIGH);
  Serial.print("Inflating with speed: ");
  Serial.println(speed);
}

void deflate(int speed) {
  analogWrite(deflation_pump, speed);
  digitalWrite(inflation_pump, LOW);
  digitalWrite(valve1, LOW);
  digitalWrite(valve2, LOW);
  Serial.print("Deflating with speed: ");
  Serial.println(speed);
}

void stop() {
  analogWrite(inflation_pump, 0);
  analogWrite(deflation_pump, 0);
  digitalWrite(valve1, LOW);
  digitalWrite(valve2, LOW);
  Serial.println("All pumps stopped and valves closed.");
}