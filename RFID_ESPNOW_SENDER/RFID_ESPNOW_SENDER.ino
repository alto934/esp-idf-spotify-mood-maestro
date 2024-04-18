#include <SPI.h>
#include <MFRC522.h>
#include <esp_now.h>
#include <WiFi.h>

#define SS_PIN 5
#define RST_PIN 0
#define RXp2 16 //  RX pin for Serial2
#define TXp2 17 // TX pin for Serial2

MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the MFRC522 class
MFRC522::MIFARE_Key key;

// ESP-NOW peer address
uint8_t peer_mac[6] = {0x40, 0x4C, 0xCA, 0x51, 0x3A, 0xB0};

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RXp2, TXp2); // Initialize Serial2 communication

  SPI.begin();
  rfid.PCD_Init();
  Serial.println(F("RFID reading via Serial2."));

  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  // Initialize and configure ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Add the peer to ESP-NOW
  esp_now_peer_info_t peer_info = {};
  memcpy(peer_info.peer_addr, peer_mac, 6);
  if (esp_now_add_peer(&peer_info) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
}


void loop() {
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    // Print UID to Serial (for logging)
    Serial.print("UID:");
    for (byte i = 0; i < rfid.uid.size; i++) {
      Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
      Serial.print(rfid.uid.uidByte[i], HEX);
    }
    Serial.println();

    // Print UID to Serial2 (for communication with external device)
    Serial2.print("UID:");
    for (byte i = 0; i < rfid.uid.size; i++) {
        Serial2.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
        Serial2.print(rfid.uid.uidByte[i], HEX);
    }
    Serial2.println();

    // Prepare the UID with spaces before each byte
    char formatted_uid[20]; // Assuming a maximum UID size of 8 bytes
    formatted_uid[0] = '\0'; // Start with an empty string
    for (byte i = 0; i < rfid.uid.size; i++) {
      char byte_str[4];
      sprintf(byte_str, " %02X", rfid.uid.uidByte[i]);
      strcat(formatted_uid, byte_str);
    }

    // Send the formatted UID via ESP-NOW
    esp_now_send(peer_mac, (uint8_t *)formatted_uid, strlen(formatted_uid));
    Serial.println(formatted_uid);


    // Halt PICC
    rfid.PICC_HaltA();
    // Stop encryption on PCD
    rfid.PCD_StopCrypto1();
  }

  delay(500);
}