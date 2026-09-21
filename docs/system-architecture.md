# ESP32 IoT Ecosystem - System Architecture

**Document Purpose:** High-level overview of the complete IoT ecosystem architecture, spanning embedded firmware, mobile app, cloud backend, and multi-ecosystem integrations.

**Target Audience:** Developers, architects, future collaborators, and documentation for project wiki.

**Last Updated:** 2026-08-28

---

## Table of Contents

1. [Overview](#overview)
2. [System Components](#system-components)
3. [Architecture Diagrams](#architecture-diagrams)
4. [Data Flow](#data-flow)
5. [Communication Protocols](#communication-protocols)
6. [Security Architecture](#security-architecture)
7. [Deployment Architecture](#deployment-architecture)
8. [Technology Stack](#technology-stack)
9. [Scalability Considerations](#scalability-considerations)

---

## Overview

The ESP32 IoT Ecosystem is a complete end-to-end IoT solution demonstrating modern IoT architecture patterns, from embedded device firmware to cloud backend and multi-ecosystem integration (Alexa, Google Home, SmartThings, Apple Home).

**Current Scale:** 1 user, 3 ESP32-C3 devices (hobby/learning project)

**Design Philosophy:** Optimize for learning and correct architectural patterns, not premature scale.

**Key Features:**

- BLE-based device provisioning (Wi-Fi credentials via phone app)
- Private authenticated MQTT broker (per-device credentials)
- Firebase-based backend (serverless Cloud Functions + Firestore)
- Mobile app (Flutter, Android-first, iOS-ready)
- Admin dashboard (web-based fleet management)
- Matter protocol support (multi-ecosystem control)

---

## System Components

### 1. Device Firmware (ESP32-C3)

- **Platform:** ESP32-C3 Super Mini (2MB or 4MB flash)
- **RTOS:** FreeRTOS (built into ESP-IDF)
- **Languages:** C (main firmware), C++ (Matter integration)
- **Build System:**
  - Phases A-D: PlatformIO + ESP-IDF
  - Phase E+: Native ESP-IDF (required for Matter SDK)

**Responsibilities:**

- LED control (GPIO8, active-low)
- BLE provisioning (NimBLE GATT service, custom protocol)
- Wi-Fi connectivity (stored credentials in NVS)
- MQTT client (connect to private broker, pub/sub device topics)
- Matter endpoint (OnOff cluster, multi-ecosystem control)
- Persistent state (LED state survives reboot via NVS)

**Firmware Modules:**

- `main.c` - Core logic: LED control, MQTT client, Wi-Fi management
- `ble_prov.c/.h` - BLE provisioning GATT service
- `matter_device.cpp` - Matter OnOff cluster (Phase E+)

**Topics Published:**

- `devices/registry` (retained, device metadata + state)
- `devices/<device_id>/status` (retained, online/offline via Last Will)

**Topics Subscribed:**

- `devices/<device_id>/led/set` (LED control commands: ON/OFF)
- `devices/<device_id>/wifi/set` (management commands: remove, reprovision)

---

### 2. Mobile App (Flutter)

- **Platform:** Flutter 3.13+ (Dart)
- **Targets:** Android (primary), iOS (Phase F)
- **State Management:** Riverpod 3

**Responsibilities:**

- User authentication (Firebase Auth, email/password)
- BLE device provisioning (scan, connect, send Wi-Fi + MQTT credentials)
- Device ownership claiming (post-provisioning)
- Device control (LED on/off via backend HTTPS API)
- Device management (reset, remove owner)
- Real-time device state display (Firestore listeners)

**App Architecture:**

```
lib/
├── core/
│   ├── config/broker_config.dart       # MQTT broker config (legacy, Phase C removes MQTT)
│   ├── models/device.dart              # Device data model
│   └── storage/settings_repository.dart # Local storage (shared_preferences)
├── services/
│   ├── auth_service.dart               # Firebase Auth wrapper (Phase B+)
│   ├── backend_service.dart            # Backend API client (Phase C+)
│   └── ble_provisioning_service.dart   # BLE GATT communication
├── providers/
│   └── app_providers.dart              # Riverpod providers (auth, devices, state)
└── features/
    ├── auth/                           # Sign-in/sign-up screens (Phase B+)
    ├── devices/device_list_screen.dart # Main device list + control
    └── settings/                       # App settings
```

**Key Dependencies:**

- `firebase_core` + `firebase_auth` (Phase B+)
- `cloud_firestore` (Phase B+, real-time device state)
- `cloud_functions` (Phase C+, device control)
- `flutter_blue_plus` (BLE provisioning)
- `flutter_riverpod` (state management)

---

### 3. Backend (Firebase Cloud Functions + Firestore)

- **Platform:** Firebase (Google Cloud)
- **Runtime:** Node.js 18+ (Cloud Functions)
- **Database:** Firestore (NoSQL, real-time)

**Responsibilities:**

- Generate per-device MQTT credentials (Phase A)
- Manage device ownership (Phase B)
- Authorize and execute device control commands (Phase C)
- Subscribe to device MQTT topics, sync state to Firestore (Phase C)
- Admin fleet management (Phase D)
- Audit logging (Phase D)

**Cloud Functions:**
| Function | Trigger | Auth | Purpose |
|----------|---------|------|---------|
| `prepareDevice` | HTTPS Callable | Yes (Phase B+) | Generate MQTT credentials for new device |
| `claimDevice` | HTTPS Callable | Yes | Claim device ownership |
| `controlDeviceLed` | HTTPS Callable | Yes | Control device LED (ON/OFF) |
| `resetDevice` | HTTPS Callable | Yes | Reset device (wipe Wi-Fi credentials) |
| `removeDeviceOwner` | HTTPS Callable | Yes | Remove ownership + revoke MQTT credentials |
| `adminGetDevices` | HTTPS Callable | Admin only | Get all devices (fleet view) |
| `adminReassignOwner` | HTTPS Callable | Admin only | Reassign device ownership |
| `adminResetDevice` | HTTPS Callable | Admin only | Reset device (audited) |

**Backend Services:**

- **MQTT Subscriber Service:** Long-running process (Cloud Run or local) that subscribes to device MQTT topics (`devices/+/status`, `devices/registry`), updates Firestore on state changes
- **EMQX API Client:** Manages device user creation/deletion via EMQX REST API

**Firestore Collections:**

- `/devices/{deviceId}` - Device metadata + state (source of truth)
- `/users/{userId}/devices/{deviceId}` - Denormalized device list per user (read-optimized)
- `/admin-audit-log/{logId}` - Admin action audit trail (Phase D)

---

### 4. MQTT Broker (EMQX Cloud Serverless)

- **Service:** EMQX Cloud Serverless (managed)
- **Tier:** Free (1M session minutes/month, 1GB traffic/month)
- **Protocol:** MQTT 3.1.1 over TLS (mqtts://), port 8883
- **Authentication:** Username/password per device

**Responsibilities:**

- Message routing between devices and backend
- TLS termination
- Per-device authentication (username = `device_<device_id>`)
- ACL enforcement (device can only pub/sub to `devices/<device_id>/#`)
- Last Will delivery (offline status when device disconnects)
- Retained message storage (`devices/registry`, `devices/<device_id>/status`)

**Configuration:**

- Backend service account: username `backend`, full access to all `devices/#` topics
- Per-device accounts: username `device_<device_id>`, restricted to `devices/<device_id>/#`

---

### 5. Admin Dashboard (Firebase Hosting)

- **Platform:** Firebase Hosting (static site)
- **Framework:** React (TypeScript) or Vue.js
- **Build Tool:** Vite or Create React App

**Responsibilities:**

- Device fleet view (all devices across all users)
- Device detail view (metadata, owner, status, actions)
- Ownership reassignment
- Device reset (audited)
- OTA firmware trigger (placeholder, Phase E)
- Audit log view

**Admin Authentication:**

- Firebase Auth with custom claim: `{ admin: true }`
- Set via Firebase Console or Admin SDK script

**Admin UI Routes:**

- `/` - Device fleet table
- `/devices/:id` - Device detail view
- `/audit` - Audit log
- `/login` - Admin sign-in

---

### 6. Infrastructure as Code (Pulumi)

- **Platform:** Pulumi (TypeScript or Python)
- **Cloud Providers:** EMQX (via REST API), Firebase (via gcloud/Terraform)

**Responsibilities:**

- EMQX device provisioning automation
- Firebase project configuration (Firestore indexes, security rules)
- Future: Cloud Run service deployment (MQTT subscriber)

**Repository:** `~/playground/Pulumi/esp32-iot-infra/`

---

## Architecture Diagrams

### Phase A-C: Private Broker + Backend Control

```
┌─────────────────────────────────────────────────────────────────────┐
│                         User's Network                              │
│                                                                     │
│  ┌──────────────┐         BLE          ┌─────────────────────┐    │
│  │  ESP32-C3    │ ←─────provisioning───→│   Flutter App      │    │
│  │  Device      │                       │   (Android/iOS)    │    │
│  │              │                       │                    │    │
│  │  - LED GPIO8 │                       │  - Firebase Auth   │    │
│  │  - BLE GATT  │                       │  - Backend API     │    │
│  │  - MQTT Pub  │                       │  - Firestore       │    │
│  │  - NVS State │                       │    Listeners       │    │
│  └───────┬──────┘                       └─────────┬──────────┘    │
│          │                                         │               │
│          │ Wi-Fi                                   │ HTTPS         │
│          │                                         │               │
└──────────┼─────────────────────────────────────────┼───────────────┘
           │                                         │
           │                                         │
    ───────┴─────────────────────────────────────────┴───────────────
                            Internet
    ────────────────────────────────────────────────────────────────
           │                                         │
           │                                         │
           │ MQTT 3.1.1/TLS                         │ HTTPS/REST
           │ (mqtts://)                             │
           ▼                                         ▼
    ┌──────────────────┐                   ┌──────────────────────┐
    │  EMQX Cloud      │                   │  Firebase            │
    │  Serverless      │                   │                      │
    │                  │                   │  ┌────────────────┐  │
    │  - Per-device    │◄──MQTT Pub/Sub───►│  │ Cloud Functions│  │
    │    auth          │                   │  │                │  │
    │  - TLS           │                   │  │ - Device API   │  │
    │  - ACLs          │                   │  │ - MQTT Client  │  │
    │  - Last Will     │                   │  │ - Admin API    │  │
    │  - Retained msgs │                   │  └────────┬───────┘  │
    │                  │                   │           │          │
    └──────────────────┘                   │  ┌────────▼───────┐  │
                                           │  │  Firestore     │  │
                                           │  │                │  │
                                           │  │ /devices/{id}  │  │
                                           │  │ /users/{uid}/  │  │
                                           │  │   devices/     │  │
                                           │  └────────────────┘  │
                                           │                      │
                                           └──────────────────────┘
```

### Phase E: Matter Multi-Ecosystem Integration

```
┌─────────────────────────────────────────────────────────────────────┐
│                         User's Network                              │
│                                                                     │
│  ┌──────────────────┐                                              │
│  │  ESP32-C3 Device │                                              │
│  │                  │                                              │
│  │  Control Paths:  │                                              │
│  │  1) MQTT         │──────MQTT/TLS────────► (Backend path)       │
│  │  2) Matter       │──────Matter───────────► (Ecosystem path)    │
│  │                  │                                              │
│  │  - LED OnOff     │         │                                    │
│  │    Cluster       │         │                                    │
│  │  - BLE           │         │ Matter over                        │
│  │    Commissioning │         │ Thread/Wi-Fi                       │
│  └──────────────────┘         │                                    │
│           │                   │                                    │
│           │                   ▼                                    │
│           │          ┌────────────────────┐                        │
│           │          │  Matter Controller │                        │
│           │          │  (Phone/Hub)       │                        │
│           │          │                    │                        │
│           │          │  - Alexa Echo      │                        │
│           │          │  - Google Home Hub │                        │
│           │          │  - SmartThings Hub │                        │
│           │          │  - Apple HomePod   │                        │
│           │          └────────────────────┘                        │
│           │                                                         │
└───────────┼─────────────────────────────────────────────────────────┘
            │
            │ (MQTT path continues to work alongside Matter)
            ▼
      [Backend/EMQX as in Phase A-C diagram]
```

---

## Data Flow

### Device Provisioning Flow (Phase A)

```
1. User opens app → Tap "Add Device"
2. App scans for BLE devices → Finds "ESP32C3-<mac>"
3. User selects device → App connects to BLE GATT service
4. App requests Wi-Fi scan → Device scans, returns SSID list
5. User selects Wi-Fi network, enters password
6. App calls backend: prepareDevice(deviceId)
   ├─ Backend generates unique MQTT username/password
   ├─ Backend stores credentials in Firestore
   ├─ Backend creates EMQX user via REST API
   └─ Backend returns credentials to app
7. App sends to device via BLE:
   {"type":"connect", "ssid":"...", "password":"...",
    "mqtt_broker":"...", "mqtt_username":"...", "mqtt_password":"..."}
8. Device stores credentials in NVS
9. Device disconnects BLE, connects to Wi-Fi
10. Device connects to EMQX with unique credentials
11. Device publishes to devices/registry (retained)
12. App discovers device via Firestore (Phase C) or MQTT (Phase A-B)
```

### Device Ownership Claim Flow (Phase B)

```
1. Device online, published to devices/registry
2. App shows device in "Unclaimed Devices" list
3. User taps "Claim This Device"
4. App calls backend: claimDevice(deviceId) with Firebase Auth token
   ├─ Backend verifies user is authenticated
   ├─ Backend checks device is not already owned
   ├─ Backend updates /devices/{deviceId}: ownerId = user.uid
   └─ Backend creates /users/{uid}/devices/{deviceId} (denormalized)
5. App now shows device in "My Devices" list
6. If app reinstalled, user logs in → sees device in "My Devices" (recovery!)
```

### LED Control Flow (Phase C)

```
1. User taps LED toggle in app
2. App calls backend: controlDeviceLed(deviceId, "ON") with Firebase Auth token
   ├─ Backend verifies user is authenticated
   ├─ Backend checks user owns this device (Firestore lookup)
   ├─ Backend connects to EMQX with backend service account
   └─ Backend publishes "ON" to devices/{deviceId}/led/set (QoS 1)
3. Device receives MQTT message
   ├─ Device calls set_led(true)
   ├─ Device saves state to NVS
   └─ Device publishes updated state to devices/registry
4. Backend MQTT subscriber sees registry update
   └─ Backend updates Firestore: /devices/{deviceId}/ledState = "ON"
5. App Firestore listener sees change
   └─ App updates UI: toggle switches to ON
```

### Device Reset Flow (Phase C)

```
1. User taps "Reset Device" in app
2. App confirms: "This will wipe Wi-Fi credentials. Continue?"
3. User confirms → App calls backend: resetDevice(deviceId)
   ├─ Backend verifies user owns device
   └─ Backend publishes {"command":"remove"} to devices/{deviceId}/wifi/set
4. Device receives command
   ├─ Device erases Wi-Fi credentials from NVS
   ├─ Device publishes Last Will (offline, reason=removed)
   ├─ Device starts LED blinking
   ├─ Device starts BLE advertising
   └─ Device disconnects from Wi-Fi/MQTT
5. Device is now in "factory reset" state (ready to re-provision)
```

---

## Communication Protocols

### BLE Provisioning Protocol (NimBLE GATT)

**Service UUID:** `6e400001-b5a3-f393-e0a9-e50e24dcca9e` (Nordic UART Service style)

**Characteristics:**

- **RX** (write): `6e400002-b5a3-f393-e0a9-e50e24dcca9e` - Phone → Device
- **TX** (notify): `6e400003-b5a3-f393-e0a9-e50e24dcca9e` - Device → Phone

**Message Format:** JSON, one message per line (`\n`-delimited)

**Phone → Device Messages:**

```json
{"type":"scan"}
{"type":"connect","ssid":"MyWiFi","password":"secret",
 "mqtt_broker":"xxx.emqxsl.com","mqtt_username":"device_abc","mqtt_password":"xxx"}
```

**Device → Phone Messages:**

```json
{"type":"hello","device_id":"a1b2c3d4e5f6","wifi":"provisioning"}
{"type":"status","wifi":"scanning"}
{"type":"scan_result","ssid":"MyWiFi","rssi":-45,"auth":"WPA2","channel":6}
{"type":"scan_done","count":5}
{"type":"status","wifi":"connecting"}
{"type":"status","wifi":"connected","detail":"192.168.1.100"}
```

---

### MQTT Topics & Payloads

**Device → Backend/App:**

- `devices/registry` (QoS 1, retained)

  ```json
  {
    "device_id": "a1b2c3d4e5f6",
    "status": "online",
    "led_topic": "devices/a1b2c3d4e5f6/led/set",
    "led_state": "OFF",
    "firmware_version": "1.0.0"
  }
  ```

- `devices/<device_id>/status` (QoS 1, retained)
  ```
  online
  ```
  or
  ```json
  { "state": "offline", "reason": "removed" }
  ```

**Backend → Device:**

- `devices/<device_id>/led/set` (QoS 1)

  ```
  ON
  ```

  or

  ```
  OFF
  ```

- `devices/<device_id>/wifi/set` (QoS 1)
  ```json
  { "command": "remove" }
  ```
  or
  ```json
  { "command": "reprovision" }
  ```

---

### Backend API (Firebase Cloud Functions)

**Base URL:** `https://<region>-<project-id>.cloudfunctions.net/`

**Authentication:** Firebase Auth ID token in `Authorization: Bearer <token>` header or as part of Firebase Callable Function request

**Endpoints:** See "Backend API Reference" section in Appendix

---

### Matter Protocol (Phase E+)

**Device Type:** OnOff Light (Matter Device Type 0x0100)

**Clusters:**

- **OnOff Cluster (0x0006):** On/Off attribute (boolean)
- **Basic Information Cluster (0x0028):** Vendor, product, serial number
- **Descriptor Cluster (0x001D):** Device type, endpoints

**Commissioning:** BLE or Wi-Fi (BLE preferred for consistency with existing provisioning)

**Controllers:** Any Matter-compatible controller (Alexa, Google Home, SmartThings, Apple Home)

---

## Security Architecture

### Authentication & Authorization

**Device Authentication (MQTT):**

- Per-device MQTT credentials (username/password)
- TLS 1.2+ (mqtts://, port 8883)
- Server certificate verification enabled

**User Authentication (App):**

- Firebase Auth (email/password, Phase B+)
- ID token-based API calls (JWT)
- Token refresh handled by Firebase SDK

**Admin Authentication (Dashboard):**

- Firebase Auth with custom claim: `{ admin: true }`
- Server-side claim verification in Cloud Functions

**Authorization:**

- Device control: User must own device (Firestore `/devices/{deviceId}/ownerId` check)
- Admin actions: User must have `admin: true` custom claim
- MQTT ACLs: Device can only pub/sub to `devices/<device_id>/#`

### Data Protection

**Data at Rest:**

- Firestore: Encrypted by Google Cloud (AES-256)
- NVS (device): ESP32 flash encryption (can be enabled in sdkconfig)
- MQTT credentials in Firestore: Password hash stored (SHA-256), plain password never stored

**Data in Transit:**

- MQTT: TLS 1.2+ (port 8883)
- Backend API: HTTPS (TLS 1.2+)
- BLE: Unencrypted for Phase A-D (provisioning only, short-lived connection); can add SMP pairing/encryption later

**Secrets Management:**

- Backend: Firebase Functions Config for EMQX credentials
- App: Firebase Auth handles token refresh
- Device: MQTT credentials stored in NVS (plaintext; enable flash encryption for production)

### Threat Model

**Threats Mitigated:**

- ✅ Unauthorized device access (per-device MQTT auth + ACLs)
- ✅ Unauthorized user access (Firebase Auth + ownership checks)
- ✅ Device impersonation (unique MQTT credentials per device)
- ✅ Man-in-the-middle (TLS for MQTT + backend API)
- ✅ Stolen phone = stolen devices (server-side ownership, device can be remotely reset)

**Threats Not Fully Mitigated (Acceptable for Hobby Scale):**

- ⚠️ BLE provisioning eavesdropping (BLE traffic unencrypted; mitigated: short-range, MQTT credentials change after provisioning)
- ⚠️ Physical device access (attacker with device + serial cable can read NVS; mitigated: enable flash encryption for production)
- ⚠️ Compromised Firebase project (admin has full access; mitigated: strong Firebase account password + 2FA)

---

## Deployment Architecture

### Development Environment

- **Firmware:** PlatformIO (Phases A-D) or ESP-IDF (Phase E+) on developer machine (Manjaro Linux)
- **App:** Flutter dev server (`flutter run`) on Android device or emulator
- **Backend:** Firebase Emulator Suite for local testing, deployed to Firebase Cloud for integration testing
- **MQTT Subscriber:** Runs locally as Node.js process for development

### Production Environment

- **Firmware:** Flashed to physical ESP32-C3 devices via USB (OTA in Phase E)
- **App:** Distributed as APK (Android) or via App Store (iOS, Phase F)
- **Backend:** Firebase Cloud Functions (auto-scaling, serverless)
- **MQTT Subscriber:** Cloud Run (Phase D) or Firebase Cloud Functions (long-running)
- **Admin Dashboard:** Firebase Hosting (global CDN)
- **MQTT Broker:** EMQX Cloud Serverless (managed, multi-AZ)

### CI/CD (Future)

- **Firmware:** GitHub Actions → build → upload binary to Firebase Storage
- **App:** GitHub Actions → build APK/IPA → upload to internal testing
- **Backend:** `firebase deploy --only functions,firestore,hosting` on git push to main
- **Admin:** `firebase deploy --only hosting` on git push to main

---

## Technology Stack

### Firmware

- **Language:** C (main), C++ (Matter)
- **SDK:** ESP-IDF v5.x (Phases A-D), v6.0.2+ (Phase E+)
- **Build:** PlatformIO (Phases A-D), native ESP-IDF (Phase E+)
- **Libraries:** esp_wifi, esp_mqtt_client, nimble (BLE), esp_matter (Phase E+)

### Mobile App

- **Framework:** Flutter 3.13+
- **Language:** Dart 3.x
- **State Management:** Riverpod 3
- **Key Packages:** firebase_core, firebase_auth, cloud_firestore, cloud_functions, flutter_blue_plus

### Backend

- **Platform:** Firebase (Google Cloud)
- **Runtime:** Node.js 18+ (Cloud Functions)
- **Database:** Firestore (NoSQL)
- **Language:** TypeScript (recommended) or JavaScript
- **Libraries:** firebase-admin, mqtt, axios (for EMQX API)

### Admin Dashboard

- **Framework:** React 18+ (TypeScript) or Vue.js 3
- **Build Tool:** Vite or Create React App
- **Libraries:** firebase, react-router-dom (or vue-router)

### Infrastructure

- **IaC:** Pulumi (TypeScript or Python)
- **Config Management:** Ansible (deferred to Phase D+)

### MQTT Broker

- **Service:** EMQX Cloud Serverless
- **Protocol:** MQTT 3.1.1
- **TLS:** Managed by EMQX

---

## Scalability Considerations

### Current Scale

- **Users:** 1
- **Devices:** 3
- **Messages/day:** ~1000 (LED toggles + status updates)
- **Cost:** $0/month (all services within free tiers)

### Scale Projections

**10 Users, 30 Devices:**

- **EMQX:** Still within free tier (1M session minutes/month)
- **Firebase:** Still within Spark plan limits (50K reads/day, 20K writes/day)
- **Estimated Cost:** $0/month

**100 Users, 300 Devices:**

- **EMQX:** May exceed free tier → upgrade to $10-20/month tier
- **Firebase:** Exceed Spark limits → upgrade to Blaze (pay-as-you-go), ~$5-10/month
- **Cloud Run (MQTT subscriber):** ~$5/month
- **Estimated Cost:** ~$20-35/month

**1000 Users, 3000 Devices:**

- **EMQX:** Dedicated cluster recommended (~$50-100/month)
- **Firebase:** Firestore + Functions cost ~$50-100/month
- **Cloud Run:** ~$20/month
- **CDN (admin dashboard):** Minimal cost
- **Estimated Cost:** ~$120-220/month

**Scaling Bottlenecks:**

- **MQTT Subscriber:** Single-instance bottleneck; solution: horizontal scaling with MQTT load balancer
- **Firestore Writes:** Device state updates; solution: batch writes, reduce update frequency
- **Firebase Functions Cold Starts:** ~1-2s latency; solution: min instances (costs more) or migrate to Cloud Run

**Horizontal Scaling Considerations:**

- Firmware: Stateless, scales linearly (each device independent)
- App: Stateless, scales linearly (each user independent)
- Backend: Firebase auto-scales, but consider:
  - MQTT subscriber: needs work queue for horizontal scaling
  - Firestore: monitor read/write usage, optimize queries

---

## Future Architecture Enhancements

**Phase E+ (Matter Integration):**

- Add Matter endpoint to firmware (coexists with MQTT)
- Device controllable via Alexa, Google Home, SmartThings, Apple Home
- Backend tracks Matter commissioning state

**Phase F (iOS Support):**

- Build iOS version of Flutter app
- Test Apple Home integration

**Beyond Phase F:**

- **OTA Firmware Updates:** Backend triggers OTA, device downloads from Firebase Storage
- **Device Sharing:** Multiple users can control same device (role-based: owner vs. viewer)
- **Grouping/Rooms:** Organize devices into rooms/groups
- **Automation/Scenes:** "Goodnight" scene turns off all lights
- **Telemetry/Analytics:** Device uptime, connectivity stats, usage patterns
- **Multi-Tenant:** Support multiple independent user bases (white-label)

---

**Document Status:** Living document, updated as architecture evolves.

**Maintainer:** Project lead

**Review Cadence:** After each phase completion, update relevant sections.
