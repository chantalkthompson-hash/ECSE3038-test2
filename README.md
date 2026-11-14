# ECSE3038 Test 2: The Bathroom Fan
An IoT bathroom humidity monitoring and extractor fan control system built with a FastAPI + MongoDB Atlas backend, an MQTT broker, and an ESP32 microcontroller with a DHT22 sensor simulated in Wokwi.

# How to Run the API
1. Navigate to the `api/` directory:
   ```bash
   cd api

2. Create and activate a Python virtual environment, then install dependencies:

python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

3. Create a .env file in api/ containing your MongoDB connection string and MQTT name:

MONGODB_URI=<your_mongodb_connection_string>
MQTT_NAME=<your_mqtt_name>

4. Start the FastAPI server:

uvicorn app:app --host 127.0.0.1 --port 8000

# How to Run the Board
1. Open the board/ folder in VS Code with the PlatformIO and Wokwi extensions installed.

2. Create board/include/secrets.h containing #define DEVICE_KEY "" so the project compiles.

3. Build the project and start the Wokwi simulation from diagram.json to view the board's MAC address in the Serial Monitor.

4. Register the board via POST /fans on the API documentation page (/docs) and paste the returned key into board/include/secrets.h:

C++
#pragma once
#define DEVICE_KEY "<your_returned_api_key>"

5. Rebuild and restart the Wokwi simulation. Use PUT /fans/{mac}/settings on the API to control the fan mode (auto, on, off) and max_humidity threshold.