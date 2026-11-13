// R11 Windows Store clang-cl's runtime does not automatically link
// compiler-rt __udivti3. Full upstream timebase uses unsigned __int128.
// Provide the actual unsigned 128/128 division operation in portable
// shift/subtract math: this is NOT a fake game HLE implementation.
//
// Both caller and callee compile with the SAME clang-cl Windows x64 ABI.
// Avoid using 128-bit / or % inside this function (which would recursively
// generate a call to this exact symbol).
#if !defined(__clang__) || !defined(_WIN32)
#error "R11 compiler-rt fallback is specifically for WindowsStore clang-cl"
#endif
using u128 = unsigned __int128;

extern "C" u128 __udivti3(u128 dividend, u128 divisor) noexcept {
  if (divisor == 0) __builtin_trap();
  if (dividend < divisor) return 0;
  if (divisor == 1) return dividend;
  u128 quotient = 0;
  u128 remainder = 0;
  for (int i = 127; i >= 0; --i) {
    // Preserve the 129th carry bit: remainder may exceed 2^127 before
    // the next shift, even though the low 128 bits wrap around.
    const bool carry = (remainder >> 127) != 0;
    remainder = (remainder << 1) | ((dividend >> i) & u128(1));
    if (carry || remainder >= divisor) {
      remainder -= divisor;
      quotient |= (u128(1) << i);
    }
  }
  return quotient;
}
