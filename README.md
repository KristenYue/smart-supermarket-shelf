# Smart Supermarket Shelf System

This repository is a cleaned and security-hardened implementation of the two-node Smart Shelf prototype.

## Architecture

- **Shelf 1 (`smart-shelf-01`)**: MKR ENV Shield, ultrasonic sensor and light sensor. It reports temperature, humidity, pressure, customer presence, dwell time and product interactions.
- **Shelf 2 (`smart-shelf-02`)**: HX711 load-cell interface, sound sensor on A0, light sensor on A1, assistance button on D4, LED on D3 and buzzer on D2.
- **AWS IoT Core**: the only MQTT broker. Devices publish telemetry/events/alerts and subscribe to command/config topics using TLS and their ATECC608A secure elements.
- **DynamoDB**: one telemetry table and one derived-analysis table, both keyed by `deviceID` and numeric `timestamp`.
- **Java cloud service**: performs rule-based analysis, serves the dashboard API and sends commands through AWS IoT Core. AWS credentials come from the default SDK credential chain and are never stored in browser code.
- **Dashboard**: polls the local Java API and stores the optional API token only in `sessionStorage`.

## MQTT contract

| Topic | Direction | Payload |
|---|---|---|
| `smartshelf/{deviceID}/telemetry` | device → cloud | JSON telemetry every 5 s |
| `smartshelf/{deviceID}/events` | device → cloud | state and engagement events |
| `smartshelf/{deviceID}/alerts` | device → cloud | alert activation/recovery |
| `smartshelf/cmd/{deviceID}` | cloud → device | `ON`, `OFF`, `FLASH`, `AUTO`, or `ACK` |
| `smartshelf/{deviceID}/config` | cloud → device | JSON threshold changes |

## Arduino libraries

Install these through Arduino IDE Library Manager:

- WiFiNINA
- ArduinoMqttClient
- ArduinoBearSSL
- ArduinoECCX08
- Arduino_MKRENV (Shelf 1)
- DFRobot_HX711_I2C (Shelf 2)

Use an Arduino MKR WiFi 1010 board package and update the NINA firmware with the Arduino Firmware Updater before testing TLS.

## Device configuration

For each sketch:

1. Copy `secrets.example.h` to `secrets.h` in the same directory.
2. Enter the Wi-Fi settings, AWS IoT endpoint, client ID and matching public device certificate.
3. Provision a different AWS IoT thing/certificate for each physical board.
4. Attach the generated `SmartShelfDevicePolicy` to both device certificates.
5. Never commit `secrets.h`. It is blocked by the root `.gitignore`.

The firmware assumes that the ATECC608A private key/certificate slot has already been provisioned with ArduinoECCX08/AWS IoT tooling.

## Create AWS resources

Deploy `infra/cloudformation.yaml` in `eu-west-2` using the AWS CloudFormation console or CLI. It creates:

- `SmartShelfTelemetry`
- `SmartShelfAnalysis`
- an IoT Rule that writes telemetry to DynamoDB
- the least-privilege device IoT policy

Certificate creation and attachment remain manual because each certificate belongs to a physical secure element.

## Build and run the cloud service

Requirements: Java 17, Maven 3.9+, and AWS credentials supplied through an AWS profile, IAM role or temporary environment credentials.

Do **not** put an AWS access key in this repository or in the HTML file.

PowerShell example:

```powershell
cd cloud-service
mvn clean package

$env:AWS_REGION = "eu-west-2"
$env:AWS_PROFILE = "your-local-profile"
$env:AWS_IOT_DATA_ENDPOINT = "https://YOUR_ENDPOINT-ats.iot.eu-west-2.amazonaws.com"
$env:API_TOKEN = "choose-a-long-random-value"
java -jar target/smart-shelf-cloud-service-1.0.0.jar
```

Open `http://127.0.0.1:8080`, enter the same API token, and click **Apply token**.

The IAM identity used by the service needs only:

- `dynamodb:Query` on both tables
- `dynamodb:PutItem` on `SmartShelfAnalysis`
- `iot:Publish` on `smartshelf/cmd/*`

## Thresholds and corrected behavior

- Shelf 1 environment: 2–8 °C and 30–70% RH with a 30-second debounce.
- Shelf 1 visitor distance: at most 80 cm.
- Shelf 1 interaction light change: at least 60 ADC units.
- Shelf 2 state priority: manual assistance → obstruction → disturbance → out of stock → low stock → normal.
- Shelf 2 obstruction: light below 100.
- Shelf 2 disturbance: sound above 150.
- Shelf 2 low/out of stock: at most 25 g / at most 1 g.
- The D4 button uses `INPUT_PULLUP`, so a pressed button correctly reads `LOW`.
- Shelf 2 aggregates minimum light and maximum sound over each 5-second telemetry window.

## Before publishing to GitHub

1. Delete or rotate every AWS key that previously appeared in the original HTML files.
2. Confirm `secrets.h`, `SECRET_SSID.txt`, `.env`, private keys and build output are absent.
3. Run `git status` and `git diff --cached` before every push.
4. Add the report to `docs/` only if all team members agree to publish the names and email addresses shown in it.

The old `Back.ino` is intentionally not included. Its public unauthenticated MQTT broker and hard-coded Wi-Fi credentials have been replaced by authenticated AWS IoT command subscriptions inside each shelf sketch.

