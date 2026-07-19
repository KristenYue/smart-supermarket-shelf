#pragma once

// Copy this file to secrets.h and replace the placeholders.
// Never commit secrets.h.
#define SECRET_SSID "YOUR_WIFI_NAME"
#define SECRET_PASS "YOUR_WIFI_PASSWORD"

#define AWS_CLIENT_ID "smart-shelf-01"
#define AWS_IOT_ENDPOINT "YOUR_ENDPOINT-ats.iot.eu-west-2.amazonaws.com"
#define AWS_IOT_PORT 8883

// The public device certificate stored in the ATECC608A certificate slot.
// The private key remains inside the secure element.
const char DEVICE_CERT[] = R"CERT(
-----BEGIN CERTIFICATE-----
PASTE_DEVICE_CERTIFICATE_HERE
-----END CERTIFICATE-----
)CERT";

