#include "llvm/IR/Instructions.h"
#include "llvm/Support/Format.h"

#include <map>
#include <set>
// #define DEBUG

using namespace llvm;

class Statistics {
private:
  uint64_t total_instruction_counter;
  uint64_t total_selective_counter;
  uint64_t total_unsafe_counter;
  uint64_t total_memintrinsics;
  uint64_t unsafe_memintrinsics;
  uint64_t selective_memintrinsics;
  uint64_t pointer_query;
  std::map<unsigned, uint64_t> *total_instuction;
  std::map<unsigned, uint64_t> *selective_instuction;
  std::map<unsigned, uint64_t> *unsafe_instuction;
  std::set<Function *> *fList;

public:
  Statistics() {
    total_memintrinsics = 0;
    unsafe_memintrinsics = 0;
    selective_memintrinsics = 0;
    total_instruction_counter = 0;
    total_selective_counter = 0;
    total_unsafe_counter = 0;
    pointer_query = 0;
    fList = new std::set<Function *>;
    total_instuction = new std::map<unsigned, uint64_t>;
    unsafe_instuction = new std::map<unsigned, uint64_t>;
    selective_instuction = new std::map<unsigned, uint64_t>;
  }
  ~Statistics() {
    delete total_instuction;
    delete unsafe_instuction;
    delete fList;
  }

  void addUnsafeInst(Instruction *I);
  void addSelectiveInst(Instruction *I);
  void addInstruction(Instruction *I);
  void recordquery() { pointer_query += 1; }
  void printResult();
};
