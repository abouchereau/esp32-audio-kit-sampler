#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "driver/i2s.h"
#include "ES8388.h"

// ==== Broches SD validées ====
#define SD_CS   13
#define SD_SCK  14
#define SD_MISO 2
#define SD_MOSI 15

// ==== ES8388 en I2C sur 33/32  ====
ES8388 es(33, 32, 400000);

// ==== I2S config  ====
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

// ==== Broches I2S de ta carte ====
i2s_pin_config_t i2s_pins = {
  .bck_io_num = 27,
  .ws_io_num = 25,
  .data_out_num = 26,
  .data_in_num = -1
};

// ====== Fonction pour sauter l'en-tête WAV ======
bool skipWavHeader(File &f) {
  if (f.size() < 44) return false;

  uint8_t header[44];
  f.read(header, 44);

  // Vérification "RIFF" "WAVE"
  if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F') return false;
  if (header[8] != 'W' || header[9] != 'A' || header[10] != 'V' || header[11] != 'E') return false;

  // Vérification format PCM 16 bits stéréo 44100 Hz
  uint16_t audioFormat = header[20] | (header[21] << 8);
  uint16_t numChannels = header[22] | (header[23] << 8);
  uint32_t sampleRate  = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
  uint16_t bitsPerSample = header[34] | (header[35] << 8);

  if (audioFormat != 1) { Serial.println("Format non PCM !"); return false; }
  if (numChannels != 2) { Serial.println("Pas stereo !"); return false; }
  if (sampleRate != 44100) { Serial.println("Pas 44100Hz !"); return false; }
  if (bitsPerSample != 16) { Serial.println("Pas 16 bits !"); return false; }

  return true;
}

// ===================
//      SETUP
// ===================
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n==== Initialisation ES8388 ====");
  if (!es.init()) {
    Serial.println("⚠️ ERREUR ES8388");
    while(1);
  }



  pinMode(39, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  pinMode(19, OUTPUT);
  pinMode(22, OUTPUT);
  Serial.println("Read Reg ES8388 : ");
  if (!es.init()) Serial.println("Init Fail");
  es.inputSelect(IN2);
  es.setInputGain(8);
  es.mixerSourceSelect(MIXADC, MIXADC);
  es.mixerSourceControl(DACOUT);
  es.outputSelect(OUT2);        // casque
  es.setOutputVolume(80);       // volume raisonnable

  Serial.println("✓ ES8388 OK");

  // ====== I2S ======
  Serial.println("Init I2S...");
  i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &i2s_pins);
  Serial.println("✓ I2S OK");

  // ====== SD ======
  Serial.println("Init SD...");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("⚠️ SD KO");
    while(1);
  }
  Serial.println("✓ SD OK");

  // ====== Lecture fichier WAV ======
  File f = SD.open("/test.wav");
  if (!f) {
    Serial.println("⚠️ test.wav introuvable");
    while(1);
  }

  Serial.println("Fichier ouvert.");

  if (!skipWavHeader(f)) {
    Serial.println("⚠️ Format WAV incompatible");
    while(1);
  }

  Serial.println("Header WAV OK — lecture en cours...");

  uint8_t buffer[1024];
  size_t bytesRead;
  size_t bytesWritten;

  // ====== Lecture boucle ======
  while ((bytesRead = f.read(buffer, sizeof(buffer))) > 0) {
    i2s_write(I2S_NUM_0, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
  }

  Serial.println("Fin lecture WAV.");
}

// ===================
//       LOOP
// ===================
void loop() {
}
