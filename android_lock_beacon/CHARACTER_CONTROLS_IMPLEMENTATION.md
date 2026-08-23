# Character controls implementation plan

Source issue: #8.

This feature branch is based on the current main development line, not on the Google Play release branch.

## Architecture

- Keep the three existing actions and BLE/Web UI/OTA handlers unchanged.
- Render the approved background image inside the app content area, excluding Android status/navigation bars.
- Define each character hit region in normalized source-image coordinates (0..1).
- Compute the same center-crop scale and translation used for image rendering and transform hit regions with that matrix.
- Enforce a minimum accessible touch size without changing the visual image.
- Keep the Privacy Policy as a pinned bottom row above the Android navigation area.
- Do not show application or ESP32 version labels on the home screen.

## Release-fix migration boundary

Port only production-safe Android fixes from `play-release-v2`:
- release R8/resource shrinking and release Logcat stripping;
- legacy location permission pairing;
- safe dynamic receiver registration;
- API 26/27/28 compatibility fixes;
- Android 13+ GATT status handling;
- normal battery-settings flow (no direct exemption permission);
- automatic-mode recovery after app update;
- clean bottom Privacy Policy footer.

Do not port:
- Base64 background chunks;
- `BleLockApplication` runtime background decoder;
- temporary release-only background resource transport.

## Acceptance

- `lintRelease bundleRelease assembleDebug` pass.
- Character hit regions remain aligned on 16:9, 19.5:9, and 20:9 screens.
- No background is drawn under Android system bars.
- Privacy Policy remains active and pinned at the bottom.
- Existing lock functions pass hardware regression.
