"""
Temperature Monitoring Dashboard
Port: 5000
Displays real-time cow temperature + GPS data from LoRaWAN nodes
"""

from flask import Flask, render_template_string
import paho.mqtt.client as mqtt
import json
import threading
import os
import csv
from datetime import datetime

app = Flask(__name__)

# Store latest device data
device_data = {}

# MQTT Configuration
MQTT_HOST = "localhost"
MQTT_PORT = 1883

# Application ID for temperature monitoring
TEMP_APP_ID = "98829cab-b99b-46b8-9c80-924b02a33e40"

HTML = """
<!DOCTYPE html>
<html>
<head>
    <title>🐄 Cow Temperature Dashboard</title>
    <meta http-equiv="refresh" content="10">
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f0f0; }
        h1 { color: #333; }
        table { width: 100%; border-collapse: collapse; background: white; }
        th { background: #2196F3; color: white; padding: 12px; text-align: left; }
        td { padding: 10px; border-bottom: 1px solid #ddd; }
        tr:hover { background: #f5f5f5; }
        .temp-low      { font-weight: bold; color: #0099ff; }
        .temp-normal   { font-weight: bold; color: #00cc44; }
        .temp-warning  { font-weight: bold; color: #ffaa00; }
        .temp-critical { font-weight: bold; color: #ff0000; animation: blink 1s infinite; }
        @keyframes blink { 0%{opacity:1;} 50%{opacity:0.3;} 100%{opacity:1;} }
        .legend { margin: 10px 0; padding: 10px; background: white; border-radius: 5px; }
        .legend span { margin-right: 20px; }
    </style>
</head>
<body>
    <h1>🐄 Cow Temperature Monitoring Dashboard</h1>
    <p>Auto-refreshes every 10 seconds</p>

    <div class="legend">
        <span style="color:#0099ff">🔵 Too Low (&lt;35°C)</span>
        <span style="color:#00cc44">🟢 Normal (35-38.5°C)</span>
        <span style="color:#ffaa00">🟡 Warning (38.5-39.5°C)</span>
        <span style="color:#ff0000">🔴 FEVER (&gt;39.5°C)</span>
    </div>

    <table>
        <tr>
            <th>Device</th>
            <th>Temperature</th>
            <th>Location</th>
            <th>Last Seen</th>
        </tr>
        {% for name, d in devices.items() %}
        <tr>
            <td>{{ name }}</td>
            {% set t = d.temperature %}
            {% if t == 'N/A' %}
            <td>N/A</td>
            {% elif t|float < 35.0 %}
            <td class="temp-low">{{ t }} °C 🔵 Too Low</td>
            {% elif t|float <= 38.5 %}
            <td class="temp-normal">{{ t }} °C 🟢 Normal</td>
            {% elif t|float <= 39.5 %}
            <td class="temp-warning">{{ t }} °C 🟡 Warning</td>
            {% else %}
            <td class="temp-critical">{{ t }} °C 🔴 FEVER !</td>
            {% endif %}
            {% if d.latitude != 0.0 and d.longitude != 0.0 %}
            <td><a href="https://www.google.com/maps?q={{ d.latitude }},{{ d.longitude }}" target="_blank">📍 {{ d.latitude }}, {{ d.longitude }}</a></td>
            {% else %}
            <td>No GPS fix</td>
            {% endif %}
            <td>{{ d.last_seen }}</td>
        </tr>
        {% endfor %}
    </table>
</body>
</html>
"""

def on_app_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload)
        dev_name = payload.get("deviceInfo", {}).get("deviceName", "unknown")
        obj = payload.get("object", {})
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        temp = obj.get("temperature", "N/A")
        lat  = obj.get("latitude", 0.0)
        lon  = obj.get("longitude", 0.0)

        device_data[dev_name] = {
            "temperature": temp,
            "latitude": lat,
            "longitude": lon,
            "last_seen": now
        }

        # Save to CSV
        csv_file = "/home/yam/data.csv"
        file_exists = os.path.isfile(csv_file)
        with open(csv_file, "a", newline="") as f:
            writer = csv.writer(f)
            if not file_exists:
                writer.writerow(["Time", "Device", "Temperature", "Latitude", "Longitude"])
            writer.writerow([now, dev_name, temp, lat, lon])

    except Exception as e:
        print(f"Error: {e}")

def start_mqtt():
    client = mqtt.Client()
    client.on_message = on_app_message
    client.connect(MQTT_HOST, MQTT_PORT)
    client.subscribe(f"application/{TEMP_APP_ID}/device/+/event/up")
    client.loop_forever()

@app.route('/')
def index():
    return render_template_string(HTML, devices=device_data)

if __name__ == '__main__':
    t = threading.Thread(target=start_mqtt)
    t.daemon = True
    t.start()
    app.run(host='0.0.0.0', port=5000, debug=False)
