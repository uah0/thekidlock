# Character controls implementation plan

Source issue: #8.

This feature branch is based on the current main development line, not on the Google Play release branch.

## Architecture

- Store the approved 1080 x 2400 JPEG as the normal binary Android resource
  `app/src/main/res/drawable-nodpi/home_background.jpg`.
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
- The packaged APK/AAB contain `home_background.jpg` as a drawable resource;
  no Base64 transport or runtime image decoder is used.
- Character hit regions remain aligned on 16:9, 19.5:9, and 20:9 screens.
- No background is drawn under Android system bars.
- Privacy Policy remains active and pinned at the bottom.
- Existing lock functions pass hardware regression.

## Local verification (24.08.2026)

- `gradlew.bat --no-daemon lintRelease bundleRelease assembleDebug`: successful.
- APK and AAB each contain one unmodified `home_background.jpg` with 812030 bytes
  and SHA-256 `9a7fdb22aff298d1c562c297d9f085daae99999dc898cd373880890f1f44d9e1`.
- The 1080 x 1920, 1080 x 2340 and 1080 x 2400 transforms use one uniform
  scale and produce zero overlap between all three hit regions.
