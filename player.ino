#include <Wire.h>
//https://github.com/joeyoung/arduino_keypads/tree/master/Keypad_I2C
#include <Keypad_I2C.h>
#include <Keypad.h>

#define I2C_ADDR 0x20        // Adresse détectée
#define SDA_PIN 21
#define SCL_PIN 22

const byte ROWS = 4; 
const byte COLS = 4;

// Disposition des touches (adapter si ton clavier diffère)
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// Sur un PCF8574, les pins vont de 0 à 7.
// Mapping typique :
//   P0–P3 = lignes
//   P4–P7 = colonnes
byte rowPins[ROWS] = {0, 1, 2, 3};
byte colPins[COLS] = {4, 5, 6, 7};

// Création du keypad via I2C
Keypad_I2C keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS, I2C_ADDR, PCF8574 );

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  keypad.begin();
  keypad.setDebounceTime(10);
  
  Serial.println("Clavier I2C prêt.");
}

void loop() {
  char key = keypad.getKey();

  if (key) {
    Serial.print("Touche appuyée : ");
    Serial.println(key);
  }
}
