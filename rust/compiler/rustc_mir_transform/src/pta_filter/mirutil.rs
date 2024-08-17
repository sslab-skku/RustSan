#[allow(unused_imports)]
use rustc_middle::mir::*;
use rustc_span::def_id::DefId;
use rustc_middle::ty::TyCtxt;

use std::fmt::Debug;

pub trait Instruction: Debug {
    fn source_info(&self) -> SourceInfo;
}

impl<'a> Instruction for Statement<'a> {
    fn source_info(&self) -> SourceInfo {
        self.source_info
    }
}

impl<'a> Instruction for Terminator<'a> {
    fn source_info(&self) -> SourceInfo {
        self.source_info
    }
}

#[allow(dead_code)]
pub fn get_localdecl_by_local<'a, 'tcx: 'a>(body: &'a Body<'tcx>, local: Local) -> &'a LocalDecl<'tcx> {
    &body.local_decls[local]
}

#[allow(dead_code)]
pub fn get_instruction_by_location<'a, 'tcx: 'a>(body: &'a Body<'tcx>, loc: Location) -> &'a dyn Instruction {
    let bb = &body.basic_blocks[loc.block];
    if bb.statements.len() == loc.statement_index {
        bb.terminator()
    } else {
        &bb.statements[loc.statement_index]
    }
}

#[allow(dead_code)]
pub fn is_local_non_primitive<'a, 'tcx: 'a>(body: &'a Body<'tcx>, local: Local) -> bool {
    !body.local_decls[local].ty.is_primitive_ty()
}

/*
pub fn get_body_by_defid<'tcx>(tcx: TyCtxt<'tcx>, did: DefId) -> &'tcx Body<'tcx> {
}
*/

#[allow(dead_code)]
pub fn decode_callsite<'a, 'tcx: 'a>(ptr: usize) -> &'a Terminator<'tcx> {
    unsafe { &*(ptr as *const Terminator<'tcx>) }
}

#[allow(dead_code)]
pub fn encode_callsite<'a, 'tcx: 'a>(terminator: &'a Terminator<'tcx>) -> usize {
    terminator as *const Terminator<'tcx> as usize
}

#[allow(dead_code)]
pub fn is_unsafe<'tcx, T: Instruction + ?Sized>(body: &Body<'tcx>, instruction: &T) -> bool {
    let mut scope = Some(instruction.source_info().scope);
    let safety = loop {
        if scope.is_none() {
            break false;
        } else {
            if let rustc_middle::mir::ClearCrossCrate::Set(source_scope_local_data) = &body.source_scopes[scope.unwrap()].local_data {
                if source_scope_local_data.safety != rustc_middle::mir::Safety::Safe {
                    break true;
                }
            }
            scope = body.source_scopes[scope.unwrap()].parent_scope;
        }
    };
    safety
}

#[allow(dead_code)]
pub fn resolve_func<'a, 'tcx: 'a>(tcx: TyCtxt<'tcx>, func: &'a Operand<'tcx>) -> Option<DefId> {
    let const_fn = func.const_fn_def();
    if const_fn.is_none() {
        return None;
    }

    let const_fn = const_fn.unwrap();
    let did = const_fn.0;
    let substs = const_fn.1;

    let step1 = rustc_middle::ty::Instance::resolve(tcx, rustc_middle::ty::ParamEnv::reveal_all(), did, substs);
    if step1 == Ok(None) || step1.is_err() {
        return Some(did);
    }

//    println!("step1: {:?}", step1);
    let step2 = step1.unwrap();
//    println!("step2: {:?}", step2);
    let step3 = step2.unwrap();
//    println!("step3: {:?}", step3);
    let res = step3.polymorphize(tcx).def_id();
    Some(rustc_middle::ty::Instance::resolve(tcx, rustc_middle::ty::ParamEnv::reveal_all(), did, substs).unwrap().unwrap().polymorphize(tcx).def_id())
}
