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

#[allow(unused_imports)]
use crate::pta_filter::util::*;

#[allow(unused_imports)]
use crate::pta_filter::mirutil::*;

pub struct MyDbg;

impl<'tcx> MirPass<'tcx> for MyDbg {
    fn is_enabled(&self, sess: &rustc_session::Session) -> bool {
        sess.mir_opt_level() > 0
    }

    #[allow(unused_variables)]
    fn run_pass(&self, tcx: TyCtxt<'tcx>, body: &mut Body<'tcx>) {
        for block in body.basic_blocks.iter() {
            for statement in block.statements.iter() {
                if is_unsafe(&body, statement) {
                    // pta_target: work as unsafe_or_not
                    // true if unsafe, false if safe
                    unsafe { to_mut(statement).pta_target = true; }
                } else {
                    unsafe { to_mut(statement).pta_target = false; }
                }
            }

            let terminator = &block.terminator.as_ref().unwrap();
            if is_unsafe(&body, *terminator) {
                unsafe { to_mut(*terminator).pta_target = true; }
            } else {
                unsafe { to_mut(*terminator).pta_target = false; }
            }
        }
    }
}
