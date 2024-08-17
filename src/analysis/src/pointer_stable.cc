#include "DDA/ContextDDA.h"
#include "DDA/DDAPass.h"
#include "DDA/FlowDDA.h"
#include "Graphs/SVFG.h"
#include "MemoryModel/PointerAnalysis.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/SVFIRBuilder.h"
#include "Util/BasicTypes.h"
#include "Util/Options.h"
#include "WPA/Andersen.h"
#include "WPA/Steensgaard.h"

#include "llvm/Bitcode/LLVMBitCodes.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <chrono>
#include <vector>

#include "statistics.hh"

using namespace llvm;
using namespace std;
using namespace SVF;


// Decide the alias analysis algorithm
static cl::opt<bool>
    ClSteensAA("use-steens", cl::desc("Use Steensgaard Alias Analysis in SVF"));

static cl::opt<bool>
    ClFlosAA("use-flos", cl::desc("Use Flow-Sensetive Alias Analysis in SVF"));

static cl::opt<bool> ClSUPAAnalysis("use-supa",
                                    cl::desc("Use SUPA Analysis in SVF"));

// utils for statistics
static cl::opt<bool>
    ClUnsafeCounting("unsafe-counting",
                     cl::desc(""));

static cl::opt<bool>
    ClNaiveRustSan("naive",
                   cl::desc("naive RustSan only instrument the unsafe code"));

static cl::opt<bool>
    ClAnalysisDebug("rustsan-debug",
                    cl::desc("Use Steensgaard Alias Analysis in SVF"));

static cl::opt<bool> ClNoOutput("no-output",
                                cl::desc("Do not produce output file"));

static llvm::cl::opt<std::string>
    InputFilename(cl::Positional, llvm::cl::desc("<input bitcode>"),
                  llvm::cl::init("-"));

const char kUnsafeMetadata[] = "Unsafe.full";

bool isInterestingInstruction(Instruction *I) {
  if (auto MI = dyn_cast<MemIntrinsic>(I)) {
    if (!isa<MemTransferInst>(MI))
      return false;
  }
  if (!isa<LoadInst>(I) && !isa<StoreInst>(I) && !isa<AtomicRMWInst>(I) &&
      !isa<AtomicCmpXchgInst>(I))
    return false;

  return true;
}

void putUnsafeObjsSet(PointerAnalysis *pta, Value *val,
                      std::set<NodeID> &unsafeObjs) {
  NodeID pNodeId = pta->getPAG()->getValueNode(val);
  const PointsTo &pts = pta->getPts(pNodeId);
  for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie; ii++) {
    unsafeObjs.insert(*ii);
    PAGNode *targetObj = pta->getPAG()->getGNode(*ii);
    if (ClAnalysisDebug) {
      if (targetObj->hasValue())
        errs() << *targetObj->getValue() << "\n";
    }
  }
}

int main(int argc, char **argv) {
  Statistics s;
  int arg_num = 0;
  char **arg_value = new char *[argc];
  std::vector<std::string> moduleNameVec;
  LLVMUtil::processArguments(argc, argv, arg_num, arg_value, moduleNameVec);
  cl::ParseCommandLineOptions(arg_num, arg_value,
                              "Whole Program Points-to Analysis\n");
  std::set<NodeID> unsafeObjsID;

  if (Options::WriteAnder == "ir_annotator") {
    LLVMModuleSet::getLLVMModuleSet()->preProcessBCs(moduleNameVec);
  }

  std::chrono::system_clock clock;
  auto start_time = chrono::system_clock::to_time_t(clock.now());
  errs() << "[RustSan]" << clock.now() << ": Analyzer Start.\n";

  SVFModule *svfModule =
      LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(moduleNameVec);
  svfModule->buildSymbolTableInfo();

  if (ClUnsafeCounting) {
    uint64_t tot_counter = 0;
    uint64_t counter = 0;
    uint64_t tot_mem_counter = 0;
    uint64_t mem_counter = 0;

    for (auto &F : *svfModule) {
      Function *llvmF = F->getLLVMFun();
      for (auto &BB : *llvmF) {
        for (auto &I : BB) {
          tot_counter++;
          if (isInterestingInstruction(&I))
            tot_mem_counter++;
          if (I.hasMetadata(kUnsafeMetadata)) {
            counter++;
            if (isInterestingInstruction(&I))
              mem_counter++;
          }
        }
      }
    }
    errs() << "Total Unsafe Instruction: " << counter << "("
           << format("%.2f", (double)counter / (double)tot_counter * 100)
           << "%)\n";
    errs() << "Total Unsafe Memory Instruction: " << mem_counter << "("
           << format("%.2f",
                     (double)mem_counter / (double)tot_mem_counter * 100)
           << "%)\n";
    return 0;
  }
  if (ClNaiveRustSan) {
    Module *M = LLVMModuleSet::getLLVMModuleSet()->getMainLLVMModule();
    auto MD_nosan = M->getMDKindID("nosanitize");
    for (auto &F : *svfModule) {
      Function *llvmF = F->getLLVMFun();
      for (auto &BB : *llvmF) {
        for (auto &I : BB) {
          if (!I.hasMetadata(kUnsafeMetadata)) {
            I.setMetadata(MD_nosan, MDNode::get(M->getContext(), None));
          }
        }
      }
    }
    if (!ClNoOutput)
      LLVMModuleSet::getLLVMModuleSet()->dumpModulesToFile(".svf.bc");
    return 0;
  }

  SVFIRBuilder builder;
  SVFIR *pag = builder.build(svfModule);
  PointerAnalysis *ander = nullptr;
  DDAPass *dda = new DDAPass();
  if (ClSteensAA)
    ander = Steensgaard::createSteensgaard(pag);
  else if (ClFlosAA)
    ander = FlowSensitive::createFSWPA(pag);
  else if (ClSUPAAnalysis)
    dda->runOnModule(svfModule);
  else
    ander = AndersenWaveDiff::createAndersenWaveDiff(pag);

  auto pta_time = chrono::system_clock::to_time_t(clock.now());
  errs() << "[RustSan]" << clock.now() << ": "
         << (ClSteensAA ? "Steens" : "Andersen") << " Done.\n";

  std::set<const Value*> total_obj;
  for (auto &F : *svfModule) {
    for(auto &BB: *F->getLLVMFun()){
      for (auto &I: BB) {
        for (int i = 0; i < I.getNumOperands() + 1; ++i) {
          llvm::Value *arg;
          if (i == I.getNumOperands()) {
            // HACK: check return value of instruction in this loop
            if (I.getType()->isVoidTy()) {
              continue;
            } else {
              arg = llvm::cast<llvm::Value>(&I);
            }
          } else {
            arg = I.getOperand(i);
          } // arg = I.getOperand(i);

          NodeID pNodeId;
          if (ander->getPAG()->hasValueNode(arg))
            pNodeId = ander->getPAG()->getValueNode(arg);
          else
            break;
          const PointsTo &pts = ander->getPts(pNodeId);
          for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie;
               ii++) {
            PAGNode *targetObj = ander->getPAG()->getGNode(*ii);
            if (targetObj->hasValue())
              total_obj.insert(targetObj->getValue());
            }
          }
        }
      }
    }

  /// Phase 1: detect all unsafe objects
  for (auto &F : *svfModule) {
    for (auto &BB : *F->getLLVMFun()) {
      for (auto &I : BB) {
        if (I.getMetadata(kUnsafeMetadata) != nullptr) {
          s.addUnsafeInst(&I);
          for (int i = 0; i < I.getNumOperands(); ++i) {
            auto Op = I.getOperand(i);
            bool really = false;
            if (CallBase *CallBaseI = dyn_cast<CallBase>(&I)) {
              if (CallBaseI->getCalledFunction() == Op) {
                // this operand is called function...
                // function cannot be an unsafe object, right?
                continue;
              }
            }
            if (Op->getType()->isPointerTy()) {
              s.recordquery();
              putUnsafeObjsSet(ander, Op, unsafeObjsID);
            }
          }
          if (!I.getType()->isVoidTy()) {
            s.recordquery();
            putUnsafeObjsSet(ander, cast<Value>(&I), unsafeObjsID);
          }
        }
      }
    }
  }
  std::set<const Value *> unsafeObjs;
  std::set<const Value *> falseSafeObjs;
  for (auto obj : unsafeObjsID) {
    PAGNode *targetObj = ander->getPAG()->getGNode(obj);
    if (targetObj->hasValue()) {
      unsafeObjs.insert(targetObj->getValue());
    }
  }
  Module *M = LLVMModuleSet::getLLVMModuleSet()->getMainLLVMModule();
  auto MD_unsafe = M->getMDKindID(kUnsafeMetadata);
  auto MD_nosan = M->getMDKindID("nosanitize");
  auto MD_sel = M->getMDKindID("selective");
  errs() << "[RustSan]" << clock.now() << " UnsafeObjs[" << unsafeObjs.size()
         << "]\n";
  if (ClAnalysisDebug)
    errs() << "{\n";
  for (const Value *obj : unsafeObjs) {
    if (ClAnalysisDebug)
      errs() << *obj << "\n";
    const Instruction *I = dyn_cast<llvm::Instruction>(obj);
    if (I) {
      Instruction *II = const_cast<Instruction *>(I);
      II->setMetadata(MD_unsafe, MDNode::get(M->getContext(), None));
    } else if (const Function *F = dyn_cast<llvm::Function>(obj)) {
      // errs() << "Unsafe Function: "<< F->getName() << "\n";
      if (F->getName().contains("closure")) {
        llvm::Function *FF = const_cast<llvm::Function *>(F);
        FF->addFnAttr("unsafe_closure");
      }
    }
  }
  if (ClAnalysisDebug)
    errs() << "}\n";

  if (ClAnalysisDebug)
    errs() << "Selective Instructions:\n";

  for (auto &F : *svfModule) {
    Function *llvmF = F->getLLVMFun();
    if (llvmF->hasFnAttribute("unsafe_closure")) {
      if (ClAnalysisDebug)
        errs() << "We meet unsafe closure: " << llvmF->getName() << "\n";
      for (auto &BB : *llvmF) {
        for (auto &I : BB) {
          s.addInstruction(&I);
        }
        continue;
      }
    }
    bool functionflag = false;
    for (auto &BB : *llvmF) {
      for (auto &I : BB) {
        bool rustallocflag = false;
        s.addInstruction(&I);
        bool selective_flag = false;
        for (int i = 0; i < I.getNumOperands() + 1; ++i) {
          llvm::Value *arg;
          if (i == I.getNumOperands()) {
            // HACK: check return value of instruction in this loop
            if (I.getType()->isVoidTy()) {
              continue;
            } else {
              arg = llvm::cast<llvm::Value>(&I);
            }
          } else {
            arg = I.getOperand(i);
          }

          NodeID pNodeId;
          if (ander->getPAG()->hasValueNode(arg))
            pNodeId = ander->getPAG()->getValueNode(arg);
          else
            break;
          if (ClSUPAAnalysis) {
            if (!isInterestingInstruction(&I))
              continue;
            for (auto unsafeObj : unsafeObjs) {
              if (dda->alias(unsafeObj, arg))
                I.setMetadata(MD_sel, MDNode::get(M->getContext(), None));
            }
            continue;
          }
          const PointsTo &pts = ander->getPts(pNodeId);
          s.recordquery();
          bool fsflag = false;
          for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie;
               ii++) {
            const Value *obj = nullptr;
            PAGNode *targetObj = ander->getPAG()->getGNode(*ii);
            if (targetObj->hasValue())
              obj = targetObj->getValue();
            if (obj != nullptr && unsafeObjs.count(obj) != 0) {
              selective_flag = true;
              functionflag = true;
              fsflag = true;
            }
          }
          if (fsflag == true) {
            for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie;
                 ii++) {
              const Value *obj = nullptr;
              PAGNode *targetObj = ander->getPAG()->getGNode(*ii);
              if (targetObj->hasValue())
                obj = targetObj->getValue();
              if (obj != nullptr && unsafeObjs.count(obj) == 0) {
                falseSafeObjs.insert(obj);
              }
            }
          }
          if (rustallocflag) {
            errs() << "i: " << i << " (" << arg->getName() << ")"
                   << "\t" << selective_flag << "\n";
          }
          if (selective_flag) {
            I.setMetadata(MD_sel, MDNode::get(M->getContext(), None));
            I.setMetadata(MD_nosan, NULL);
            break;
          } else {
            if (!I.getMetadata("Unsafe.full") && !I.getMetadata("selective")) {
              I.setMetadata(MD_nosan, MDNode::get(M->getContext(), None));
            } else if (I.getMetadata("Unsafe.full")) {
              functionflag = true;
            }
          }
        }
      }
    }
    if (functionflag) {
      llvmF->addFnAttr(Attribute::SanitizeAddress);
    }
  }

  for (auto &F : *svfModule) {
    Function *llvmF = F->getLLVMFun();
    for (auto &BB : *llvmF) {
      for (auto &I : BB) {
        if (!I.getMetadata("nosanitize")) {
          s.addSelectiveInst(&I);
        }
      }
    }
  }
  auto end_time = chrono::system_clock::to_time_t(clock.now());

  s.printResult();
  errs() << "[RustSan]" << clock.now() << " TotalObjs["
         << total_obj.size() << "]\n";
    errs() << "[RustSan]" << clock.now() << " UnsafeObjRatio:"
         << format("%.2f", (double)unsafeObjs.size() / (double)total_obj.size() * 100) << "%\n";
  errs() << "[RustSan]" << clock.now() << " FalseSafeObjs["
         << falseSafeObjs.size() << "]\n";

  errs() << "[RustSan]" << clock.now() << ": Analyzer Finish.\n";
  errs() << "\tPTA time: \t\t" << pta_time - start_time << " seconds\n";
  errs() << "\tTraverse time:\t\t" << end_time - pta_time << " seconds\n";

  AndersenWaveDiff::releaseAndersenWaveDiff();
  SVFIR::releaseSVFIR();

  if (!ClNoOutput)
    LLVMModuleSet::getLLVMModuleSet()->dumpModulesToFile(".svf.bc");
  SVF::LLVMModuleSet::releaseLLVMModuleSet();

  llvm::llvm_shutdown();

  return 0;
}
