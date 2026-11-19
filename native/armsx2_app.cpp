// SW2PS2 on the ARMSX2 core: two pieces of OunceSX2's own Switch 2 app (GPL, Copyright their authors;
// https://github.com/nyathea/OunceSX2) that the core relies on at run time:
//  - HorizonThreadWrap: pthread_create/join wrapped (stack sizes and core placement for PCSX2's threads)
//  - ServiceLookupGuard: service lookups that kaiHEN would leave waiting are refused instead
// Linked with -Wl,--wrap=pthread_create,--wrap=pthread_join,--wrap=smGetServiceWrapper,--wrap=smGetServiceOriginal.
#ifdef SW2_ARMSX2
#include "../third_party/ouncesx2-eval/src/switch/app/HorizonThreadWrap.cpp"
#include "../third_party/ouncesx2-eval/src/switch/app/ServiceLookupGuard.cpp"
#endif
