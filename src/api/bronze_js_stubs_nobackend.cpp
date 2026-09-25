// No-op entry stub for the Bronze-compiled js/flora.js, linked only on a
// target brass has no code generator for (BRASS_HOST_BACKEND OFF; x86_64 and
// AArch64, Apple Silicon included, compile flora.js natively). It satisfies
// the static entry point required by broflora_api.

extern "C" {
void bronze_flora_main() {}
}
