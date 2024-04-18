# esp-idf-spotify-mood-maestro
An IoT project integrating Spotify music streaming with ESP32-C6 microcontrollers. Users can select albums using RFID tags, and the ESP32-C6 modules play the playlist/ album using Spotify's API to enhance the listening experience with Mood Light visualisations.
# Try it yourself!
## Spotify ESP32-C6

### Steps

1. Create a project on Spotify's developer platform and obtain the client ID and client secret.

2. Assuming you have configured the ESP-IDF extension properly on VSCode, open the `spotify-rfid-player` folder in your workspace.

3. Fill in the appropriate Wi-Fi SSID and password credentials.

4. Fill in your client ID and secret. Ideally, you should store these securely using NVS (Non-Volatile Storage) Flash. However, for a production code, you may hard-code it into the code. Remember to keep the client secret secure.

5. Configure your redirect URL. It should be of the form `http://ESP_IP_ADDRESS/` in the code. This is necessary to make the authorization request and get the access token for the Spotify API. Remember to update this redirect URL on the Spotify developer dashboard of the project you made in step 1.

6. Using `menuconfig`, edit the configurations of the ESP for HTTPS to allow insecure requests and TLS to skip server verification.

7. Build, flash, and monitor the project!

## RFID Reader ESP32-WROOM-32D (Running Arduino)

### Steps

1. Open the sketch in `RFID_ESPNOW_SENDER` in the Arduino IDE.

2. Select the board as `DOIT ESP32 DEVKIT 1` and select the corresponding COM Port.

3. Connect the MFRC22 to the ESP32. The connection diagram can be found in the Report PDF. Also, make the serial connection to the Spotify ESP32-C6 [Rx1 to pin 4 of Spotify ESP].

4. Upload and open the Serial Monitor.

5. Tap an RFID card on the reader and check if the UID is being read and sent successfully.

## LED ESP32-C6

### Steps

1. Assuming you have configured the ESP-IDF extension properly on VSCode, open the `led_strip` folder in your workspace.

2. Make the necessary connections from the LED Strip to the ESP.

3. Build, Flash, and Monitor.

4. Take note of the MAC Address and remember to put that in the RFID Arduino code.

5. You can change the durations of the fade as well as the colors associated with each mood.

## Using the whole player:
1. You will have to click the Authorization link that is printed in the Monitor tab of the Spotify ESP32-C6. It will open the Spotify Auth Page in your browser. Click Agree. Once page redirects and shows `Authorization Received` you can close the page and use the player.
2. The most pressing improvement required for the project is the automatic refreshing of the access token which will remove the need for repeated authorization.
