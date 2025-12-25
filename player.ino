#include <Wire.h>
//https://github.com/joeyoung/arduino_keypads/tree/master/Keypad_I2C
#include <Keypad_I2C.h>
#include <Keypad.h>
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "driver/i2s.h"
// /!\ modifier cette librairie pour supprimer le destructeur !
#include "ES8388.h"

#define SD_CS   13
#define SD_SCK  14
#define SD_MISO 2
#define SD_MOSI 15

#define I2C_ADDR 0x20    
#define SDA_CODEC 33
#define SCL_CODEC 32
#define SDA_KEYS 21
#define SCL_KEYS 22

TwoWire I2C_CODEC = Wire;    
TwoWire I2C_KEYS  = Wire1;   

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
float volumeControl = 1.0f;
float volumeGeneral = 0.8f;   // volume réellement appliqué
float targetVolume  = 0.9f;   // volume cible

float fadeDownFactor = 0.40f; 
float fadeUpFactor  = 5.0f;  
float volumeEpsilon  = 0.001f;
String nextTrack = "";
bool pendingStart = false;

uint32_t bytesRemaining = 0;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {0, 1, 2, 3};
byte colPins[COLS] = {4, 5, 6, 7};

Keypad_I2C keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS, I2C_ADDR, 1, &I2C_KEYS);


inline void updateVolumeFade() {
  //Fade IN
  if (volumeGeneral > volumeEpsilon && volumeGeneral < targetVolume) {
    volumeGeneral *= fadeUpFactor;
    if (volumeGeneral > targetVolume) {
      volumeGeneral = targetVolume;
    }
    Serial.print("vol+ = ");
    Serial.println(volumeGeneral);
  } 
  //Fade Out
  else if (volumeGeneral > targetVolume) {
    volumeGeneral *= fadeDownFactor;
    if (volumeGeneral < targetVolume) {
      volumeGeneral = targetVolume;
    }  
    if (volumeGeneral < volumeEpsilon || volumeGeneral < targetVolume) {
      volumeGeneral = targetVolume;
    }    
    Serial.print("vol- = ");
    Serial.println(volumeGeneral);
  }

}


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
  if (fmt != 1 || ch < 1 || ch > 2 || sr != 44100 || bits != 16) return false;
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
  bytesRemaining = wavFile.size() - wavFile.position();
  playing = true;
}

void stopPlayback() {

  if (playing) {
    Serial.println("Lecture stoppée.");
    wavFile.close();
    playing = false;
    uint8_t zero[512] = {0};
    size_t written;

    for (int i = 0; i < 12; i++) {
      i2s_write(I2S_NUM_0, zero, sizeof(zero), &written, portMAX_DELAY);
    }  
  }
}

void manageKeyPad(char key) {
  //Serial.println(key);
	switch(key) {
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': 
      nextTrack = String(setList) + String(key);
      if (playing) {
        targetVolume = 0.0f;
        pendingStart = true;
      }
      else {
        volumeGeneral = 0.1f;
        targetVolume = 0.9f;
        startPlayback(nextTrack);
      }

		break;
		case 'A': case 'B': case 'C': case 'D': 
			setList = key;
		break;
		case '*': 
		  stopPlayback();
		break;
		case '#':    
			volumeControl += 0.4f;
			if (volumeControl > 1.01f) {
				volumeControl = 0.2f;
			}			
    break;
		default: 
		break;
	}
}

void setup() {
  Serial.begin(115200);
  delay(300);
  I2C_CODEC.begin(SDA_CODEC, SCL_CODEC, 400000);
  I2C_KEYS.begin(SDA_KEYS, SCL_KEYS, 100000);

  delay(10);

  i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &i2s_pins);

  uint8_t silence[256] = {0};
  size_t written;
  i2s_write(I2S_NUM_0, silence, sizeof(silence), &written, 50);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  SD.begin(SD_CS);

  keypad.begin();
  keypad.setDebounceTime(10);  

  es = new ES8388(I2C_CODEC);
  if (!es->init()) {
    Serial.println("ES8388 init FAILED");
  } else {
    Serial.println("ES8388 init OK");
  }

  es->inputSelect(IN2);
  es->setInputGain(0);
  es->mixerSourceSelect(MIXADC, MIXADC);
  es->mixerSourceControl(DACOUT);
  es->outputSelect(OUT2);
  es->setOutputVolume(90);

  Serial.println("READY !");
  delay(200);
  startPlayback("boot");
}



void loop() {
  char key = keypad.getKey();
  if (key)  {
    manageKeyPad(key);
  }

  updateVolumeFade();

  if (playing) {

    uint8_t buffer[1024];
    size_t n = wavFile.read(buffer, sizeof(buffer));
    
    if (n > 0) {
      int16_t* samples = (int16_t*)buffer;
      int sampleCount = n / 2;

      for (int i = 0; i < sampleCount; i++) {
        samples[i] = (int16_t)(samples[i] * volumeGeneral * volumeControl);
      }

      size_t written;
      i2s_write(I2S_NUM_0, buffer, n, &written, portMAX_DELAY);

      bytesRemaining -= n;
      if (bytesRemaining < 3000) {
        volumeGeneral = 0.1f;
      }
      if (bytesRemaining < 1500) {
        volumeGeneral = 0.0f;
      }
    } 
    else {
      stopPlayback();
    }
  }

  if (pendingStart && volumeGeneral <= 0.001f) {
    stopPlayback();
    startPlayback(nextTrack);
    targetVolume = 0.9f;
    pendingStart = false;
  }
}
