# Third-party notices

The repository's own code uses the license in [LICENSE](LICENSE). Vendored upstream files retain their own licenses. Build tools do not fetch moving branches.

| Component | Pinned source | License | Use |
| --- | --- | --- | --- |
| LZ4 | [lz4/lz4 v1.10.0](https://github.com/lz4/lz4/tree/v1.10.0/lib) | BSD 2-Clause for the library files; see [included license](third_party/lz4/LICENSE) | Block compression and freestanding decompression |
| elf2epf | [xingji-studio/elf2epf @ 67e02821851a89d610247fdf6a8819b8ac4aa90e](https://github.com/xingji-studio/elf2epf/tree/67e02821851a89d610247fdf6a8819b8ac4aa90e) | Apache-2.0; see [included license](third_party/elf2epf/LICENSE) | Host-side ELF-to-EPF conversion and validation |

The vendored `lz4.c` SHA-256 is `9396f7de527bc8435de9c7569fb7998e56545a84b4f3c2d808c0235c01774539`; `lz4.h` is `26b82efc53d1570f3b54eef02e9c4764c1ad374ff03cac04e2ced5ea4d4c552f`.

The optional workbench is built against a separately installed Qt 6 Widgets SDK. Portable GUI folders produced by `make package` include the Qt runtime and plugins collected from that SDK; available SDK license notices are copied into `licenses/`. Qt sources are not vendored in this repository, and Qt is not included in generated application installation runtimes. Distribution of the workbench must follow the license of the selected Qt distribution. Linux graphical installers load the system GTK 3 library. Windows installers use OS-provided Win32 / COM libraries.

XJ380 native bindings were independently written against the published [XAPI 1.4 specification](https://docs.xingjisoft.com/develop/xj380/XJ380_API_Spec_CPP_1_4) and checked against [XJ380-SDK @ 91df024ff5f346f4870b3dc042cbc0e5bf06a046](https://github.com/xingji-studio/XJ380-SDK/tree/91df024ff5f346f4870b3dc042cbc0e5bf06a046). The SDK is not vendored or linked. The OS ABI value, launch ABI and user desktop convention were checked against [OpenXJ380 @ f821630ceb0762635da2b3c3ff9611ee25efbe86](https://github.com/xingji-studio/OpenXJ380/tree/f821630ceb0762635da2b3c3ff9611ee25efbe86). These pins document the reference ABI; later OS changes require an explicit compatibility check.
