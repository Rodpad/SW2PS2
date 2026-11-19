// jitdis <code file> <base hex> <start hex> <end hex>: disassemble a range of a
// dumped EE code cache (SW2_JIT_DUMP) with vixl's disassembler.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "aarch64/decoder-aarch64.h"
#include "aarch64/disasm-aarch64.h"
using namespace vixl::aarch64;
int main(int argc, char** argv) {
  if (argc < 5) return 2;
  FILE* f = fopen(argv[1], "rb"); if (!f) return 1;
  std::vector<unsigned char> code; int c; while ((c = fgetc(f)) != EOF) code.push_back((unsigned char)c); fclose(f);
  unsigned long long base = strtoull(argv[2], 0, 16), a = strtoull(argv[3], 0, 16), b = strtoull(argv[4], 0, 16);
  if (a < base || b > base + code.size() || b <= a) { printf("range outside the dump\n"); return 1; }
  Decoder decoder; PrintDisassembler dis(stdout); decoder.AppendVisitor(&dis);
  const Instruction* start = reinterpret_cast<const Instruction*>(&code[a - base]);
  const Instruction* end = reinterpret_cast<const Instruction*>(&code[b - base]);
  dis.MapCodeAddress(a, start);
  decoder.Decode(start, end);
  return 0;
}
