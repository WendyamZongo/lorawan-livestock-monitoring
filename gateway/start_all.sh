#!/bin/bash
# Auto-start script for LoRaWAN Cow Monitoring System
# Place this file at /home/yam/start_all.sh

# Start ChirpStack
cd /home/yam/chirpstack-docker && docker compose up -d
sleep 10

# Start packet forwarder
cd /home/yam/sx1302_hal/packet_forwarder
nohup ./lora_pkt_fwd > /dev/null 2>&1 &

# Start temperature dashboard
nohup python3 /home/yam/dashboard/app.py > /dev/null 2>&1 &

# Start PIR motion dashboard
nohup python3 /home/yam/dashboard2/app.py > /dev/null 2>&1 &

echo "✅ All services started!"
echo "Temperature : http://yam.local:5000"
echo "PIR Motion  : http://yam.local:5001"
