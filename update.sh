echo "Starting OTA update..."
curl -X POST --data-binary @build/tesla-ambient-lighting.bin 192.168.4.1/ota