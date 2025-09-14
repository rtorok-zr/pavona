// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0



class dv_sequencer_list;
  static local dv_pair#(uvm_sequencer_base, bit) sequencer_list[$];

  extern function new(string name, uvm_component parent);
  extern static function void register_sequencer(dv_pair#(uvm_sequencer_base, bit) seqr_pair);
  extern static function void reset_sequencers();
endclass


function dv_sequencer_list::new(string name, uvm_component parent);
  `uvm_fatal("dv_sequencer_list::new()", "Sequencer List object cannot be created");
endfunction : new

function void dv_sequencer_list::register_sequencer(dv_pair#(uvm_sequencer_base, bit) seqr_pair);
  sequencer_list.push_back(seqr_pair);
endfunction


function void dv_sequencer_list::reset_sequencers();
  uvm_sequencer_base seqr;
  bit                do_not_reset;

  foreach (sequencer_list[i]) begin
    seqr         = sequencer_list[i].first;
    do_not_reset = sequencer_list[i].second;
    if (!do_not_reset) seqr.stop_sequences();
  end
endfunction
