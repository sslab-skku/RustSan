#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Format.h"

#include <chrono>

#include "statistics.hh"

using namespace llvm;
void Statistics::addSelectiveInst(Instruction *I) {
  total_selective_counter++;
  fList->insert(I->getFunction());
  (*selective_instuction)[I->getOpcode()] += 1;
  if (auto MI = dyn_cast<MemIntrinsic>(I)) {
    if (isa<MemTransferInst>(MI))
      selective_memintrinsics += 1;
  }
}
void Statistics::addInstruction(Instruction *I) {
  total_instruction_counter++;
  (*total_instuction)[I->getOpcode()] += 1;
  if (auto MI = dyn_cast<MemIntrinsic>(I)) {
    if (isa<MemTransferInst>(MI))
      total_memintrinsics += 1;
  }
}

void Statistics::addUnsafeInst(Instruction *I) {
  total_unsafe_counter++;
  (*unsafe_instuction)[I->getOpcode()] += 1;
  if (auto MI = dyn_cast<MemIntrinsic>(I)) {
    if (isa<MemTransferInst>(MI))
      unsafe_memintrinsics += 1;
  }
}

void Statistics::printResult() {
  errs() << "##### instruction statistics #####\n";
  errs() << "total instructions:               " << total_instruction_counter
         << "\n";
  errs() << "\n";
  uint64_t tmp_total = (*total_instuction)[Instruction::Load] +
                       (*total_instuction)[Instruction::Store] +
                       // (*total_instuction)[Instruction::Invoke] +
                       // (*total_instuction)[Instruction::Call] +
                       (*total_instuction)[Instruction::AtomicCmpXchg] +
                       (*total_instuction)[Instruction::AtomicRMW];
  uint64_t tmp_unsafe = (*unsafe_instuction)[Instruction::Load] +
                        (*unsafe_instuction)[Instruction::Store] +
                        // (*unsafe_instuction)[Instruction::Invoke] +
                        // (*unsafe_instuction)[Instruction::Call] +
                        (*unsafe_instuction)[Instruction::AtomicCmpXchg] +
                        (*unsafe_instuction)[Instruction::AtomicRMW];
  uint64_t tmp_selective = (*selective_instuction)[Instruction::Load] +
                           (*selective_instuction)[Instruction::Store] +
                           // (*selective_instuction)[Instruction::Invoke] +
                           // (*selective_instuction)[Instruction::Call] +
                           (*selective_instuction)[Instruction::AtomicCmpXchg] +
                           (*selective_instuction)[Instruction::AtomicRMW];
  errs() << "total memory instructions:        " << tmp_total << "\n";
  errs() << "  total Load instructions:        "
         << (*total_instuction)[Instruction::Load] << "\n";
  errs() << "  total Store instructions:       "
         << (*total_instuction)[Instruction::Store] << "\n";
  errs() << "  total AtomicCmpXchg instructions:      "
         << (*total_instuction)[Instruction::AtomicCmpXchg] << "\n";
  errs() << "  total AtomicRMW instructions:        "
         << (*total_instuction)[Instruction::AtomicRMW] << "\n";
  errs() << "  total MemTransferInst instructions:        "
         << total_memintrinsics << "\n\n";

  errs() << "total Unsafe memory instructions:        " << tmp_unsafe << "("
         << format("%.2f", (double)tmp_unsafe / (double)tmp_total * 100)
         << "%)\n";
  errs() << "  total load instructions:        "
         << (*unsafe_instuction)[Instruction::Load] << "\n";
  errs() << "  total store instructions:       "
         << (*unsafe_instuction)[Instruction::Store] << "\n";
  errs() << "  total AtomicCmpXchgInst instructions:      "
         << (*unsafe_instuction)[Instruction::AtomicCmpXchg] << "\n";
  errs() << "  total AtomicRMWInst instructions:        "
         << (*unsafe_instuction)[Instruction::AtomicRMW] << "\n";
  errs() << "  total MemTransferInst instructions:        "
         << unsafe_memintrinsics << "\n\n";

  errs() << "total Selective instructions:        " << tmp_selective << "("
         << format("%.2f", (double)tmp_selective / (double)tmp_total * 100)
         << "%)\n";
  errs() << "  total load instructions:        "
         << (*selective_instuction)[Instruction::Load] << "\n";
  errs() << "  total store instructions:       "
         << (*selective_instuction)[Instruction::Store] << "\n";
  errs() << "  total AtomicCmpXchgInst instructions:      "
         << (*selective_instuction)[Instruction::AtomicCmpXchg] << "\n";
  errs() << "  total AtomicRMWInst instructions:        "
         << (*selective_instuction)[Instruction::AtomicRMW] << "\n";
  /*
  errs() << "  total invoke instructions:      "
         << (*selective_instuction)[Instruction::Invoke] << "\n";
  errs() << "  total call instructions:      "
         << (*selective_instuction)[Instruction::Call] << "\n";
  */
  errs() << "  total MemTransferInst instructions:        "
         << selective_memintrinsics << "\n\n";

  errs() << "total Pointer Query:\t\t" << pointer_query << "\n\n";
}
