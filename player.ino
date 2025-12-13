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

//#define KEY1 36 
//#define KEY2 13 
//#define KEY3 19 
//#define KEY4 23 
//#define KEY5 18 
//#define KEY6 5


ES8388* es = nullptr;


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

File wavFile;
bool playing = false;

const byte ROWS = 4; 
const byte COLS = 4;

char setList = 'A';
int volumeGeneral = 80;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {0, 1, 2, 3};
byte colPins[COLS] = {4, 5, 6, 7};

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

void manageKeyPad(char key) {
  Serial.println(key);
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
			volumeGeneral -= 20;
			if (volumeGeneral<10) {
				volumeGeneral = 80;
			}			
			es->setOutputVolume(volumeGeneral);
		default: 
		break;
	}
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(SDA_PIN, SCL_PIN);  
  delay(50);

  es = new ES8388(SDA_PIN, SCL_PIN, 400000);
  if (!es->init()) {
    Serial.println("ES8388 init FAILED");
  } else {
    Serial.println("ES8388 init OK");
  }

  es->inputSelect(IN2);
  es->setInputGain(8);
  es->mixerSourceSelect(MIXADC, MIXADC);
  es->mixerSourceControl(DACOUT);
  es->outputSelect(OUT2);
  es->setOutputVolume(volumeGeneral);

  i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &i2s_pins);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  SD.begin(SD_CS);

  keypad.begin();
  keypad.setDebounceTime(10);  
  
  Serial.println("READY !");
  startPlayback("boot");
}




void loop() {
  char key = keypad.getKey();
  if (key) {
	  manageKeyPad(key);
  }

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
