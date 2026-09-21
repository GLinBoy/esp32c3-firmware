# Deployment Guide

**Document Purpose:** Step-by-step instructions for deploying each component of the ESP32 IoT Ecosystem to development, staging, and production environments.

**Target Audience:** Developers, DevOps engineers, and future maintainers.

**Last Updated:** 2026-08-28

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Environment Setup](#environment-setup)
3. [Firmware Deployment](#firmware-deployment)
4. [Mobile App Deployment](#mobile-app-deployment)
5. [Backend Deployment](#backend-deployment)
6. [Admin Dashboard Deployment](#admin-dashboard-deployment)
7. [Infrastructure Provisioning](#infrastructure-provisioning)
8. [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Development Machine

- **OS:** Linux (Manjaro/Ubuntu), macOS, or Windows (WSL2)
- **Tools:**
  - Git
  - Node.js 18+ and npm
  - Python 3.8+
  - Firebase CLI: `npm install -g firebase-tools`

### Accounts & Credentials

- **Firebase:** Google account with Firebase project created
- **EMQX Cloud:** Account with Serverless deployment provisioned
- **GitHub/GitLab:** (Optional) For CI/CD and version control

### Hardware

- **For firmware:** ESP32-C3 development board, USB cable, serial driver
- **For app:** Android device (USB debugging enabled) or iOS device (Phase F)

---

## Environment Setup

### 1. Clone Repositories

```bash
# Firmware
cd ~/playground/PlatformIO/
git clone <firmware-repo-url> esp32c3-firmware
cd esp32c3-firmware

# Mobile App
cd ~/playground/Flutter/
git clone <app-repo-url> esp32_remote
cd esp32_remote

# Backend
cd ~/playground/Firebase/
git clone <backend-repo-url> esp32-iot-functions
cd esp32-iot-functions

# Admin Dashboard
cd ~/playground/Firebase/
git clone <admin-repo-url> esp32-iot-admin
cd esp32-iot-admin

# Infrastructure
cd ~/playground/Pulumi/
git clone <infra-repo-url> esp32-iot-infra
cd esp32-iot-infra
```

### 2. Install Dependencies

**Firmware (Phases A-D: PlatformIO):**

```bash
pip install platformio
```

**Firmware (Phase E+: ESP-IDF):**

```bash
# Install ESP-IDF v6.0.2
git clone --recursive --branch v6.0.2 https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf
./install.sh
source ./export.sh

# Install esp-matter
cd ~
git clone --depth 1 https://github.com/espressif/esp-matter.git
cd esp-matter
git submodule update --init --depth 1
cd ./connectedhomeip/connectedhomeip
./scripts/checkout_submodules.py --platform esp32 --shallow
cd ../..
./install.sh
source ./export.sh
```

**Mobile App:**

```bash
# Install Flutter SDK (follow: https://docs.flutter.dev/get-started/install/linux)
cd ~/playground/Flutter/esp32_remote/
flutter pub get
```

**Backend:**

```bash
cd ~/playground/Firebase/esp32-iot-functions/
cd functions
npm install
```

**Admin Dashboard:**

```bash
cd ~/playground/Firebase/esp32-iot-admin/
npm install  # or yarn install
```

**Infrastructure:**

```bash
# Install Pulumi
curl -fsSL https://get.pulumi.com | sh

cd ~/playground/Pulumi/esp32-iot-infra/
npm install  # if TypeScript project
```

---

## Firmware Deployment

### Phase A-D: PlatformIO Build

**1. Configure Serial Port:**

```bash
# Find your ESP32-C3 serial port
ls /dev/ttyACM* /dev/ttyUSB*
# Example output: /dev/ttyACM0

# Update platformio.ini if needed
nano platformio.ini
# Set: upload_port = /dev/ttyACM0
#      monitor_port = /dev/ttyACM0
```

**2. Build Firmware:**

```bash
cd ~/Projects/GLinBoy/GitHub/esp32c3-firmware
pio run
```

**3. Flash to Device:**

```bash
# Full clean build (recommended after config changes)
pio run --target fullclean
pio run --target upload

# Flash and monitor serial output
pio run --target upload && pio device monitor
```

**4. Verify:**

- Serial output shows: "No Wi-Fi credentials, entering BLE provisioning"
- LED blinking (indicates not connected to Wi-Fi)
- BLE advertising visible: `ESP32C3-<mac>`

### Phase E+: ESP-IDF Build

**1. Activate ESP-IDF Environment:**

```bash
source ~/esp-idf/export.sh
source ~/esp-matter/export.sh
```

**2. Configure:**

```bash
cd ~/Projects/GLinBoy/GitHub/esp32c3-firmware
idf.py menuconfig
# Navigate to Component config → Partition Table → Custom partition table CSV
# Ensure partition table is set correctly (partitions_2mb.csv or partitions_4mb.csv)
```

**3. Build:**

```bash
idf.py build
```

**4. Flash:**

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

**5. Verify:**

- Serial output shows Matter QR code (Phase E)
- Device advertises BLE for provisioning
- Device can be commissioned via chip-tool or Alexa/Google Home app

---

## Mobile App Deployment

### Development Build (Android)

**1. Configure Firebase:**

```bash
cd ~/playground/Flutter/esp32_remote/

# Install FlutterFire CLI
dart pub global activate flutterfire_cli

# Configure Firebase (Phase B+)
flutterfire configure
# Select your Firebase project
# Select Android platform
# Accept default package name
```

**2. Connect Android Device:**

```bash
# Enable USB debugging on Android device (Settings → Developer Options → USB Debugging)
adb devices
# Verify device is listed
```

**3. Run in Debug Mode:**

```bash
flutter run
# Select device if multiple connected
```

**4. Build Debug APK:**

```bash
flutter build apk --debug
# Output: build/app/outputs/flutter-apk/app-debug.apk
```

### Production Build (Android)

**1. Generate Signing Key:**

```bash
keytool -genkey -v -keystore ~/upload-keystore.jks -keyalg RSA -keysize 2048 -validity 10000 -alias upload
# Follow prompts, remember password
```

**2. Configure Signing:**

```bash
# Create android/key.properties
nano android/key.properties
```

Add:

```properties
storePassword=<password>
keyPassword=<password>
keyAlias=upload
storeFile=/home/<username>/upload-keystore.jks
```

Update `android/app/build.gradle`:

```gradle
def keystoreProperties = new Properties()
def keystorePropertiesFile = rootProject.file('key.properties')
if (keystorePropertiesFile.exists()) {
    keystoreProperties.load(new FileInputStream(keystorePropertiesFile))
}

android {
    ...
    signingConfigs {
        release {
            keyAlias keystoreProperties['keyAlias']
            keyPassword keystoreProperties['keyPassword']
            storeFile keystoreProperties['storeFile'] ? file(keystoreProperties['storeFile']) : null
            storePassword keystoreProperties['storePassword']
        }
    }
    buildTypes {
        release {
            signingConfig signingConfigs.release
        }
    }
}
```

**3. Build Release APK:**

```bash
flutter build apk --release
# Output: build/app/outputs/flutter-apk/app-release.apk
```

**4. Distribute:**

- Manual: Share APK file directly
- Google Play: Upload to Google Play Console (internal testing → production)

### iOS Build (Phase F)

**1. Prerequisites:**

- macOS with Xcode installed
- Apple Developer account ($99/year)
- Physical iOS device (Matter requires real hardware)

**2. Configure:**

```bash
cd ~/playground/Flutter/esp32_remote/
open ios/Runner.xcworkspace
# Xcode opens
# Select Runner target → Signing & Capabilities → Team → Select your Apple Developer account
```

**3. Build and Run:**

```bash
flutter run -d <ios-device-id>
```

**4. TestFlight/App Store:**

- Archive in Xcode: Product → Archive
- Upload to App Store Connect
- Submit for TestFlight or App Store review

---

## Backend Deployment

### Local Development (Firebase Emulator)

**1. Start Emulators:**

```bash
cd ~/playground/Firebase/esp32-iot-functions/
firebase emulators:start
```

**2. Emulator UI:**

- Open http://localhost:4000
- View Firestore data, Functions logs, Auth users

**3. Connect App to Emulators:**

```dart
// lib/main.dart (Flutter)
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Firebase.initializeApp(options: DefaultFirebaseOptions.currentPlatform);

  // Connect to emulators (development only)
  if (kDebugMode) {
    FirebaseFunctions.instance.useFunctionsEmulator('localhost', 5001);
    FirebaseFirestore.instance.useFirestoreEmulator('localhost', 8080);
    FirebaseAuth.instance.useAuthEmulator('localhost', 9099);
  }

  runApp(MyApp());
}
```

### Production Deployment

**1. Login to Firebase:**

```bash
firebase login
```

**2. Select Project:**

```bash
cd ~/playground/Firebase/esp32-iot-functions/
firebase use <project-id>
```

**3. Set Environment Variables:**

```bash
# EMQX credentials (Phase A)
firebase functions:config:set emqx.broker_url="<deployment-id>.emqxsl.com"
firebase functions:config:set emqx.api_url="<api-url>"
firebase functions:config:set emqx.api_key="<api-key>"
firebase functions:config:set emqx.username="backend"
firebase functions:config:set emqx.password="<backend-password>"
```

**4. Deploy Functions:**

```bash
# Deploy all Functions
firebase deploy --only functions

# Deploy specific function
firebase deploy --only functions:prepareDevice

# Deploy Functions + Firestore rules
firebase deploy --only functions,firestore
```

**5. Deploy Firestore Rules:**

```bash
firebase deploy --only firestore:rules
```

**6. Deploy Firestore Indexes:**

```bash
# Indexes are auto-detected from queries, or manually defined in firestore.indexes.json
firebase deploy --only firestore:indexes
```

**7. Verify Deployment:**

```bash
# View Functions logs
firebase functions:log

# Test function
firebase functions:shell
> prepareDevice({deviceId: "test123"})
```

### Backend MQTT Subscriber (Phase C)

**Development (Local):**

```bash
cd ~/playground/Firebase/esp32-iot-functions/functions/
node src/mqtt-subscriber.js
```

**Production (Cloud Run):**

```bash
# Build Docker image
cd ~/playground/Firebase/esp32-iot-functions/
docker build -t gcr.io/<project-id>/mqtt-subscriber -f Dockerfile.subscriber .

# Push to Google Container Registry
docker push gcr.io/<project-id>/mqtt-subscriber

# Deploy to Cloud Run
gcloud run deploy mqtt-subscriber \
  --image gcr.io/<project-id>/mqtt-subscriber \
  --platform managed \
  --region us-central1 \
  --allow-unauthenticated \
  --set-env-vars EMQX_BROKER_URL=<url>,EMQX_USERNAME=backend,EMQX_PASSWORD=<pwd>
```

---

## Admin Dashboard Deployment

### Development

**1. Start Dev Server:**

```bash
cd ~/playground/Firebase/esp32-iot-admin/
npm start
# React dev server runs on http://localhost:3000
```

### Production (Firebase Hosting)

**1. Build:**

```bash
cd ~/playground/Firebase/esp32-iot-admin/
npm run build
# Output: dist/ or build/ (depending on framework)
```

**2. Configure Firebase Hosting:**

```bash
firebase init hosting
# Select existing project
# Public directory: build (or dist)
# Single-page app: Yes
# GitHub Actions: No (for now)
```

**3. Deploy:**

```bash
firebase deploy --only hosting
```

**4. Verify:**

- Open: https://<project-id>.web.app
- Admin login should work
- Device fleet table should load

---

## Infrastructure Provisioning

### EMQX Cloud Serverless (Phase A)

**1. Manual Setup (Initial):**

- Visit https://www.emqx.com/en/cloud/serverless-mqtt
- Create account, create free-tier deployment
- Note broker URL: `<deployment-id>.emqxsl.com`
- Generate API key: Dashboard → API Keys → Create

**2. Pulumi Automation (Phase C+):**

```bash
cd ~/playground/Pulumi/esp32-iot-infra/
pulumi up
# Preview changes, confirm
```

### Firebase Project Setup

**1. Create Project:**

- Visit https://console.firebase.google.com
- Create new project: `esp32-iot-ecosystem`
- Enable Google Analytics (optional)

**2. Enable Services:**

```bash
firebase projects:list
firebase use <project-id>

# Enable Firestore
firebase firestore:setup

# Enable Authentication
# Visit Firebase Console → Authentication → Sign-in method → Email/Password → Enable

# Enable Functions (requires Blaze plan for external API calls in Phase A)
# Upgrade to Blaze: Firebase Console → Usage and Billing → Details & Settings → Modify plan
```

**3. Set Admin Custom Claim:**

```bash
# Create a user first via Firebase Console or app sign-up
# Then set admin claim via Node.js script:

node <<EOF
const admin = require('firebase-admin');
admin.initializeApp();

const userId = '<your-user-uid>';
admin.auth().setCustomUserClaims(userId, { admin: true })
  .then(() => console.log('Admin claim set'))
  .catch(err => console.error(err));
EOF
```

---

## Troubleshooting

### Firmware Issues

**Problem:** `Error: No serial port found`

```bash
# Solution: Check device connection
ls /dev/ttyACM* /dev/ttyUSB*
# Add user to dialout group (Linux)
sudo usermod -a -G dialout $USER
# Log out and log back in
```

**Problem:** `Flash size mismatch`

```bash
# Solution: Verify board flash size
idf.py size
# Check platformio.ini or sdkconfig: flash size should match hardware (2MB or 4MB)
```

**Problem:** `MQTT connection failed`

```bash
# Solution: Check credentials
# Verify EMQX broker URL, username, password stored in NVS match backend-generated values
# Use serial monitor to view connection logs
```

### App Issues

**Problem:** `firebase_core plugin not found`

```bash
# Solution: Regenerate Firebase config
flutter clean
flutter pub get
flutterfire configure
```

**Problem:** `FirebaseFunctionsException: permission-denied`

```bash
# Solution: User not authenticated or not authorized
# Check Firebase Auth state
# Verify Firestore rules allow authenticated access
# Verify user owns device (ownership check in Cloud Function)
```

### Backend Issues

**Problem:** `firebase deploy --only functions` fails with `Error: HTTP Error: 403, Forbidden`

```bash
# Solution: Enable required APIs
gcloud services enable cloudfunctions.googleapis.com --project=<project-id>
gcloud services enable cloudbuild.googleapis.com --project=<project-id>
```

**Problem:** Cloud Function times out

```bash
# Solution: Increase timeout
# functions/index.ts:
export const someFunction = onCall({timeoutSeconds: 540}, async (request) => {
  // ... function logic
});

# Redeploy: firebase deploy --only functions
```

**Problem:** MQTT subscriber crashes

```bash
# Solution: Add reconnection logic
# mqtt-subscriber.js:
client.on('error', (err) => {
  console.error('MQTT error:', err);
  // Reconnect after 5 seconds
  setTimeout(() => client.reconnect(), 5000);
});
```

### Admin Dashboard Issues

**Problem:** `firebase deploy --only hosting` fails with `Error: HTTP Error: 404`

```bash
# Solution: Initialize hosting
firebase init hosting
# Select project, set public directory to dist or build
```

**Problem:** Admin dashboard shows "Permission denied"

```bash
# Solution: Verify admin custom claim is set
# Run Node.js script to check:
admin.auth().getUser(userId).then(user => {
  console.log('Custom claims:', user.customClaims);
});
```

---

## Continuous Integration / Continuous Deployment (CI/CD)

### GitHub Actions (Example)

**Firmware CI:**

```yaml
# .github/workflows/firmware.yml
name: Firmware Build
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Set up Python
        uses: actions/setup-python@v4
        with:
          python-version: '3.9'
      - name: Install PlatformIO
        run: pip install platformio
      - name: Build firmware
        run: pio run
      - name: Upload binary
        uses: actions/upload-artifact@v3
        with:
          name: firmware
          path: .pio/build/esp32-c3-devkitm-1/firmware.bin
```

**Backend CI/CD:**

```yaml
# .github/workflows/backend.yml
name: Backend Deploy
on:
  push:
    branches: [main]
jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Set up Node.js
        uses: actions/setup-node@v3
        with:
          node-version: '18'
      - name: Install Firebase CLI
        run: npm install -g firebase-tools
      - name: Deploy to Firebase
        run: firebase deploy --only functions,firestore --token ${{ secrets.FIREBASE_TOKEN }}
```

---

## Rollback Procedures

### Firmware Rollback

```bash
# Flash previous firmware binary
esptool.py --port /dev/ttyACM0 write_flash 0x10000 firmware-previous.bin
```

### Backend Rollback

```bash
# Firebase Functions keep previous versions
firebase functions:list
firebase functions:rollback functionName --revision <revision-id>
```

### App Rollback

- Google Play: Rollout → Halt rollout, roll back to previous version
- Direct APK: Distribute previous APK version

---

**Document Status:** Living document, updated with deployment learnings.

**Maintainer:** DevOps / Project Lead

**Review Cadence:** After each major deployment, update with lessons learned.
