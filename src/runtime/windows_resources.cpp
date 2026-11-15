#include <stdint.h>
#if defined(_MSC_VER)
#pragma section(".apicon", read, write)
extern "C" __declspec(allocate(".apicon")) volatile uint8_t ap_icon_slot[262144] = {
#else
extern "C" __attribute__((used, section(".apicon"), aligned(16))) volatile uint8_t ap_icon_slot[262144] = {
#endif
    'A', 'P', 'I', 'C', 'O', 'N', 'R', 'E', 'S', 'E', 'R', 'V', 'E', '1', 0xa7, 0xd2, 0x6e};
