#include "radio.h"

#if defined(_RETRO_)

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Arduino.h> // _DSB()
#include <ArduinoJson.h>
#include <Base64.h>

#include "sensors_pinout.h"
#include "model.h"


/* 
{"pedals":{"tps1":23,"tps2":23,"brake":0,"apps_plaus":true,"brake_plaus":true},
"suspensions":{"front_sx":23,"front_dx":23,"retro_sx":23,"retro_dx":23},
"wheels":{"front_sx":23,"front_dx":23,"retro_sx":23,"retro_dx":23},
"accelerometers":{"acc_x":23,"acc_z":23}
}
*/
// JSON_BUFFER_SIZE generated by https://arduinojson.org/assistant/
#define JSON_BUFFER_SIZE      JSON_OBJECT_SIZE(2) + 3 * JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 219

#define CIPHER_MAX_LENGTH     1024 // multiplo di 16

#define IV_LEN                AES_KEYLEN  

volatile bool radio_transmit = false;

char key[AES_KEYLEN] = {   0x8e, 0x73, 0xb0, 0xf7, 0xda, 0x0e, 0x64, 0x52, 
                                        0xc8, 0x10, 0xf3, 0x2b, 0x80, 0x90, 0x79, 0xe5, 
                                        0x62, 0xf8, 0xea, 0xd2, 0x52, 0x2c, 0x6b, 0x7b }; // 24 bytes
char iv[IV_LEN + 1];
char cipher[CIPHER_MAX_LENGTH + 1] = {0};

RF24 radio(RADIO_CE_PIN, RADIO_CSN_PIN);
const uint64_t pipe = 0xE8E8F0F0E1LL;

__attribute__((__inline__))
volatile char generate_random_char() {
    while (!(TRNG->TRNG_ISR & TRNG_ISR_DATRDY));
    return (volatile char) TRNG->TRNG_ODATA;
}

// Generate a random initialization vector
void generate_iv(char* buffer, uint16_t len) {
    uint16_t i = 0;
    for (; i < len; i++)
        buffer[i] = generate_random_char();
}

__attribute__((__inline__))
void pkcs7_padding(char* buffer, uint16_t plain_len, uint16_t buffer_len) {
    unsigned char padding = buffer_len - plain_len;
    memset(buffer + plain_len, padding, padding);
}

// ISO/IEC 7816-4 byte padding
__attribute__((__inline__))
void byte_padding(char* buffer, uint16_t plain_len, uint16_t buffer_len) {
    unsigned char padding = buffer_len - plain_len;
    if (padding) {
        buffer[plain_len] = '0x80';
        memset(buffer + plain_len + 1, '0x00', padding - 1);
    }
}

void encrypt_model(char* buffer, char* iv, uint16_t plain_len, uint16_t buffer_len) {
	
	struct AES_ctx ctx;

    //byte_padding(buffer, plain_len, buffer_len);

    AES_init_ctx_iv(&ctx, (const uint8_t*) key, (const uint8_t*) iv);

    AES_CTR_xcrypt_buffer(&ctx, (uint8_t*) buffer, buffer_len);
}

__attribute__((__inline__)) void radio_init() {
    pmc_enable_periph_clk(ID_TRNG);
    TRNG->TRNG_IDR = 0xFFFFFFFF;
    TRNG->TRNG_CR = TRNG_CR_KEY(0x524e47) | TRNG_CR_ENABLE;

    // init radio
    radio.begin();
    radio.openWritingPipe(pipe);
    radio.stopListening();
}

void radio_send_model() {
    StaticJsonBuffer<JSON_BUFFER_SIZE>  jsonBuffer;

    uint16_t                            model_len;
    
    JsonObject&   root = jsonBuffer.createObject();

    JsonObject& pedals = root.createNestedObject("pedals");
    JsonObject& suspensions = root.createNestedObject("suspensions");
    JsonObject& wheels = root.createNestedObject("wheels");
    JsonObject& accelerometers = root.createNestedObject("accelerometers");

    pedals["tps1"] = tps1_percentage;
    pedals["tps2"] = tps1_percentage;
    pedals["brake"] = brake_percentage;
    pedals["apps_plaus"] = apps_plausibility;
    pedals["brake_plaus"] = brake_plausibility;

    suspensions["front_sx"] = fr_sx_susp;
    suspensions["front_dx"] = fr_dx_susp;
    suspensions["retro_sx"] = rt_sx_susp;
    suspensions["retro_dx"] = rt_dx_susp;

    // Data Synchronization Barrier 
    // ensures all explicit data transfers before the DSB are complete before 
    // any instruction after the DSB is executed.
    __DSB(); 
    wheels["front_sx"] = get_fr_sx_rpm();

    __DSB();
    wheels["front_dx"] = get_fr_dx_rpm();

    __DSB();
    wheels["retro_sx"] = get_rt_sx_rpm();

    __DSB();
    wheels["retro_dx"] = get_rt_dx_rpm();

    accelerometers["acc_x"] = acc_x_value;
    accelerometers["acc_z"] = acc_z_value;

    memset(cipher, 0, CIPHER_MAX_LENGTH);
    root.printTo(cipher);

    model_len = strlen(cipher);

    generate_iv(iv, IV_LEN);
    iv[IV_LEN] = '\0';

    pkcs7_padding(cipher, model_len, CIPHER_MAX_LENGTH);
    encrypt_model(cipher, iv, model_len, CIPHER_MAX_LENGTH);

    int encodedIvLength = Base64.encodedLength(IV_LEN);
    int encodedLength = Base64.encodedLength(CIPHER_MAX_LENGTH);
    char encodedString[encodedIvLength + encodedLength];

    Base64.encode(encodedString, iv, IV_LEN);
    Base64.encode(encodedString + encodedIvLength, cipher, CIPHER_MAX_LENGTH);

    radio.write(encodedString, encodedIvLength + encodedLength);
}

#endif
