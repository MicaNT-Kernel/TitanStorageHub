# Titan Storage Hub Clean-Room Provenance & Compliance Declaration

## 1. Executive Summary & Purpose
**Titan Storage Hub** is an independent, sovereign, clean-room modern ISO C++23 desktop storage manager and unified drive control center. The purpose of this project is to achieve seamless management, drive detection, and userland mounting for the Titan Storage Suite (**TitanZFS**, **TitanReFS**, **TitanNTFS**, and **TitanBtrfs**) on Windows NT and MicaNT without proprietary dependencies or kernel driver instability.

---

## 2. Legal Framework & Fair Use Interoperability
This implementation is developed in accordance with established United States and international copyright laws and fair use precedents governing computer program interoperability:
- **17 U.S.C. § 1201(f) (Reverse Engineering & Interoperability Exemption)**: Expressly permits functional analysis and implementation for enabling interoperability of independently created software.
- **Google LLC v. Oracle America, Inc. (141 S. Ct. 1183, 2021)**: Confirmed that functional declarations, data formats, and API structures essential for software compatibility and interoperability fall squarely within fair use.
- **Sega Enterprises Ltd. v. Accolade, Inc. (977 F.2d 1510, 9th Cir. 1992)** & **Sony Computer Entertainment, Inc. v. Connectix Corp. (203 F.3d 596, 9th Cir. 2000)**: Established the clean-room "Chinese Wall" standard as a legally valid mechanism to create compatible implementations.

---

## 3. Strict Clean-Room Rules & Zero-Exposure Guarantee
1. **Zero Proprietary Code Contamination**:
   - The codebase does not copy, borrow, or link against any proprietary Microsoft internal code or GPL copyleft source.
2. **Original ISO C++23 Expression**:
   - All GUI controls, Win32 drawing routines, IPC client state machines, and volume detection algorithms are 100% original creative expressions implemented in modern C++23.
3. **Public Technical References**:
   - Filesystem magic signatures and Win32 APIs trace strictly to public standard formats and public Microsoft documentation.
4. **Automated Clean-Room Sentinel CI Enforcement**:
   - Every commit and pull request is scrutinized by an automated Clean-Room Sentinel (`scripts/clean_room_sentinel.js`) to guarantee compliance.
