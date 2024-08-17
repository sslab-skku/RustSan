#[allow(unused_imports)]
use rustc_middle::mir::*;
use rustc_middle::ty::TyCtxt;
use rustc_index::bit_set::BitSet;
use rustc_span::def_id::DefId;
use rustc_middle::mir::StatementKind::*;
use rustc_middle::mir::TerminatorKind::*;
use rustc_middle::mir::Rvalue::*;

pub fn get_locals_from_statement<'a, 'tcx: 'a>(stmt: &'a Statement<'tcx>) -> Option<Vec<Local>> {
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

pub fn get_locals_from_rvalue(rvalue: &Rvalue) -> Option<Vec<Local>> {
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

pub fn get_locals_from_terminator<'a, 'tcx: 'a>(term: &'a Terminator<'tcx>) -> Option<Vec<Local>> {
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
