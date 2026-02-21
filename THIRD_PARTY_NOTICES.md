# Third-Party Notices

This repository's original source code is licensed under MIT (see `LICENSE`).

Some optional runtime/build dependencies are licensed separately and are not
relicensed under MIT.

## 1) Qt (framework/runtime)

- Project use: Qt 6 (`QtQuick`, `QtQuickControls2`, `QtNetwork`,
  `QtMultimedia`) via CMake.
- License: Open-source/commercial dual licensing by The Qt Company.
- Open-source license details: LGPLv3/GPLv2/GPLv3 (module dependent).
- Reference:
  - https://doc.qt.io/qt-6/licensing.html
  - https://www.qt.io/licensing/

## 2) OpenSSL (crypto/TLS backend)

- Project use: optional OpenSSL integration for crypto/TLS functionality.
- License: Apache License 2.0.
- License text copy in this repo: `LICENSES/Apache-2.0.txt`.
- Upstream reference:
  - https://www.openssl.org/source/license.html

## 3) libcodec2 (voice codec, user-provided dynamic library policy)

- Project policy for public releases: libcodec2 binaries are not bundled by
  default. Users may install/provide libcodec2 dynamic libraries separately.
- License: GNU LGPL v2.1.
- License text copy in this repo: `LICENSES/LGPL-2.1.txt`.
- Source used for custom builds:
  - https://github.com/FriedOUDON/libcodec2
- Original upstream project:
  - https://github.com/drowe67/codec2
- License reference:
  - https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html

## Distribution Notes

- This project can be distributed as MIT for original code in this repository.
- Third-party components keep their own licenses; this file is provided to
  make that boundary explicit.
- If you redistribute third-party binaries (for example OpenSSL or libcodec2),
  you must comply with each component's license terms for notices and source
  availability requirements where applicable.
