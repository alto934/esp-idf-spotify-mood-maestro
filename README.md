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

6. Using `menuconfig`, edit the configurations for HTTPS to allow insecure requests and TLS to skip server verification.

7. Build, flash, and monitor the project!
