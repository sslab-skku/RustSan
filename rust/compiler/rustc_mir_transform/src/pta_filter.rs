use crate::MirPass;
#[allow(unused_imports)]
use rustc_middle::mir::*;
use rustc_middle::ty::TyCtxt;
use rustc_index::bit_set::BitSet;
use rustc_span::def_id::DefId;
use rustc_middle::mir::StatementKind::*;
use rustc_middle::mir::TerminatorKind::*;
use rustc_middle::mir::Rvalue::*;

use std::collections::HashSet;

pub mod util;
pub mod mirutil;

#[allow(unused_imports)]
use util::*;
// use crate::unsafe_alloc::util::*;

#[allow(unused_imports)]
use mirutil::*;
// use crate::unsafe_alloc::mirutil::*;

pub struct PTAFilter;

impl<'tcx> MirPass<'tcx> for PTAFilter {
    fn is_enabled(&self, sess: &rustc_session::Session) -> bool {
        sess.mir_opt_level() > 0
    }

    #[allow(unused_variables)]
    fn run_pass(&self, tcx: TyCtxt<'tcx>, body: &mut Body<'tcx>) {
        for block in body.basic_blocks.iter() {
            for statement in block.statements.iter() {
                if statement.pta_target {
                    let locals = get_locals_from_statement(&statement);
                    if locals.is_some() {
                        for local in locals.unwrap() {
                            body.local_decls[local].safety = false;
                        }
                    }
                }
            }

            let terminator = block.terminator.as_ref().unwrap();
            if terminator.pta_target {
                let locals = get_locals_from_terminator(&terminator);
                if locals.is_some() {
                    for local in locals.unwrap() {
                        body.local_decls[local].safety = false;
                    }
                }
            }
        }
        /*
        let mut cnt = 0;
        let mut total = 0;
        for block in body.basic_blocks.iter() {
            for statement in block.statements.iter() {
                if !is_unsafe(&body, statement) {
                    // don't need to filter safe statement
                    continue;
                }

                unsafe { to_mut(statement).pta_target = true; }
                total += 1;
                let locals = get_locals_from_statement(&statement);

                let mut is_safe_to_filter = true;
                if locals.is_some() {
                    for local in locals.unwrap() {
                        if local.as_usize() <= body.arg_count {
                            is_safe_to_filter = false;
                            break;
                        }
                    }
                } else {
                    is_safe_to_filter = false;
                }

                if is_safe_to_filter {
                    cnt += 1;
                    unsafe { to_mut(statement).pta_target = false; }
                }

                // println!("stmt: {:?}\t{}", statement, is_safe_to_filter);

                // TODO: is_safe_to_filter -> statement -> LLVM instruction
            }

            let terminator = &block.terminator;
            if terminator.is_none() {
                continue;
            }
            let terminator = terminator.as_ref().unwrap();
            if !is_unsafe(&body, terminator) {
                // don't need to filter safe terminator
                continue;
            }
            unsafe { to_mut(terminator).pta_target = true; }
            total += 1;

            let locals = get_locals_from_terminator(&terminator);

            let mut is_safe_to_filter = true;
            if locals.is_some() {
                for local in locals.unwrap() {
                    if local.as_usize() <= body.arg_count {
                        is_safe_to_filter = false;
                        break;
                    }
                }
            } else {
                is_safe_to_filter = false;
            }

            if is_safe_to_filter {
                cnt += 1;
                unsafe { to_mut(terminator).pta_target = false; }
            }

            // println!("term: {:?}\t{}", terminator, is_safe_to_filter);
        }
        
        // println!("body {:?}: {} / {}", body.did, cnt, total);
        */
    }
}

fn get_locals_from_statement<'a, 'tcx: 'a>(stmt: &'a Statement<'tcx>) -> Option<Vec<Local>> {
    let mut result: Vec<Local> = Vec::new();

    match &stmt.kind {
        Assign(box (place, rvalue)) => {
            if place.as_local().is_none() {
                return None;
            } else {
                result.push(place.as_local().unwrap());
            }

            let rvalue_local: Option<Vec<Local>> = get_locals_from_rvalue(&rvalue);
            if rvalue_local.is_none() {
                return None;
            } else {
                result.append(&mut rvalue_local.unwrap());
            }
        },
        FakeRead(..) => { /* not exists */ },
        SetDiscriminant { place, .. } => {
        },
        Deinit(..) => { /* skip */ },
        StorageLive(..) => { /* skip */ },
        StorageDead(..) => { /* skip */ },
        Retag(..) => { /* not exists */ },
        AscribeUserType(..) => { /* not exists */ },
        Coverage(..) => { /* not exists */ },
        Intrinsic(..) => { /* skip */ },
        ConstEvalCounter => { /* skip */ },
        Nop => { /* skip */ },
    }

    Some(result)
}

fn get_locals_from_rvalue(rvalue: &Rvalue) -> Option<Vec<Local>> {
    match rvalue {
        Use(operand) => {
            if operand.constant().is_some() {
                return Some(vec![]);
            } else {
                // place
                let place = operand.place().unwrap();
                if place.as_local().is_none() {
                    return None;
                } else {
                    return Some(vec![place.as_local().unwrap()]);
                }
            }
        },
        Repeat(operand, ..) => {
            return None;
        },
        Ref(..) => {
            return None;
        },
        AddressOf(..) => {
            return None;
        },
        _ => { return None; }   // kimjy: tbh idc bout other cases
    }

    Some(vec![])
}

fn get_locals_from_terminator<'a, 'tcx: 'a>(term: &'a Terminator<'tcx>) -> Option<Vec<Local>> {
    let mut result: Vec<Local> = Vec::new();

    match &term.kind {
        Call { func, args, destination, ..  } => {
            for arg in args {
                if arg.constant().is_some() {
                    continue;
                } else {
                    // place
                    let place = arg.place().unwrap();
                    if place.as_local().is_none() {
                        return None;
                    } else {
                        result.push(place.as_local().unwrap());
                    }
                }
            }

            if destination.as_local().is_none() {
                return None;
            } else {
                result.push(destination.as_local().unwrap());
            }
        },
        _ => {
            // kimjy: well, idk
        }
    }

//    None
    Some(result)
}
