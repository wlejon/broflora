// Native arm64 entry stubs for Bronze compiled JS polyfills on Apple Silicon.
// Bronze code generator is x86_64; on Apple Silicon (arm64), these stubs
// satisfy the static entry points required by broflora_api.

extern "C" {
void bronze_flora_main() {}
}
