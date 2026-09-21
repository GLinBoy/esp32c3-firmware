# Backend API Reference

**Document Purpose:** Complete reference for all backend API endpoints (Firebase Cloud Functions).

**Base URL:** `https://<region>-<project-id>.cloudfunctions.net/`

**Authentication:** Firebase Auth ID token required for all endpoints (except where noted).

**Last Updated:** 2026-08-28

---

## Table of Contents
1. [Authentication](#authentication)
2. [User Endpoints (Phases B-C)](#user-endpoints)
3. [Admin Endpoints (Phase D)](#admin-endpoints)
4. [Error Handling](#error-handling)
5. [Rate Limiting](#rate-limiting)
6. [Examples](#examples)

---

## Authentication

All API calls (except explicitly marked as public) require Firebase Authentication.

### Firebase Callable Functions
Most endpoints use Firebase HTTPS Callable Functions, which automatically handle authentication:

```typescript
// Client-side (Flutter)
final callable = FirebaseFunctions.instance.httpsCallable('functionName');
final result = await callable.call({
  'param1': 'value1',
  'param2': 'value2',
});
```

The Firebase SDK automatically includes the user's ID token in the request.

### Server-Side Auth Verification
```typescript
// Backend
export const someFunction = onCall(async (request) => {
  if (!request.auth) {
    throw new HttpsError('unauthenticated', 'User must be authenticated');
  }
  const userId = request.auth.uid;
  // ... rest of function
});
```

---

## User Endpoints

### POST /prepareDevice

**Phase:** A (Phase B+ requires authentication)

**Purpose:** Generate MQTT credentials for a new device during provisioning.

**Authentication:** 
- Phase A: No authentication (public endpoint for initial testing)
- Phase B+: Required

**Request:**
```json
{
  "deviceId": "a1b2c3d4e5f6"
}
```

**Response:**
```json
{
  "mqttBroker": "xxx.emqxsl.com",
  "mqttPort": 8883,
  "mqttUsername": "device_a1b2c3d4e5f6",
  "mqttPassword": "random32charalphanumeric123456"
}
```

**Errors:**
- `invalid-argument`: deviceId missing or invalid format
- `internal`: EMQX API call failed
- `unauthenticated`: (Phase B+) User not authenticated

**Implementation Notes:**
- Generates unique MQTT credentials per device
- Stores credentials in Firestore `/devices/{deviceId}`
- Creates EMQX user via REST API
- Sets device ACL (can only pub/sub to `devices/{deviceId}/#`)

**Example (Flutter):**
```dart
final callable = FirebaseFunctions.instance.httpsCallable('prepareDevice');
final result = await callable.call({'deviceId': deviceId});
final mqttBroker = result.data['mqttBroker'];
final mqttUsername = result.data['mqttUsername'];
final mqttPassword = result.data['mqttPassword'];
```

---

### POST /claimDevice

**Phase:** B

**Purpose:** Claim ownership of a device after provisioning.

**Authentication:** Required

**Request:**
```json
{
  "deviceId": "a1b2c3d4e5f6"
}
```

**Response:**
```json
{
  "success": true,
  "deviceId": "a1b2c3d4e5f6"
}
```

**Errors:**
- `unauthenticated`: User not authenticated
- `not-found`: Device not found in Firestore
- `already-exists`: Device already owned by another user
- `invalid-argument`: deviceId missing or invalid

**Implementation Notes:**
- Checks device exists in `/devices/{deviceId}`
- Checks device not already owned (`ownerId` field is null)
- Sets `ownerId` to current user's UID
- Creates denormalized copy in `/users/{userId}/devices/{deviceId}`

**Example (Flutter):**
```dart
final callable = FirebaseFunctions.instance.httpsCallable('claimDevice');
final result = await callable.call({'deviceId': deviceId});
if (result.data['success']) {
  print('Device claimed successfully!');
}
```

---

### POST /controlDeviceLed

**Phase:** C

**Purpose:** Control a device's LED (turn on or off).

**Authentication:** Required

**Request:**
```json
{
  "deviceId": "a1b2c3d4e5f6",
  "state": "ON"
}
```

**Parameters:**
- `deviceId` (string, required): Device identifier (12-char hex)
- `state` (string, required): `"ON"` or `"OFF"`

**Response:**
```json
{
  "success": true,
  "deviceId": "a1b2c3d4e5f6",
  "state": "ON"
}
```

**Errors:**
- `unauthenticated`: User not authenticated
- `not-found`: Device not found
- `permission-denied`: User does not own this device
- `invalid-argument`: deviceId or state missing/invalid
- `internal`: MQTT publish failed

**Implementation Notes:**
- Verifies user owns device (Firestore `/devices/{deviceId}/ownerId` == `request.auth.uid`)
- Connects to EMQX with backend service account
- Publishes state (`ON` or `OFF`) to `devices/{deviceId}/led/set` (QoS 1)
- Device receives command, updates LED, publishes state to registry
- Backend MQTT subscriber updates Firestore
- App Firestore listener reflects state change

**Example (Flutter):**
```dart
final callable = FirebaseFunctions.instance.httpsCallable('controlDeviceLed');
await callable.call({
  'deviceId': device.id,
  'state': isOn ? 'ON' : 'OFF',
});
// State update will come via Firestore listener
```

---

### POST /resetDevice

**Phase:** C

**Purpose:** Reset a device (wipe Wi-Fi credentials, enter BLE provisioning mode).

**Authentication:** Required

**Request:**
```json
{
  "deviceId": "a1b2c3d4e5f6"
}
```

**Response:**
```json
{
  "success": true,
  "deviceId": "a1b2c3d4e5f6",
  "message": "Reset command sent"
}
```

**Errors:**
- `unauthenticated`: User not authenticated
- `not-found`: Device not found
- `permission-denied`: User does not own this device
- `internal`: MQTT publish failed

**Implementation Notes:**
- Verifies ownership
- Publishes `{"command":"remove"}` to `devices/{deviceId}/wifi/set` (QoS 1)
- Device erases Wi-Fi credentials from NVS
- Device publishes offline status (Last Will with `reason=removed`)
- Device starts LED blinking
- Device starts BLE advertising
- Device ready to be re-provisioned

**Example (Flutter):**
```dart
final confirm = await showDialog<bool>(
  context: context,
  builder: (context) => AlertDialog(
    title: Text('Reset Device?'),
    content: Text('This will wipe Wi-Fi credentials. Device must be re-provisioned.'),
    actions: [
      TextButton(onPressed: () => Navigator.pop(context, false), child: Text('Cancel')),
      TextButton(onPressed: () => Navigator.pop(context, true), child: Text('Reset')),
    ],
  ),
);

if (confirm == true) {
  final callable = FirebaseFunctions.instance.httpsCallable('resetDevice');
  await callable.call({'deviceId': device.id});
  // Show success message, navigate back
}
```

---

### POST /removeDeviceOwner

**Phase:** C

**Purpose:** Remove device ownership and revoke MQTT credentials. Device loses connection and must be re-provisioned and re-claimed.

**Authentication:** Required

**Request:**
```json
{
  "deviceId": "a1b2c3d4e5f6"
}
```

**Response:**
```json
{
  "success": true,
  "deviceId": "a1b2c3d4e5f6",
  "message": "Device ownership removed"
}
```

**Errors:**
- `unauthenticated`: User not authenticated
- `not-found`: Device not found
- `permission-denied`: User does not own this device
- `internal`: Firestore or EMQX API call failed

**Implementation Notes:**
- Verifies ownership
- Deletes `ownerId`, `provisionedAt`, `provisionedBy` from `/devices/{deviceId}`
- Deletes `/users/{userId}/devices/{deviceId}`
- Calls EMQX REST API to delete device user (revokes MQTT credentials)
- Device loses MQTT connection immediately
- Device must be factory-reset (via resetDevice or physical serial command) to re-provision

**Example (Flutter):**
```dart
final confirm = await showDialog<bool>(
  context: context,
  builder: (context) => AlertDialog(
    title: Text('Remove Device?'),
    content: Text('This will revoke ownership and credentials. Device will go offline.'),
    actions: [
      TextButton(onPressed: () => Navigator.pop(context, false), child: Text('Cancel')),
      TextButton(
        onPressed: () => Navigator.pop(context, true),
        child: Text('Remove', style: TextStyle(color: Colors.red)),
      ),
    ],
  ),
);

if (confirm == true) {
  final callable = FirebaseFunctions.instance.httpsCallable('removeDeviceOwner');
  await callable.call({'deviceId': device.id});
  // Device removed from user's list
}
```

---

## Admin Endpoints

All admin endpoints require the user to have the custom claim `{ admin: true }`.

### GET /adminGetDevices

**Phase:** D

**Purpose:** Get all devices across all users (fleet view).

**Authentication:** Required (admin claim)

**Request:**
```json
{}
```

**Response:**
```json
{
  "devices": [
    {
      "id": "a1b2c3d4e5f6",
      "deviceId": "a1b2c3d4e5f6",
      "ownerId": "user123",
      "ownerEmail": "user@example.com",
      "nickname": "Living Room LED",
      "online": true,
      "ledState": "OFF",
      "lastSeen": "2026-08-28T10:30:00Z",
      "firmwareVersion": "1.0.0",
      "hardwareModel": "ESP32-C3-Super-Mini",
      "provisionedAt": "2026-08-01T08:00:00Z"
    },
    // ... more devices
  ]
}
```

**Errors:**
- `unauthenticated`: User not authenticated
- `permission-denied`: User does not have admin claim

**Implementation Notes:**
- Fetches all documents from `/devices` collection
- For each device with `ownerId`, fetches owner's email from Firebase Auth
- Returns array of device objects with owner info

**Example (React Admin UI):**
```typescript
const callable = functions.httpsCallable('adminGetDevices');
const result = await callable();
const devices = result.data.devices;
setDevices(devices);
```

---

### POST /adminReassignOwner

**Phase:** D

**Purpose:** Reassign device ownership to a different user.

**Authentication:** Required (admin claim)

**Request:**
```json
{
  "deviceId": "a1b2c3d4e5f6",
  "newOwnerId": "user456"
}
```

**Response:**
```json
{
  "success": true
}
```

**Errors:**
- `unauthenticated`: User not authenticated
- `permission-denied`: User does not have admin claim
- `not-found`: Device or new owner not found
- `invalid-argument`: deviceId or newOwnerId missing

**Implementation Notes:**
- Updates `/devices/{deviceId}/ownerId` to `newOwnerId`
- Deletes device from old owner's `/users/{oldOwnerId}/devices/{deviceId}`
- Creates device in new owner's `/users/{newOwnerId}/devices/{deviceId}`
- Logs action to `/admin-audit-log`

**Example (React Admin UI):**
```typescript
const reassign = async (deviceId: string, newOwnerEmail: string) => {
  // First, look up new owner UID by email (separate admin endpoint or client-side)
  const newOwnerId = await getUserIdByEmail(newOwnerEmail);
  
  const callable = functions.httpsCallable('adminReassignOwner');
  await callable({ deviceId, newOwnerId });
  
  showNotification('Device ownership reassigned');
  refreshDeviceList();
};
```

---

### POST /adminResetDevice

**Phase:** D

**Purpose:** Reset a device (admin version, audited).

**Authentication:** Required (admin claim)

**Request:**
```json
{
  "deviceId": "a1b2c3d4e5f6"
}
```

**Response:**
```json
{
  "success": true
}
```

**Errors:**
- `unauthenticated`: User not authenticated
- `permission-denied`: User does not have admin claim
- `not-found`: Device not found
- `internal`: MQTT publish failed

**Implementation Notes:**
- Same logic as user `resetDevice` endpoint
- Additionally logs action to `/admin-audit-log` with admin UID

**Example (React Admin UI):**
```typescript
const callable = functions.httpsCallable('adminResetDevice');
await callable({ deviceId });
showNotification('Device reset command sent');
```

---

### POST /adminTriggerOTA

**Phase:** D (placeholder), E (implementation)

**Purpose:** Trigger OTA firmware update for a device.

**Authentication:** Required (admin claim)

**Request:**
```json
{
  "deviceId": "a1b2c3d4e5f6",
  "firmwareVersion": "1.1.0",
  "firmwareUrl": "https://storage.googleapis.com/.../firmware-1.1.0.bin"
}
```

**Response (Phase D - unimplemented):**
```json
{
  "error": "unimplemented",
  "message": "OTA not yet implemented"
}
```

**Response (Phase E - implemented):**
```json
{
  "success": true,
  "message": "OTA triggered"
}
```

**Errors:**
- `unauthenticated`: User not authenticated
- `permission-denied`: User does not have admin claim
- `unimplemented`: (Phase D) OTA not yet implemented
- `not-found`: Device not found
- `invalid-argument`: Missing required fields

**Implementation Notes (Phase E):**
- Publishes OTA command to device via MQTT
- Device downloads firmware from URL
- Device verifies signature, applies update, reboots
- Logs action to `/admin-audit-log`

---

## Error Handling

All errors follow Firebase HTTPS Error format:

**Error Response:**
```json
{
  "error": {
    "code": "permission-denied",
    "message": "User does not own this device"
  }
}
```

**Common Error Codes:**
- `unauthenticated`: User not authenticated (no Firebase Auth token)
- `permission-denied`: User authenticated but not authorized (ownership check failed or missing admin claim)
- `not-found`: Resource not found (device, user)
- `already-exists`: Resource already exists (device already owned)
- `invalid-argument`: Request parameters missing or invalid
- `internal`: Server error (MQTT publish failed, EMQX API failed, Firestore error)
- `unimplemented`: Feature not yet implemented (e.g., OTA in Phase D)

**Client-Side Handling (Flutter):**
```dart
try {
  final callable = FirebaseFunctions.instance.httpsCallable('controlDeviceLed');
  await callable.call({'deviceId': deviceId, 'state': 'ON'});
} on FirebaseFunctionsException catch (e) {
  switch (e.code) {
    case 'permission-denied':
      showError('You do not own this device');
      break;
    case 'not-found':
      showError('Device not found');
      break;
    case 'unauthenticated':
      showError('Please sign in');
      break;
    default:
      showError('Error: ${e.message}');
  }
}
```

---

## Rate Limiting

**Current:** No explicit rate limiting (Phase A-D)

**Future (Phase E+):**
- Firebase Cloud Functions have built-in quotas (default: 10K invocations/day on Spark plan, unlimited on Blaze)
- Consider adding application-level rate limiting for high-frequency actions (LED control):
  - Per-user: 100 LED toggles/minute
  - Per-device: 10 LED toggles/second
  - Implemented via Firestore transaction counters or Redis (if added)

**Current Mitigations:**
- Firebase Auth throttles repeated login attempts
- EMQX rate limits MQTT messages (configurable per user)
- Firestore has per-document write limits (1 write/second sustained)

---

## Examples

### Complete Device Provisioning + Claim Flow (Flutter)

```dart
Future<void> provisionAndClaimDevice(String deviceId, String ssid, String password) async {
  try {
    // Step 1: Get MQTT credentials from backend
    final prepareCallable = FirebaseFunctions.instance.httpsCallable('prepareDevice');
    final prepareResult = await prepareCallable.call({'deviceId': deviceId});
    
    final mqttBroker = prepareResult.data['mqttBroker'];
    final mqttUsername = prepareResult.data['mqttUsername'];
    final mqttPassword = prepareResult.data['mqttPassword'];
    
    // Step 2: Send credentials to device via BLE
    await bleService.sendProvisioningData(
      deviceId: deviceId,
      ssid: ssid,
      password: password,
      mqttBroker: mqttBroker,
      mqttUsername: mqttUsername,
      mqttPassword: mqttPassword,
    );
    
    // Step 3: Wait for device to come online (poll Firestore or MQTT)
    await Future.delayed(Duration(seconds: 10));
    
    // Step 4: Claim device ownership
    final claimCallable = FirebaseFunctions.instance.httpsCallable('claimDevice');
    await claimCallable.call({'deviceId': deviceId});
    
    showSuccess('Device provisioned and claimed!');
  } on FirebaseFunctionsException catch (e) {
    showError('Failed to provision device: ${e.message}');
  }
}
```

### Admin Dashboard Device Table (React)

```typescript
import { getFunctions, httpsCallable } from 'firebase/functions';

const DeviceFleetTable: React.FC = () => {
  const [devices, setDevices] = useState<Device[]>([]);
  const functions = getFunctions();

  useEffect(() => {
    const fetchDevices = async () => {
      const callable = httpsCallable(functions, 'adminGetDevices');
      const result = await callable();
      setDevices(result.data.devices);
    };
    fetchDevices();
  }, []);

  const handleReassignOwner = async (deviceId: string, newOwnerEmail: string) => {
    const newOwnerId = await lookupUserByEmail(newOwnerEmail); // separate helper
    const callable = httpsCallable(functions, 'adminReassignOwner');
    await callable({ deviceId, newOwnerId });
    // Refresh device list
    fetchDevices();
  };

  return (
    <table>
      <thead>
        <tr>
          <th>Device ID</th>
          <th>Owner</th>
          <th>Status</th>
          <th>LED State</th>
          <th>Actions</th>
        </tr>
      </thead>
      <tbody>
        {devices.map(device => (
          <tr key={device.id}>
            <td>{device.deviceId}</td>
            <td>{device.ownerEmail}</td>
            <td>{device.online ? 'Online' : 'Offline'}</td>
            <td>{device.ledState}</td>
            <td>
              <button onClick={() => handleReassignOwner(device.id, prompt('New owner email:'))}>
                Reassign
              </button>
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  );
};
```

---

**Document Status:** Complete for Phases A-D. Phase E (OTA) endpoint will be updated during implementation.

**Maintainer:** Backend team

**Changelog:**
- 2026-08-28: Initial version (Phases A-D endpoints documented)
