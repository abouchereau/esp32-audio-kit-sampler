#include <Wire.h>
#include <Keypad_I2C.h>
#include <Keypad.h>
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include <math.h>
#include "driver/i2s.h"
#include "ES8388.h"

// --------------------------------------------------
// Configuration matérielle
// --------------------------------------------------

#define SD_CS       13
#define SD_SCK      14
#define SD_MISO     2
#define SD_MOSI     15

#define I2C_ADDR    0x20
#define SDA_CODEC   33
#define SCL_CODEC   32
#define SDA_KEYS    21
#define SCL_KEYS    22

TwoWire I2C_CODEC = Wire;
TwoWire I2C_KEYS  = Wire1;

ES8388* es = nullptr;
Preferences fadePrefs;

// --------------------------------------------------
// I2S
// --------------------------------------------------

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

// --------------------------------------------------
// Lecture audio
// --------------------------------------------------

File wavFile;

bool playing = false;

uint32_t bytesRemaining = 0;

char setList = 'A';
String nextTrack = "";

float volumeControl = 1.0f;

int targetVolume = 90;
int volumeGeneral = 90;
float volumeGeneralFloat = 0.9f;

bool pendingStart = false;
bool pendingStop = false;

// --------------------------------------------------
// Clavier 4 x 4
// --------------------------------------------------

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

// Brochage PCF8574
byte rowPins[ROWS] = {3, 2, 1, 0};
byte colPins[COLS] = {7, 6, 5, 4};

Keypad_I2C keypad(
    makeKeymap(keys),
    rowPins,
    colPins,
    ROWS,
    COLS,
    I2C_ADDR,
    1,
    &I2C_KEYS
);

// --------------------------------------------------
// Fade out utilisateur : réglage persistant
// --------------------------------------------------

uint8_t fadeOutSeconds = 5;

bool userFadeActive = false;
uint32_t userFadeStartMs = 0;

float userFadeStartGain = 1.0f;
float userFadeGain = 1.0f;

// Gestion de # maintenu
bool hashDown = false;
bool hashUsedForSetting = false;

// --------------------------------------------------
// Mémoire Flash
// --------------------------------------------------

void loadFadeOutSetting() {
    fadePrefs.begin("audio", true);

    uint8_t saved = fadePrefs.getUChar("fadeSec", 5);

    fadePrefs.end();

    if (saved >= 1 && saved <= 9) {
        fadeOutSeconds = saved;
    } else {
        fadeOutSeconds = 5;
    }

    Serial.printf(
        "Duree fade out : %u seconde(s)\n",
        fadeOutSeconds
    );
}

void saveFadeOutSetting(uint8_t seconds) {
    if (seconds < 1 || seconds > 9) {
        return;
    }

    if (seconds == fadeOutSeconds) {
        Serial.printf(
            "Fade out deja regle sur %u s\n",
            fadeOutSeconds
        );
        return;
    }

    fadePrefs.begin("audio", false);

    size_t written = fadePrefs.putUChar("fadeSec", seconds);

    fadePrefs.end();

    if (written == sizeof(uint8_t)) {
        fadeOutSeconds = seconds;

        Serial.printf(
            "Nouveau fade out : %u seconde(s), memorise\n",
            fadeOutSeconds
        );
    } else {
        Serial.println("Erreur sauvegarde fade out");
    }
}

// --------------------------------------------------
// Fondu de transition historique
// Ne pas confondre avec le fade utilisateur
// --------------------------------------------------

void updateVolumeFade() {
    // Fade IN
    if (volumeGeneral < targetVolume) {
        switch (volumeGeneral) {
            case 0:
                volumeGeneral = 2;
                break;

            case 2:
                volumeGeneral = 10;
                break;

            case 10:
                volumeGeneral = 90;
                break;

            default:
                break;
        }

        volumeGeneralFloat = 0.01f * volumeGeneral;
    }

    // Fade OUT de transition
    else if (volumeGeneral > targetVolume) {
        switch (volumeGeneral) {
            case 90:
                volumeGeneral = 10;
                break;

            case 10:
                volumeGeneral = 2;
                break;

            case 2:
                volumeGeneral = 0;
                break;

            default:
                break;
        }

        volumeGeneralFloat = 0.01f * volumeGeneral;
    }
}

// --------------------------------------------------
// Fade out utilisateur exponentiel
// --------------------------------------------------

void cancelUserFadeOut() {
    userFadeActive = false;
    userFadeGain = 1.0f;
    userFadeStartGain = 1.0f;
}

void startUserFadeOut() {
    if (!playing || pendingStart || pendingStop) {
        return;
    }

    userFadeStartMs = millis();
    userFadeStartGain = userFadeGain;

    userFadeActive = true;

    Serial.printf(
        "Fade out utilisateur demarre : %u s\n",
        fadeOutSeconds
    );
}

void updateUserFadeOut() {
    if (!userFadeActive) {
        return;
    }

    if (!playing || pendingStart || pendingStop) {
        cancelUserFadeOut();
        return;
    }

    uint32_t elapsed = millis() - userFadeStartMs;

    float durationMs = fadeOutSeconds * 1000.0f;

    float progress = (float)elapsed / durationMs;

    if (progress >= 1.0f) {
        userFadeGain = 0.0f;
        userFadeActive = false;

        // Arrêt en fin de fade utilisateur
        stopPlayback();

        // Réinitialisation pour la prochaine piste
        userFadeGain = 1.0f;
        userFadeStartGain = 1.0f;

        volumeGeneral = 0;
        volumeGeneralFloat = 0.0f;
        targetVolume = 90;

        pendingStart = false;
        pendingStop = false;

        Serial.println("Fade out termine");

        return;
    }

    // Courbe exponentielle en amplitude.
    // 0 dB au départ, environ -60 dB à la fin.
    float exponentialGain = powf(0.001f, progress);

    userFadeGain = userFadeStartGain * exponentialGain;
}

// --------------------------------------------------
// Lecture des fichiers WAV
// --------------------------------------------------

bool skipWavHeader(File &f) {
    uint8_t h[44];

    if (f.size() < 44) {
        return false;
    }

    if (f.read(h, 44) != 44) {
        return false;
    }

    if (strncmp((char*)h, "RIFF", 4) != 0) {
        return false;
    }

    if (strncmp((char*)h + 8, "WAVE", 4) != 0) {
        return false;
    }

    uint16_t fmt = h[20] | (h[21] << 8);
    uint16_t ch  = h[22] | (h[23] << 8);

    uint32_t sr =
        h[24] |
        (h[25] << 8) |
        (h[26] << 16) |
        (h[27] << 24);

    uint16_t bits = h[34] | (h[35] << 8);

    if (fmt != 1 ||
        ch < 1 ||
        ch > 2 ||
        sr != 44100 ||
        bits != 16) {
        return false;
    }

    return true;
}

void startPlayback(String filename) {
    // Toujours repartir avec le gain utilisateur neutre
    cancelUserFadeOut();

    Serial.println("Demarrage lecture " + filename + ".wav");

    if (wavFile) {
        wavFile.close();
    }

    playing = false;

    wavFile = SD.open("/" + filename + ".wav");

    if (!wavFile) {
        Serial.println("Impossible d'ouvrir " + filename + ".wav");

        wavFile = SD.open("/file_not_found.wav");

        if (!wavFile) {
            Serial.println("Impossible d'ouvrir file_not_found.wav");
            return;
        }
    }

    if (!skipWavHeader(wavFile)) {
        Serial.println("Format WAV incompatible");

        wavFile.close();

        wavFile = SD.open("/bad_file_format.wav");

        if (!wavFile) {
            Serial.println("Impossible d'ouvrir bad_file_format.wav");
            return;
        }

        if (!skipWavHeader(wavFile)) {
            Serial.println("Fichier de secours incompatible");
            wavFile.close();
            return;
        }
    }

    bytesRemaining = wavFile.size() - wavFile.position();

    // Ne pas écraser le volume courant ici :
    // la transition historique contrôle le fade IN.
    playing = true;
}

void stopPlayback() {
    if (playing) {
        Serial.println("Lecture stoppee.");

        wavFile.close();
        playing = false;

        uint8_t zero[512] = {0};
        size_t written = 0;

        for (int i = 0; i < 12; i++) {
            i2s_write(
                I2S_NUM_0,
                zero,
                sizeof(zero),
                &written,
                portMAX_DELAY
            );
        }
    }

    cancelUserFadeOut();
}

// --------------------------------------------------
// Gestion des touches ordinaires
// --------------------------------------------------

void manageKeyPad(char key) {
    Serial.printf("Touche : %c\n", key);

    switch (key) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':

            cancelUserFadeOut();

            nextTrack = String(setList) + String(key);

            pendingStop = false;

            if (playing) {
                targetVolume = 0;
                pendingStart = true;
            } else {
                pendingStart = false;

                // Le fade IN historique repart du silence
                volumeGeneral = 0;
                volumeGeneralFloat = 0.0f;
                targetVolume = 90;

                startPlayback(nextTrack);
            }

            break;

        case 'A':
        case 'B':
        case 'C':
        case 'D':

            setList = key;

            Serial.printf("Liste selectionnee : %c\n", setList);

            break;

        case '*':

            cancelUserFadeOut();

            pendingStart = false;
            pendingStop = true;
            targetVolume = 0;

            break;

        default:
            break;
    }
}

// --------------------------------------------------
// Gestion des événements clavier : PRESSED/RELEASED
// --------------------------------------------------

void pollKeypad() {
    if (!keypad.getKeys()) {
        return;
    }

    char changedKeys[LIST_MAX];
    KeyState changedStates[LIST_MAX];
    int count = 0;

    // Copier les événements avant de les traiter
    for (int i = 0; i < LIST_MAX; i++) {
        if (keypad.key[i].stateChanged) {
            changedKeys[count] = keypad.key[i].kchar;
            changedStates[count] = keypad.key[i].kstate;
            count++;
        }
    }

    // 1. Détecter d'abord l'appui sur #
    for (int i = 0; i < count; i++) {
        if (changedKeys[i] == '#' &&
            changedStates[i] == PRESSED) {

            hashDown = true;
            hashUsedForSetting = false;

            Serial.println("# enfonce");
        }
    }

    // 2. Traiter les appuis sur les autres touches
    for (int i = 0; i < count; i++) {
        if (changedStates[i] != PRESSED) {
            continue;
        }

        char k = changedKeys[i];

        if (k == '#') {
            continue;
        }

        // # maintenu + chiffre 1..9 = réglage du fade
        if (hashDown && k >= '1' && k <= '9') {
            saveFadeOutSetting(k - '0');
            hashUsedForSetting = true;
        }

        // Ne pas lancer la piste 0 par inadvertance
        // lorsque # est maintenu
        else if (hashDown && k == '0') {
            hashUsedForSetting = true;
        }

        else {
            manageKeyPad(k);
        }
    }

    // 3. Traiter le relâchement de #
    for (int i = 0; i < count; i++) {
        if (changedKeys[i] == '#' &&
            changedStates[i] == RELEASED) {

            Serial.println("# relache");

            if (!hashUsedForSetting) {
                startUserFadeOut();
            }

            hashDown = false;
            hashUsedForSetting = false;
        }
    }
}

// --------------------------------------------------
// Initialisation
// --------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(300);

    loadFadeOutSetting();

    I2C_CODEC.begin(SDA_CODEC, SCL_CODEC, 400000);
    I2C_KEYS.begin(SDA_KEYS, SCL_KEYS, 100000);

    delay(10);

    i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &i2s_pins);

    uint8_t silence[256] = {0};
    size_t written = 0;

    i2s_write(
        I2S_NUM_0,
        silence,
        sizeof(silence),
        &written,
        50
    );

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    if (!SD.begin(SD_CS)) {
        Serial.println("Erreur initialisation carte SD");
    }

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

    volumeGeneral = 0;
    volumeGeneralFloat = 0.0f;
    targetVolume = 90;

    startPlayback("boot");
}

// --------------------------------------------------
// Boucle principale
// --------------------------------------------------

void loop() {
    // Clavier : détection appui et relâchement
    pollKeypad();

    // Fondu de transition entre pistes
    updateVolumeFade();

    // Fondu utilisateur indépendant
    updateUserFadeOut();

    // Lecture audio
    if (playing) {
        uint8_t buffer[1024];

        size_t n = wavFile.read(buffer, sizeof(buffer));

        if (n > 0) {
            int16_t* samples = (int16_t*)buffer;
            int sampleCount = n / 2;

            uint32_t remainingBeforeRead = bytesRemaining;

            for (int i = 0; i < sampleCount; i++) {
                float sampleGain =
                    volumeGeneralFloat *
                    volumeControl *
                    userFadeGain;

                // Fondu final anti-craquement indépendant :
                // atténuation sur les derniers 3000 octets.
                uint32_t bytesFromEnd =
                    remainingBeforeRead - (uint32_t)(i * 2);

                if (bytesFromEnd < 3000) {
                    float tailGain = (float)bytesFromEnd / 3000.0f;

                    if (tailGain < 0.0f) {
                        tailGain = 0.0f;
                    }

                    sampleGain *= tailGain;
                }

                float value = (float)samples[i] * sampleGain;

                if (value > 32767.0f) {
                    value = 32767.0f;
                } else if (value < -32768.0f) {
                    value = -32768.0f;
                }

                samples[i] = (int16_t)value;
            }

            size_t written = 0;

            i2s_write(
                I2S_NUM_0,
                buffer,
                n,
                &written,
                portMAX_DELAY
            );

            if (n >= bytesRemaining) {
                bytesRemaining = 0;
            } else {
                bytesRemaining -= n;
            }
        }
        else {
            stopPlayback();

            // Préparer une lecture ultérieure
            volumeGeneral = 0;
            volumeGeneralFloat = 0.0f;
            targetVolume = 90;
        }
    }

    // Changement de piste après le petit fade OUT
    if (pendingStart && volumeGeneral == 0) {
        stopPlayback();

        pendingStart = false;
        pendingStop = false;

        volumeGeneral = 0;
        volumeGeneralFloat = 0.0f;
        targetVolume = 90;

        startPlayback(nextTrack);
    }

    // STOP après le petit fade OUT
    if (pendingStop && volumeGeneral == 0) {
        stopPlayback();

        pendingStop = false;
        pendingStart = false;

        volumeGeneral = 0;
        volumeGeneralFloat = 0.0f;
        targetVolume = 90;
    }
}
