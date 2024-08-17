#include "llvm/Pass.h"
#include "Graphs/SVFG.h"
#include "Graphs/VFG.h"
#include "MemoryModel/PointerAnalysis.h"
#include "SVF-FE/CallGraphBuilder.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/SVFIRBuilder.h"
#include "Util/BasicTypes.h"
#include "Util/Options.h"
#include "WPA/Andersen.h"
#include "WPA/Steensgaard.h"

#include "llvm-c/Core.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/Analysis/CFLAndersAliasAnalysis.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

#define DEBUG
#define USE_VFG
#define DEBUG_TYPE "svf-analysis"
using namespace llvm;
using namespace SVF;
const char kUnsafeMetadata[] = "Unsafe.full";
const char kNoSanitize[] = "nosanitize";
const char kSelective[] = "selective";

static cl::opt<bool> ClBaselineBuild("baseline",
                                     cl::desc("Baseline (Full-ASan"));

static cl::opt<bool>
    ClSteensAA("use-steens", cl::desc("Use Steensgaard Alias Analysis in SVF"));

static cl::opt<bool>
    ClAnalysisDebug("rustsan-debug",
                    cl::desc("Use Steensgaard Alias Analysis in SVF"));

static cl::opt<bool> ClCrossAnalysis("cross-analysis",
                                     cl::desc("Cross IR analysis"));


class AnalysisPass : public ModulePass {
public:
  static char ID;
  AnalysisPass() : ModulePass(ID){};
  bool runOnModule(Module &) override;
  void putUnsafeObjsSet(PointerAnalysis *pta, Value *val,
                        std::set<NodeID> &unsafeObjs);

  void getAnalysisUsage(AnalysisUsage &AU) {
    AU.addRequired<CFLAndersAAWrapperPass>();
  }
};

bool pointerQuery(PointerAnalysis *ander, Instruction *I,
                  std::set<const Value *> &unsafeObjs);
// Legacy
bool workFunction(PointerAnalysis *ander, Instruction *I,
                  std::set<const Value *> &unsafeObjs);
void traverseOnVFGthread(const SVFG *vfg, const Value *val,
                         std::promise<std::vector<Value *>> p);
std::vector<Value *> traverseOnVFG(const SVFG *vfg, const Value *val);
bool isInterestingInstruction(Instruction *I);

std::vector<const Value *> easy_PA(PointerAnalysis *pa, Value *V) {
  std::vector<const Value *> ret;
  NodeID pNodeId;
  pNodeId = pa->getPAG()->getValueNode(V);
  errs() << V->getName() << "'s NodeId: " << pNodeId << "\n";
  const PointsTo &pts = pa->getPts(pNodeId);
  if (pts.empty()) {
    errs() << "Points-to set is empty!\n";
    return ret;
  }
  errs() << "Points-to set: [\n";
  for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie; ii++) {
    errs() << *ii << "\n";
    PAGNode *targetObj = pa->getPAG()->getGNode(*ii);
    if (targetObj->hasValue()) {
      ret.push_back(targetObj->getValue());
    }
  }
  errs() << "]\n";
  return ret;
}

bool AnalysisPass::runOnModule(Module &M) {
  for (auto &F : M)
    F.addFnAttr(Attribute::SanitizeAddress);
  std::set<NodeID> unsafeObjsID;
  std::chrono::system_clock clock;
  errs() << "[RustSan]" << clock.now() << ": Pass Start.\n";

  if (!ClBaselineBuild) {
    auto MD_unsafe = M.getMDKindID(kUnsafeMetadata);
    auto MD_nosan = M.getMDKindID(kNoSanitize);
    auto MD_sel = M.getMDKindID(kSelective);

    auto svfModule = LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(M);
    svfModule->buildSymbolTableInfo();
    SVFIRBuilder builder;
    SVFIR *pag = builder.build(svfModule);

    AndersenBase *ander = nullptr;
    if (ClSteensAA)
      ander = Steensgaard::createSteensgaard(pag);
    else
      ander = AndersenWaveDiff::createAndersenWaveDiff(pag);
    errs() << "[RustSan]" << clock.now() << ": "
           << (ClSteensAA ? "Steens" : "Anderson") << " Done.\n";

    /// Phase 1
    for (auto &F : *svfModule) {
      for (auto &BB : *F->getLLVMFun()) {
        for (auto &I : BB) {
          if (I.getMetadata(kUnsafeMetadata) != nullptr) {
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
                putUnsafeObjsSet(ander, Op, unsafeObjsID);
              }
            }
            if (!I.getType()->isVoidTy()) {
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
      II->setMetadata(MD_unsafe, MDNode::get(M.getContext(), None));
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
        }
        continue;
      }
    }
    bool functionflag = false;
    for (auto &BB : *llvmF) {
      for (auto &I : BB) {
        bool rustallocflag = false;
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
          const PointsTo &pts = ander->getPts(pNodeId);
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
            I.setMetadata(MD_sel, MDNode::get(M.getContext(), None));
            I.setMetadata(MD_nosan, NULL);
            break;
          } else {
            if (!I.getMetadata("Unsafe.full") && !I.getMetadata("selective")) {
              I.setMetadata(MD_nosan, MDNode::get(M.getContext(), None));
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

    // Finalize & statistics
    uint64_t InstCounter = 0;
    uint64_t TotalInstCounter = 0;
    for (auto &F : M) {
      if (F.hasFnAttribute("unsafe_closure")) {
        // errs() << "We meet unsafe closure: " << F.getName() << "\n";
        continue;
      }
      for (auto &BB : F) {
        for (auto &I : BB) {
          if (isInterestingInstruction(&I)) {
            if (pointerQuery(ander, &I, unsafeObjs))
              I.setMetadata(MD_sel, MDNode::get(M.getContext(), None));
            TotalInstCounter += 1;
            if (I.hasMetadata(MD_sel) || I.hasMetadata(MD_unsafe))
              InstCounter += 1;
            else
              I.setMetadata(MD_nosan, MDNode::get(M.getContext(), None));
          }
        }
      }
    }

    errs() << "[RustSan]" << clock.now()
           << ": Pass Done, Instrumented Instructions: " << InstCounter
           << ", Total Inst: " << TotalInstCounter << "("
           << (double)InstCounter / (double)TotalInstCounter << "%)\n";
  cleanup:
    // delete svfg;
    if (!ClSteensAA)
      AndersenWaveDiff::releaseAndersenWaveDiff();
    else
      Steensgaard::releaseSteensgaard();
    SVFIR::releaseSVFIR();
    SVF::LLVMModuleSet::releaseLLVMModuleSet();
  }

  // Run ASan Pass on module M
  legacy::PassManager PM;
  PM.add(createAddressSanitizerFunctionPass(
      false, false, false, AsanDetectStackUseAfterReturnMode::Never));
  PM.add(createModuleAddressSanitizerLegacyPassPass());
  PM.run(M);
  return true;
}

bool isInterestingInstruction(Instruction *I) {
  if (auto MI = dyn_cast<MemIntrinsic>(I)) {
    if (!isa<MemTransferInst>(MI))
      return false;
  }
  if (!isa<LoadInst>(I) && !isa<StoreInst>(I) && !isa<AtomicRMWInst>(I) &&
      !isa<AtomicCmpXchgInst>(I) && !isa<CallBase>(I))
    return false;

  return true;
}

void AnalysisPass::putUnsafeObjsSet(PointerAnalysis *pta, Value *val,
                                    std::set<NodeID> &unsafeObjs) {
  auto pag = pta->getPAG();
  NodeID pNodeId = pta->getPAG()->getValueNode(val);
  const PointsTo &pts = pta->getPts(pNodeId);
  for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie; ii++)
    unsafeObjs.insert(*ii);
}

std::vector<Value *> traverseOnVFG(const SVFG *vfg, const Value *val) {
  std::vector<Value *> ret;
  SVFIR *pag = SVFIR::getPAG();

  PAGNode *pNode = pag->getGNode(pag->getValueNode(val));
  const VFGNode *vNode = vfg->getDefSVFGNode(pNode);
  FIFOWorkList<const VFGNode *> worklist;
  Set<const VFGNode *> visited;
  worklist.push(vNode);

  /// Traverse along VFG
  while (!worklist.empty()) {
    const VFGNode *vNode = worklist.pop();
    for (VFGNode::const_iterator it = vNode->OutEdgeBegin(),
                                 eit = vNode->OutEdgeEnd();
         it != eit; ++it) {
      VFGEdge *edge = *it;
      VFGNode *succNode = edge->getDstNode();
      if (visited.find(succNode) == visited.end()) {
        visited.insert(succNode);
        worklist.push(succNode);
      }
    }
  }

  /// Collect all LLVM Values
  for (Set<const VFGNode *>::const_iterator it = visited.begin(),
                                            eit = visited.end();
       it != eit; ++it) {
    if (const Value *V = (*it)->getValue())
      ret.push_back(const_cast<Value *>(V));
  }
  return ret;
}

void traverseOnVFGthread(const SVFG *vfg, const Value *val,
                         std::promise<std::vector<Value *>> p) {
  p.set_value_at_thread_exit(traverseOnVFG(vfg, val));
}

/// true: this Instruction I access to unsafe objets
bool pointerQuery(PointerAnalysis *ander, Instruction *I,
                  std::set<const Value *> &unsafeObjs) {
  if (!isInterestingInstruction(I))
    return false;

  Module *M = I->getModule();
  auto MD_unsafe = M->getMDKindID(kUnsafeMetadata);
  auto MD_nosan = M->getMDKindID("nosanitize");
  auto MD_sel = M->getMDKindID("selective");
  bool selective_flag = false;

  Value *PTR = nullptr;
  if (auto II = dyn_cast<LoadInst>(I))
    PTR = II->getPointerOperand();
  else if (auto II = dyn_cast<StoreInst>(I))
    PTR = II->getPointerOperand();
  else if (auto II = dyn_cast<AtomicRMWInst>(I))
    PTR = II->getPointerOperand();
  else if (auto II = dyn_cast<AtomicCmpXchgInst>(I))
    PTR = II->getPointerOperand();

  if (PTR) {
    // if (auto PTRI = dyn_cast<Instruction>(PTR)){
    //   if (PTRI->hasMetadata(MD_unsafe) || PTRI->hasMetadata(MD_sel))
    //     return true;
    // }
    NodeID pNodeId;
    if (ander->getPAG()->hasValueNode(PTR))
      pNodeId = ander->getPAG()->getValueNode(PTR);
    else
      // TODO: Can this branch excute?
      return false;
    const PointsTo &pts = ander->getPts(pNodeId);
    for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie; ii++) {
      const Value *obj = nullptr;
      PAGNode *targetObj = ander->getPAG()->getGNode(*ii);
      if (targetObj->hasValue())
        obj = targetObj->getValue();
      if (obj != nullptr && unsafeObjs.find(obj) != unsafeObjs.end()) {
        return true;
      }
    }
  } else {
    for (int i = 0; i < I->getNumOperands() + 1; ++i) {
      llvm::Value *arg;
      if (i == I->getNumOperands()) {
        // HACK: check return value of instruction in this loop
        if (I->getType()->isVoidTy()) {
          continue;
        } else {
          arg = dyn_cast<Value>(I);
        }
      } else {
        arg = I->getOperand(i);
      }

      NodeID pNodeId;
      if (ander->getPAG()->hasValueNode(arg))
        pNodeId = ander->getPAG()->getValueNode(arg);
      else
        continue;
      const PointsTo &pts = ander->getPts(pNodeId);
      for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie;
           ii++) {
        const Value *obj = nullptr;
        PAGNode *targetObj = ander->getPAG()->getGNode(*ii);
        if (targetObj->hasValue())
          obj = targetObj->getValue();
        if (obj != nullptr && unsafeObjs.find(obj) != unsafeObjs.end()) {
          selective_flag = true;
        }
      }
      if (selective_flag)
        return true;
    }
  }
  return false;
}

bool workFunction(PointerAnalysis *ander, Instruction *I,
                  std::set<const Value *> &unsafeObjs) {
  if (!isInterestingInstruction(I))
    return false;

  Module *M = I->getModule();
  auto MD_unsafe = M->getMDKindID(kUnsafeMetadata);
  auto MD_nosan = M->getMDKindID("nosanitize");
  auto MD_sel = M->getMDKindID("selective");
  if (CallBase *CallBaseI = dyn_cast<CallBase>(I)) {
    Function *Func = CallBaseI->getCalledFunction();
  }
  bool selective_flag = false;
  for (int i = 0; i < I->getNumOperands() + 1; ++i) {
    llvm::Value *arg;
    if (i == I->getNumOperands()) {
      // HACK: check return value of instruction in this loop
      if (I->getType()->isVoidTy()) {
        continue;
      } else {
        arg = dyn_cast<Value>(I);
      }
    } else {
      arg = I->getOperand(i);
    }

    NodeID pNodeId;
    if (ander->getPAG()->hasValueNode(arg))
      pNodeId = ander->getPAG()->getValueNode(arg);
    else
      continue;
    const PointsTo &pts = ander->getPts(pNodeId);
    for (PointsTo::iterator ii = pts.begin(), ie = pts.end(); ii != ie; ii++) {
      const Value *obj = nullptr;
      PAGNode *targetObj = ander->getPAG()->getGNode(*ii);
      if (targetObj->hasValue())
        obj = targetObj->getValue();
      if (obj != nullptr && unsafeObjs.count(obj) != 0) {
        selective_flag = true;
      }
    }
    if (selective_flag) {
      return true;
    }
  }
  return false;
}

char AnalysisPass::ID = 0;
static RegisterPass<AnalysisPass> X("svf-analysis", "SVF analysis pass",
                                    false /*  CFGOnly */,
                                    false /* isAnalysis */);

#define REGISTER_PASS_AS_LTO_PLUGIN(PassName)                                  \
  REGISTER_PASS_AS_PLUGIN(PassName, PassManagerBuilder::EP_OptimizerLast)

#define REGISTER_PASS_AS_PLUGIN(PassName, Stage)                               \
  static RegisterStandardPasses Y(                                             \
      Stage, [](const PassManagerBuilder &Builder,                             \
                legacy::PassManagerBase &PM) { PM.add(new PassName()); })

REGISTER_PASS_AS_LTO_PLUGIN(AnalysisPass);
// REGISTER_PASS_AS_PLUGIN(AnalysisPass,
// PassManagerBuilder::EP_EnabledOnOptLevel0);
