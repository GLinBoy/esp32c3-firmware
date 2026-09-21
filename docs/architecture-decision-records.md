# Architecture Decision Records (ADR)

This document tracks all significant architectural and technical decisions made during the ESP32 IoT Ecosystem project development. Each decision is recorded with context, rationale, consequences, and alternatives considered.

**Format:** Each ADR follows a lightweight format:
- **Decision:** What was decided
- **Context:** Why the decision was needed
- **Rationale:** Why this option was chosen
- **Alternatives Considered:** What other options were evaluated
- **Consequences:** Impact and tradeoffs
- **Status:** Active, Superseded, or Deprecated
- **Date:** When the decision was made
- **Phase:** Which project phase this applies to

---

## ADR-001: Use EMQX Cloud Serverless as Managed MQTT Broker

**Decision:** Use EMQX Cloud Serverless (free tier) as the MQTT broker, replacing the public `test.mosquitto.org` broker.

**Context:**
- The project started with `test.mosquitto.org` for prototyping (public, unauthenticated, unencrypted)
- Moving to production requires a private broker with authentication and TLS
- Current scale: 1 user, 3 devices (hobby project)
- Learning goal: IoT ecosystem development, not infrastructure management

**Rationale:**
- **Focus on learning:** Managed service allows focus on IoT patterns rather than broker administration
- **Free tier sufficient:** 1M session minutes/month, 1GB traffic/month covers 3 devices easily
- **Built-in security:** TLS + per-device authentication included
- **REST API:** Programmatic device provisioning via API (needed for backend automation)
- **Quick setup:** Minutes to provision vs. hours to self-host with proper security

**Alternatives Considered:**
1. **Self-hosted Mosquitto with TLS + auth:**
   - Pros: Full control, deeper learning of broker administration, no vendor lock-in
   - Cons: Requires VM/hosting ($3-5/month or desktop-hosted), certificate management, manual ACL configuration
   - Rejected: Deferred as secondary learning goal; can migrate later if desired

2. **AWS IoT Core:**
   - Pros: Includes device registry + shadow + remote commands (replaces some backend features), free tier adequate
   - Cons: Third cloud provider (already using Firebase), AWS-specific MQTT variant, more complex setup
   - Rejected: Adds vendor dependency; tracked as future alternative if Firebase limitations encountered

3. **HiveMQ Cloud:**
   - Pros: Similar to EMQX, generous free tier
   - Cons: Slightly less generous free tier than EMQX, less familiar
   - Rejected: EMQX chosen for better free-tier limits and REST API documentation

**Consequences:**
- **Positive:** Fast implementation, reliable service, no maintenance burden, automatic TLS cert renewal
- **Negative:** Vendor lock-in (mitigated: MQTT is standard protocol, migration path exists), free tier limits (mitigated: monitoring + upgrade path documented)
- **Tradeoff:** Less hands-on experience with broker administration vs. faster progress on app/backend

**Status:** Active

**Date:** 2026-08-28

**Phase:** A - Replace Public MQTT Broker

---

## ADR-002: Backend HTTPS/Cloud Functions Only (No Direct App-to-MQTT)

**Decision:** The mobile app will NOT connect to the MQTT broker directly. All device control goes through Firebase Cloud Functions via HTTPS.

**Context:**
- Standard IoT pattern: app connects directly to MQTT broker
- Security concern: app must securely store MQTT credentials, enforce per-user device access
- Spring Boot background: familiar with REST API + backend authorization patterns

**Rationale:**
- **Centralized authorization:** All ownership checks in one place (Cloud Functions), not replicated in app
- **Simpler security model:** No credential management in app, no MQTT ACL synchronization
- **Familiar pattern:** REST API over HTTPS aligns with Spring Boot experience
- **Auditability:** All control actions logged in Cloud Functions (Cloud Logging)
- **Flexibility:** Easy to add rate limiting, complex authorization logic, or backend business rules without app updates

**Alternatives Considered:**
1. **App connects to MQTT directly with per-user credentials:**
   - Pros: Lower latency (no backend hop), real-time bidirectional communication
   - Cons: Credential management in app (refresh tokens, expiry), ACL configuration complexity (per-user topic scopes), harder to audit
   - Rejected: Security and auditability concerns outweigh latency benefit for a hobby-scale LED control app

2. **Hybrid: Direct MQTT for control, HTTPS for management:**
   - Pros: Combines low latency with backend security for high-privilege actions
   - Cons: Two communication paths to maintain, still requires app MQTT credential management
   - Rejected: Added complexity not justified at current scale

**Consequences:**
- **Positive:** Single authorization point, simpler app architecture (no MQTT client), easier to audit/debug, credentials never leave backend
- **Negative:** Additional network hop adds ~50-200ms latency (acceptable for LED control, not acceptable for real-time gaming), backend becomes single point of failure (mitigated: Firebase Cloud Functions auto-scale and have high SLA)
- **Tradeoff:** Slight latency increase vs. significantly simpler security model

**Status:** Active

**Date:** 2026-08-28

**Phase:** C - Device Control Services

---

## ADR-003: Server-Side Device Ownership in Firestore

**Decision:** Device ownership is stored server-side in Firestore, not just locally in the app.

**Context:**
- Original implementation: ownership tracked only in app's `shared_preferences` (local storage)
- Real problem encountered: app reinstall caused loss of all device associations, requiring manual factory reset of physical devices to re-provision
- Recovery required Python script to send MQTT reset commands

**Rationale:**
- **Recovery after app reinstall:** User can reinstall app, log in with same Firebase account, and immediately see all owned devices
- **Multi-device support (future):** User can access devices from multiple phones/tablets with same account
- **Backend operations:** Enables remote device management (reset, ownership transfer) from backend/admin panel
- **Audit trail:** Server-side records enable tracking of ownership history, provisioning events

**Alternatives Considered:**
1. **Keep ownership local-only:**
   - Pros: Simpler initial implementation, no backend required, works offline
   - Cons: Loss of ownership on app reinstall (already experienced this pain), no multi-device support, no remote management
   - Rejected: Pain point already encountered in development; hobby project benefit of learning backend patterns outweighs simplicity

2. **Device stores owner ID on-device (in NVS):**
   - Pros: Device "knows" its owner independently of backend
   - Cons: Device can only store one owner (no sharing support later), ownership changes require re-provisioning device, no audit trail
   - Rejected: Backend ownership more flexible for future features

**Consequences:**
- **Positive:** Solves app reinstall problem, enables future sharing/multi-user features, enables remote management
- **Negative:** Requires backend (already building one), ownership state can diverge if Firestore and device both claim different owners (mitigated: Firestore is source of truth)
- **Tradeoff:** Increased backend complexity vs. dramatically better user experience

**Status:** Active

**Date:** 2026-08-28

**Phase:** B - Firebase Authentication

---

## ADR-004: Post-Discovery Device Claim (No Firmware Change for Phase B/C)

**Decision:** Device provisioning (BLE → Wi-Fi credentials) is separate from ownership claiming. User provisions device via BLE, device comes online, then user claims it via app.

**Context:**
- Need to establish ownership during device onboarding
- Two options: claim during BLE provisioning (requires firmware change) vs. claim after device is online (no firmware change)
- Hobby project goal: iterative development, minimize firmware changes in early phases

**Rationale:**
- **No firmware change needed:** Existing BLE provisioning protocol unchanged in Phase B/C
- **Simpler implementation:** Claim is a simple backend API call after device is discovered
- **Iterative approach:** Get ownership working end-to-end before adding BLE protocol complexity
- **Acceptable risk:** On a private authenticated broker, the window between provision and claim (seconds) has negligible race risk

**Alternatives Considered:**
1. **Claim during BLE provisioning:**
   - Pros: Atomic operation (provision = claim), no window where device is "unclaimed"
   - Cons: Requires firmware change (extend BLE protocol to accept claim token), adds complexity to BLE flow
   - Deferred: Can be implemented later if claim-after-discovery proves problematic; firmware change tracked as future enhancement

2. **Auto-claim on first MQTT connect:**
   - Pros: No explicit claim step, device auto-assigned to user who provisioned it
   - Cons: Assumes provisioner = owner (breaks if user provisions device for someone else), no explicit consent flow
   - Rejected: Less flexible than explicit claim

**Consequences:**
- **Positive:** Faster implementation (no firmware change), easier testing (claim is separate API call), explicit user action (user sees "Claim" button, understands ownership concept)
- **Negative:** Small race window where malicious user on same broker could claim device first (negligible risk on authenticated private broker), two-step UX (provision then claim) vs. one-step
- **Tradeoff:** Implementation speed and simplicity vs. theoretical race condition

**Status:** Active

**Date:** 2026-08-28

**Phase:** B - Firebase Authentication

---

## ADR-005: Separate Repositories Per Concern

**Decision:** Use separate git repositories for firmware, mobile app, backend (Cloud Functions), admin dashboard, and infrastructure code.

**Context:**
- Multi-component IoT ecosystem: embedded firmware (C), mobile app (Dart/Flutter), backend (Node.js/TypeScript), admin UI (React/Vue), infrastructure (Pulumi)
- Developer background: Java/Spring Boot (typically monorepo or multi-module Maven/Gradle projects)
- Need to manage different deployment lifecycles, build tools, and CI/CD pipelines

**Rationale:**
- **Clean separation:** Each repo has focused purpose, clear boundaries
- **Independent deployment:** Deploy firmware without touching backend, update admin UI without rebuilding app
- **Mirrors Spring Boot patterns:** Separate repos similar to separate Spring Boot microservices
- **Technology-specific tooling:** Each repo can have its own build system (PlatformIO/ESP-IDF, Flutter, npm, Pulumi) without conflicts
- **Granular access control (future):** If collaborating later, can grant access per repo (e.g., firmware engineer doesn't need admin UI access)

**Alternatives Considered:**
1. **Monorepo (all components in one repo):**
   - Pros: Atomic cross-component changes, single version number, simplified cross-repo refactoring
   - Cons: Large repo, mixed build systems, harder to deploy independently, single CI/CD pipeline must handle all tech stacks
   - Rejected: Complexity of unified CI/CD outweighs atomic commit benefit at current scale

2. **Firmware + app together, backend + admin separate:**
   - Pros: Device-side code together, backend code together
   - Cons: Still mixed build systems in each repo (PlatformIO + Flutter, Node.js + React), unclear boundary
   - Rejected: Still mixes concerns without clear benefit

**Consequences:**
- **Positive:** Clear boundaries, technology-specific tooling per repo, independent deployment, easier to onboard contributors to specific components
- **Negative:** Cross-repo coordination for breaking changes (e.g., MQTT topic changes affect firmware + backend), version management across repos (mitigated: PLAN.md tracks cross-repo dependencies)
- **Tradeoff:** Coordination overhead vs. cleaner separation and independent deployment

**Status:** Active

**Date:** 2026-08-28

**Phase:** All phases

---

## ADR-006: PlatformIO → ESP-IDF Migration Required for Matter

**Decision:** Migrate firmware build system from PlatformIO to native ESP-IDF (`idf.py`) in Phase E to support Matter integration.

**Context:**
- Current firmware: PlatformIO + ESP-IDF framework (Phases A-D)
- Matter requirement: esp-matter SDK v1.4.0+ requires ESP-IDF v6.0.2+
- Research finding: esp-matter SDK has zero PlatformIO support; all documentation uses native ESP-IDF build system

**Rationale:**
- **Matter SDK requirement:** esp-matter is distributed as ESP-IDF component only, no PlatformIO compatibility
- **Official support:** Espressif's Matter documentation exclusively covers native ESP-IDF builds
- **Build system only:** Firmware C code remains unchanged, only build configuration migrates (CMakeLists.txt replaces platformio.ini)
- **Learning value:** Experience both PlatformIO (Phases A-D) and native ESP-IDF (Phase E+), aligns with "learning-focused" project goal

**Alternatives Considered:**
1. **Stay on PlatformIO, skip Matter:**
   - Pros: No build system migration needed
   - Cons: Eliminates multi-ecosystem integration (Alexa/Google/SmartThings), which is a key project goal
   - Rejected: Matter integration is explicit project requirement

2. **Wait for PlatformIO esp-matter support:**
   - Pros: Avoid migration
   - Cons: No indication PlatformIO will add support, would block project indefinitely
   - Rejected: No timeline for PlatformIO support, project would stall

3. **Build Matter as separate firmware, maintain two versions:**
   - Pros: Keep PlatformIO for MQTT-only firmware
   - Cons: Maintain two codebases, users must choose firmware version, no hybrid MQTT + Matter support
   - Rejected: Maintenance burden too high for hobby project

**Consequences:**
- **Positive:** Unlocks Matter support, learn native ESP-IDF (deeper Espressif ecosystem knowledge), official SDK support and documentation
- **Negative:** One-time migration effort (estimated 1-2 hours for build config conversion), build commands change (pio → idf.py), IDE integration changes (if using VS Code PlatformIO extension)
- **Tradeoff:** Migration effort vs. Matter support and deeper ESP-IDF knowledge

**Status:** Active (Phase E)

**Date:** 2026-08-28

**Phase:** E - Matter Integration

---

## ADR-007: Matter OnOff Cluster (Standard Device Type)

**Decision:** Implement LED control as a standard Matter OnOff cluster (device type: OnOff Light), not a custom cluster.

**Context:**
- Matter supports standard device types (OnOff Light, Dimmer, Color Light, etc.) and custom clusters
- LED is a simple on/off device (no dimming, no color)
- Goal: Control via Alexa, Google Home, SmartThings, Apple Home

**Rationale:**
- **Maximum compatibility:** Standard OnOff cluster is supported by all Matter controllers (Alexa/Google/SmartThings/Apple Home) out of the box
- **No vendor-specific integrations:** Avoid building custom Alexa Skills, Google Actions, or SmartThings Edge Drivers
- **Simplest implementation:** Matter SDK provides OnOff Light template, minimal code required
- **Interoperability:** Any Matter-compatible controller can control device without custom app

**Alternatives Considered:**
1. **Custom Matter cluster for LED:**
   - Pros: Could add custom attributes (e.g., blink pattern, brightness curve)
   - Cons: Requires custom app/controller to understand custom cluster, breaks interoperability with standard Matter controllers
   - Rejected: Current device is simple on/off only, no custom features needed

2. **Build vendor-specific integrations (Alexa Skill, Google Action, SmartThings Edge Driver):**
   - Pros: Full control over UX in each ecosystem
   - Cons: 3-4x implementation effort (one integration per ecosystem), vendor lock-in, maintenance burden (must update for each ecosystem's API changes)
   - Rejected: Matter's native support eliminates need for custom integrations

**Consequences:**
- **Positive:** Works with all Matter ecosystems out of the box, minimal implementation effort, future-proof (new Matter controllers automatically compatible)
- **Negative:** Limited to standard OnOff capabilities (can't add custom features without breaking compatibility), no vendor-specific UI customization
- **Tradeoff:** Standard compatibility vs. custom features (acceptable for simple LED device)

**Status:** Active (Phase E)

**Date:** 2026-08-28

**Phase:** E - Matter Integration

---

## ADR-008: Firebase Spark Plan Initially, Upgrade to Blaze If Needed

**Decision:** Start with Firebase Spark plan (free tier) for backend, upgrade to Blaze plan (pay-as-you-go) only if free tier limits are exceeded.

**Context:**
- Current scale: 1 user, 3 devices
- Firebase Spark limits: 50K Firestore reads/day, 20K writes/day, 10GB storage, 10GB/month bandwidth
- Backend usage: Device state updates (1 write per LED toggle), device list queries (1 read per app open)

**Rationale:**
- **Free tier adequate:** At 3 devices, even aggressive usage (100 LED toggles/day, 20 app opens/day) = ~300 writes + 20 reads = well within limits
- **Pay-as-you-go safety net:** Blaze plan charges only for usage above free tier, no risk of surprise bills at low scale
- **Learning focus:** $0/month keeps project accessible, aligns with hobby scale
- **Monitoring:** Firebase Console shows usage dashboards, alerts before hitting limits

**Alternatives Considered:**
1. **Start on Blaze plan:**
   - Pros: No risk of hitting limits, enables Cloud Functions with external API calls (required for EMQX API)
   - Cons: Requires credit card even at $0 usage, psychological barrier to "paid" plan
   - Note: Will upgrade to Blaze in Phase A for EMQX API access (external network calls require Blaze)

2. **Self-hosted backend (Node.js + MongoDB/PostgreSQL):**
   - Pros: No limits, full control
   - Cons: Hosting cost (~$5-10/month), maintenance burden, doesn't align with "learn serverless" goal
   - Rejected: Firebase is part of the learning objectives

**Consequences:**
- **Positive:** $0 cost at current scale, automatic scaling, no maintenance, can upgrade seamlessly when needed
- **Negative:** Must monitor usage to avoid hitting limits (mitigated: Firebase dashboard + alerts), some features require Blaze (external API calls, longer Cloud Function timeout)
- **Tradeoff:** Free tier limits vs. zero cost and zero maintenance

**Status:** Active, will upgrade to Blaze in Phase A (required for EMQX API calls from Cloud Functions)

**Date:** 2026-08-28

**Phase:** B, C, D

---

## ADR-009: Admin Custom Claim (Single Firebase Project)

**Decision:** Admin access is controlled by Firebase Auth custom claim (`{ admin: true }`) in the same Firebase project as regular users, not a separate project or user pool.

**Context:**
- Need admin dashboard for fleet management (view all devices, reassign ownership, trigger OTA)
- Admin users are distinct from end users (different privileges)
- Hobby scale: 1-2 admins (developer + potentially one other person)

**Rationale:**
- **Simplicity:** Single Firebase project, single Auth pool, role-based access via custom claims
- **Firebase-native pattern:** Custom claims are designed for this use case (admin/moderator roles)
- **No additional cost:** Same project, same free tier
- **Sufficient security:** Custom claims verified server-side in Cloud Functions, UI checks claim before rendering admin UI
- **Hobby scale:** At 1-2 admins, separate user pool is overkill

**Alternatives Considered:**
1. **Separate Firebase project for admin:**
   - Pros: Complete isolation, admin can't accidentally use user app
   - Cons: Two projects to manage, two deployments, double the Firestore/Functions quotas to monitor, admin functions can't access user Firestore data easily
   - Rejected: Complexity not justified at hobby scale

2. **Separate auth provider (e.g., email whitelist, separate identity provider):**
   - Pros: Admin users completely separate from end users
   - Cons: More complex auth flow, still need custom claims for backend authorization
   - Rejected: Custom claims sufficient for authorization

**Consequences:**
- **Positive:** Simple implementation, single project to manage, native Firebase pattern, works with existing Firestore data
- **Negative:** Admin users in same user pool as end users (mitigated: custom claim required for admin endpoints, admin UI separate), no physical separation (acceptable for hobby project)
- **Tradeoff:** Simplicity vs. complete isolation

**Status:** Active

**Date:** 2026-08-28

**Phase:** D - Admin Dashboard

---

## ADR-010: Per-Device MQTT Credentials (Backend-Generated)

**Decision:** Each device receives unique MQTT username/password credentials, generated by backend during provisioning and delivered to device via BLE.

**Context:**
- Moving from public unauthenticated broker (`test.mosquitto.org`) to private EMQX broker
- Need to authenticate devices and enforce topic-level access control (device can only pub/sub to its own topics)
- Two options: shared credentials (all devices use same login) vs. per-device credentials

**Rationale:**
- **True per-device authentication:** Each device has its own identity on broker (username = `device_<device_id>`)
- **Credential revocation:** Backend can revoke single device's access without affecting other devices (needed for "remove device" feature)
- **ACL enforcement:** EMQX ACLs configured per device (device can only access `devices/<device_id>/#` topics)
- **Audit trail:** Broker logs show per-device activity (who published what)
- **Security best practice:** No shared secrets across devices

**Alternatives Considered:**
1. **Shared MQTT credentials (all devices use same username/password):**
   - Pros: Simpler provisioning (hardcode credentials in firmware)
   - Cons: Can't revoke single device, can't enforce per-device ACLs, security weakness (one compromised device = all devices compromised)
   - Rejected: Doesn't support "remove device" requirement, security risk

2. **X.509 client certificates per device:**
   - Pros: Stronger security than username/password, certificate-based auth
   - Cons: More complex provisioning (generate cert per device, deliver via BLE), certificate management (expiry, rotation), EMQX free tier may not support client certs
   - Deferred: Username/password adequate for hobby scale, can migrate to certs later if needed

3. **Device self-generates credentials:**
   - Pros: No backend coordination needed
   - Cons: Backend can't control credential format, backend can't revoke (device could regenerate), ACL synchronization issues
   - Rejected: Backend must control credential lifecycle for "remove device" feature

**Consequences:**
- **Positive:** Per-device control and audit, supports revocation, enforces least-privilege access, enables "remove device" feature
- **Negative:** Backend must store credentials (Firestore), BLE provisioning protocol must include MQTT credentials (firmware change in Phase A), credentials transmitted over BLE (mitigated: BLE is local-range, optional encryption can be added later)
- **Tradeoff:** Credential management complexity vs. per-device control and security

**Status:** Active

**Date:** 2026-08-28

**Phase:** A - Replace Public MQTT Broker

---

## Summary of Active Decisions

| ADR | Title | Phase | Key Impact |
|-----|-------|-------|------------|
| 001 | EMQX Cloud Serverless | A | Managed MQTT broker, focus on IoT learning |
| 002 | Backend HTTPS Only | C | No direct app-to-MQTT, centralized authorization |
| 003 | Server-Side Ownership | B | Firestore ownership, enables recovery after reinstall |
| 004 | Post-Discovery Claim | B | No firmware change, claim after provisioning |
| 005 | Separate Repositories | All | Clean boundaries, independent deployment |
| 006 | ESP-IDF Migration | E | Required for Matter, build system change only |
| 007 | Matter OnOff Cluster | E | Standard cluster, works with all ecosystems |
| 008 | Firebase Spark/Blaze | B-D | Free tier initially, upgrade if needed |
| 009 | Admin Custom Claim | D | Single project, role-based access |
| 010 | Per-Device Credentials | A | Unique MQTT credentials per device |

---

## Future Decisions to Record

As the project progresses, add new ADRs for:
- OTA firmware update mechanism (Phase E)
- Device sharing model (if implemented)
- Multi-tenant architecture (if scaling beyond hobby)
- iOS app deployment strategy (Phase F)
- Device allowlisting implementation (if added)
- Migration from EMQX to self-hosted (if decided)
- Certificate-based MQTT auth (if upgraded from username/password)

---

**Document Status:** Active, will be updated as new decisions are made during implementation.

**Last Updated:** 2026-08-28
