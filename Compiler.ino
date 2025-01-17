#include <Arduino.h>

// Pin definitions
#define DATA_PIN 2
#define CLOCK_PIN 3
#define LATCH_PIN 4
#define EEPROM_START 5
#define EEPROM_END 12
#define WRITE_ENABLE_PIN 13

// Control signal bit masks
#define CTRL_HALT      0x8000
#define CTRL_MEM_IN    0x4000
#define CTRL_RAM_IN    0x2000
#define CTRL_RAM_OUT   0x1000
#define CTRL_INST_OUT  0x0800
#define CTRL_INST_IN   0x0400
#define CTRL_REG_A_IN  0x0200
#define CTRL_REG_A_OUT 0x0100
#define CTRL_ALU_OUT   0x0080
#define CTRL_ALU_SUB   0x0040
#define CTRL_REG_B_IN  0x0020
#define CTRL_OUTPUT_IN 0x0010
#define CTRL_PC_ENABLE 0x0008
#define CTRL_PC_OUT    0x0004
#define CTRL_JUMP      0x0002
#define CTRL_FLAGS_IN  0x0001

// Microcode storage for all flag states
uint16_t microcode[4][16][8];

// Flag states
#define FLAGS_ZF0_CF0 0
#define FLAGS_ZF0_CF1 1
#define FLAGS_ZF1_CF0 2
#define FLAGS_ZF1_CF1 3

// Initialize microcode template
void setupMicrocodeTemplate() {
    uint16_t baseTemplate[16][8] = {
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, 0, 0, 0, 0, 0, 0 },  // NOP
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, CTRL_INST_OUT | CTRL_MEM_IN, CTRL_RAM_OUT | CTRL_REG_A_IN, 0, 0, 0, 0 },  // LDA
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, CTRL_INST_OUT | CTRL_MEM_IN, CTRL_RAM_OUT | CTRL_REG_B_IN, CTRL_ALU_OUT | CTRL_REG_A_IN | CTRL_FLAGS_IN, 0, 0, 0 },  // ADD
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, CTRL_INST_OUT | CTRL_MEM_IN, CTRL_RAM_OUT | CTRL_REG_B_IN, CTRL_ALU_OUT | CTRL_REG_A_IN | CTRL_ALU_SUB | CTRL_FLAGS_IN, 0, 0, 0 },  // SUB
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, CTRL_INST_OUT | CTRL_MEM_IN, CTRL_REG_A_OUT | CTRL_RAM_IN, 0, 0, 0, 0 },  // STA
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, CTRL_INST_OUT | CTRL_REG_A_IN, 0, 0, 0, 0, 0 },  // LDI
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, CTRL_INST_OUT | CTRL_JUMP, 0, 0, 0, 0, 0 },  // JMP
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, 0, 0, 0, 0, 0, 0 },  // JC
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, 0, 0, 0, 0, 0, 0 },  // JZ
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, 0, 0, 0, 0, 0, 0 },  // Custom instruction slot
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, 0, 0, 0, 0, 0, 0 },  // Custom instruction slot
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, 0, 0, 0, 0, 0, 0 },  // Custom instruction slot
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, 0, 0, 0, 0, 0, 0 },  // Custom instruction slot
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, 0, 0, 0, 0, 0, 0 },  // Custom instruction slot
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, CTRL_REG_A_OUT | CTRL_OUTPUT_IN, 0, 0, 0, 0, 0 },  // OUT
        { CTRL_MEM_IN | CTRL_PC_OUT, CTRL_RAM_OUT | CTRL_INST_IN | CTRL_PC_ENABLE, CTRL_HALT, 0, 0, 0, 0, 0 },  // HLT
    };

    for (int flags = 0; flags < 4; flags++) {
        memcpy(microcode[flags], baseTemplate, sizeof(baseTemplate));
    }

    microcode[FLAGS_ZF0_CF1][7][2] = CTRL_INST_OUT | CTRL_JUMP;  // JC
    microcode[FLAGS_ZF1_CF0][8][2] = CTRL_INST_OUT | CTRL_JUMP;  // JZ
    microcode[FLAGS_ZF1_CF1][7][2] = CTRL_INST_OUT | CTRL_JUMP;  // JC
    microcode[FLAGS_ZF1_CF1][8][2] = CTRL_INST_OUT | CTRL_JUMP;  // JZ
}

// Shifts and sets the address
void setEEPROMAddress(int address, bool enableOutput) {
    shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, (address >> 8) | (enableOutput ? 0x00 : 0x80));
    shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, address);

    digitalWrite(LATCH_PIN, LOW);
    digitalWrite(LATCH_PIN, HIGH);
    digitalWrite(LATCH_PIN, LOW);
}

// EEPROM read/write helpers
byte readEEPROM(int address) {
    for (int pin = EEPROM_START; pin <= EEPROM_END; ++pin) {
        pinMode(pin, INPUT);
    }

    setEEPROMAddress(address, true);

    byte value = 0;
    for (int pin = EEPROM_END; pin >= EEPROM_START; --pin) {
        value = (value << 1) | digitalRead(pin);
    }
    return value;
}

void writeEEPROM(int address, byte data) {
    setEEPROMAddress(address, false);
    for (int pin = EEPROM_START; pin <= EEPROM_END; ++pin) {
        pinMode(pin, OUTPUT);
    }

    for (int pin = EEPROM_START; pin <= EEPROM_END; ++pin) {
        digitalWrite(pin, data & 1);
        data >>= 1;
    }

    digitalWrite(WRITE_ENABLE_PIN, LOW);
    delayMicroseconds(1);
    digitalWrite(WRITE_ENABLE_PIN, HIGH);
    delay(10);
}

// Print EEPROM contents to serial
void printEEPROMContents(int start, int length) {
    for (int base = start; base < length; base += 16) {
        char buffer[80];
        sprintf(buffer, "%03X:", base);
        for (int offset = 0; offset < 16; ++offset) {
            byte value = readEEPROM(base + offset);
            sprintf(buffer + strlen(buffer), " %02X", value);
        }
        Serial.println(buffer);
    }
}

void setup() {
    setupMicrocodeTemplate();

    pinMode(DATA_PIN, OUTPUT);
    pinMode(CLOCK_PIN, OUTPUT);
    pinMode(LATCH_PIN, OUTPUT);
    digitalWrite(WRITE_ENABLE_PIN, HIGH);
    pinMode(WRITE_ENABLE_PIN, OUTPUT);

    Serial.begin(57600);

    for (int address = 0; address < 1024; ++address) {
        int flags = (address >> 8) & 0b11;
        int step = address & 0b111;
        int instruction = (address >> 3) & 0b1111;
        int highBit = (address >> 7) & 1;

        byte data = highBit ? microcode[flags][instruction][step] : (microcode[flags][instruction][step] >> 8);
        writeEEPROM(address, data);

        if (address % 64 == 0) {
            Serial.print(".");
        }
    }

    Serial.println("Programming complete.");
    printEEPROMContents(0, 102);
}

void loop(){
  
}
