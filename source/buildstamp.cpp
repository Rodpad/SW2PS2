// The build's time (seconds since 1970, UTC), stamped by the Makefile on every build. Kept in a
// file of its own so only this rebuilds; read by the pre-release expiry check (main.cpp).
extern const long long sw2_build_time = SW2_BUILD_TIME;
