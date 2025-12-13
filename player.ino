#include <Wire.h>
//https://github.com/joeyoung/arduino_keypads/tree/master/Keypad_I2C
#include <Keypad_I2C.h>
#include <Keypad.h>
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "driver/i2s.h"
#include "ES8388.h"


#define I2C_ADDR 0x20    
#define SDA_PIN 21
#define SCL_PIN 22


// ==== Codec ====
//ES8388 es(21, 22, 400000);//si j'enlève cette ligne, ça marche
ES8388* es = nullptr;


// ==== Variables Lecture ====
File wavFile;
bool playing = false;

const byte ROWS = 4; 
const byte COLS = 4;

char setList = 'A';
int volumeGeneral = 80;

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
  delay(300);


    //Clavier
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(50);

  // création dynamique APRÈS init I2C
  es = new ES8388(SDA_PIN, SCL_PIN, 400000);

  if (!es->init()) {
    Serial.println("ES8388 init FAILED");
  } else {
    Serial.println("ES8388 init OK");
  }

  keypad.begin();
  keypad.setHoldTime(100);
  keypad.setDebounceTime(10);
  //Wire.setClock(400000);


  
  Serial.println("READY !");

  //startPlayback("boot");
}




void loop() {

  char key = keypad.getKey();
  if (key) {
    Serial.println(key);
  }

}
