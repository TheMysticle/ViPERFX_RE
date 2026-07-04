// Alias NDK libc++ atomic to AOSP libc++ atomic for EventFlag::createEventFlag
__asm__(".global _ZN7android8hardware9EventFlag15createEventFlagEPNSt6__ndk16atomicIjEEPPS1_\n"
        ".set _ZN7android8hardware9EventFlag15createEventFlagEPNSt6__ndk16atomicIjEEPPS1_, _ZN7android8hardware9EventFlag15createEventFlagEPNSt3__16atomicIjEEPPS1_\n");
