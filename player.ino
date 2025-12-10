#include <Wire.h>
//https://github.com/joeyoung/arduino_keypads/tree/master/Keypad_I2C
#include <Keypad_I2C.h>
#include <Keypad.h>
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "driver/i2s.h"
#include "ES8388.h"

#define SD_CS   13
#define SD_SCK  14
#define SD_MISO 2
#define SD_MOSI 15

#define I2C_ADDR 0x20    
#define SDA_PIN 21
#define SCL_PIN 22

// ==== Boutons ====
#define KEY1 36 
#define KEY2 13 
#define KEY3 19 
#define KEY4 23 
#define KEY5 18 
#define KEY6 5

bool key1_down = false;
bool key2_down = false;
bool key3_down = false;
bool key4_down = false;
bool key5_down = false;
bool key6_down = false;


// ==== Codec ====
ES8388 es(33, 32, 400000);

// ==== I2S ====
i2s_config_t i2s_cfg = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = 44100,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
  .communication_format = I2S_COMM_FORMAT_I2S,
  .intr_alloc_flags = 0,
  .dma_buf_count = 8,
  .dma_buf_len = 256,
  .use_apll = false,
  .tx_desc_auto_clear = true,
  .fixed_mclk = 0
};

i2s_pin_config_t i2s_pins = {
  .bck_io_num = 27,
  .ws_io_num = 25,
  .data_out_num = 26,
  .data_in_num = -1
};

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


bool skipWavHeader(File &f) {
  uint8_t h[44];
  if (f.size() < 44) return false;
  f.read(h, 44);

  if (strncmp((char*)h, "RIFF", 4) != 0) return false;
  if (strncmp((char*)h + 8, "WAVE", 4) != 0) return false;

  uint16_t fmt = h[20] | (h[21] << 8);
  uint16_t ch  = h[22] | (h[23] << 8);
  uint32_t sr  = h[24] | (h[25] << 8) | (h[26] << 16) | (h[27] << 24);
  uint16_t bits= h[34] | (h[35] << 8);

  if (fmt != 1 || ch != 2 || sr != 44100 || bits != 16) return false;
  return true;
}

void startPlayback(String filename) {
  Serial.println("Démarrage lecture "+filename+".wav");

  wavFile = SD.open("/"+filename+".wav");
  if (!wavFile) {
    Serial.println("❌ Impossible d’ouvrir "+filename+".wav");
	wavFile = SD.open("/file_not_found.wav");
	if (!wavFile) {
		Serial.println("❌ Impossible d’ouvrir file_not_found.wav");
		return;
	}
  }
  if (!skipWavHeader(wavFile)) {
    Serial.println("❌ Format WAV incompatible");
	wavFile = SD.open("/bad_file_format.wav");
	if (!wavFile) {
		Serial.println("❌ Impossible d’ouvrir bad_file_format.wav");
		return;
	}
  }

  playing = true;
}

void stopPlayback() {
  if (playing) {
    Serial.println("Lecture stoppée.");
    wavFile.close();
    playing = false;

    // Envoi silence
    uint8_t zero[512] = {0};
    size_t written;
    i2s_write(I2S_NUM_0, zero, sizeof(zero), &written, 10);
  }
}

void onKeyPress(int keynum) {
  Serial.print("PRESS ");
  Serial.println(keynum);
  switch(keynum) {
    case 1:
      delay(150);
      stopPlayback();
    break;
    case 2: 
      delay(150);
      startPlayback("A0");
    break;
    case 3: 
      delay(150);
      startPlayback("A1");
    break;
    default: 
    break;
  }
}

void onKeyRelease(int keynum) {
  Serial.print("RELEASE ");
  Serial.println(keynum);
}

void manageKeys() {
  if (!digitalRead(KEY1) && !key1_down) {key1_down = true;onKeyPress(1);}
  if (digitalRead(KEY1) && key1_down) {key1_down = false;onKeyRelease(1);}
  if (!digitalRead(KEY2) && !key2_down) {key2_down = true;onKeyPress(2);}
  if (digitalRead(KEY2) && key2_down) {key2_down = false;onKeyRelease(2);}
  if (!digitalRead(KEY3) && !key3_down) {key3_down = true;onKeyPress(3);}
  if (digitalRead(KEY3) && key3_down) {key3_down = false;onKeyRelease(3);}
  if (!digitalRead(KEY4) && !key4_down) {key4_down = true;onKeyPress(4);}
  if (digitalRead(KEY4) && key4_down) {key4_down = false;onKeyRelease(4);}
  if (!digitalRead(KEY5) && !key5_down) {key5_down = true;onKeyPress(5);}
  if (digitalRead(KEY5) && key5_down) {key5_down = false;onKeyRelease(5);}
  if (!digitalRead(KEY6) && !key6_down) {key6_down = true;onKeyPress(6);}
  if (digitalRead(KEY6) && key6_down) {key6_down = false;onKeyRelease(6);}
}

void manageKeyPad(char key) {
	switch(key) {
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': 
			delay(150);		  
			startPlayback(String(setList) + String(key));
		break;
		case 'A': case 'B': case 'C': case 'D': 
			setList = key;
		break;
		case '*': 
		  delay(150);
		  stopPlayback();
		break;
		case '#':
			volume -= 20;
			if (volume<10) {
				volume = 80;
			}			
			es.setOutputVolume(volume);
		default: 
		break;
	}
}

void setup() {
  Serial.begin(115200);
  delay(300);

  // ==== Init boutons ====
  pinMode(KEY1, INPUT_PULLUP);
  pinMode(KEY2, INPUT_PULLUP);
  pinMode(KEY3, INPUT_PULLUP);
  pinMode(KEY4, INPUT_PULLUP);
  pinMode(KEY5, INPUT_PULLUP);
  pinMode(KEY6, INPUT_PULLUP);

  // ==== ES8388 ====
  if (!es.init()) Serial.println("Init Fail");
  es.inputSelect(IN2);
  es.setInputGain(8);
  es.mixerSourceSelect(MIXADC, MIXADC);
  es.mixerSourceControl(DACOUT);
  es.outputSelect(OUT2);
  es.setOutputVolume(volume);

  // ==== I2S ====
  i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &i2s_pins);

  // ==== SD ====
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  SD.begin(SD_CS);
  
  //Clavier
  Wire.begin(SDA_PIN, SCL_PIN);
  keypad.begin();
  keypad.setDebounceTime(10);  
  
  Serial.println("READY !");

  startPlayback("boot");
}




void loop() {

  manageKeys();
  
  char key = keypad.getKey();
  if (key) {
	manageKeyPad(key);
  }

  // ---- 2. LECTURE EN COURS ----
  if (playing) {
    uint8_t buffer[1024];
    size_t n = wavFile.read(buffer, sizeof(buffer));

    if (n > 0) {
      size_t written;
      i2s_write(I2S_NUM_0, buffer, n, &written, portMAX_DELAY);
    } else {
      Serial.println("Fin du fichier.");
      stopPlayback();
    }
  }
}
