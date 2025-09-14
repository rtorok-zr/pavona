// Copyright lowRISC contributors (OpenTitan project).
// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

class dv_base_sequencer #(type ITEM_T     = uvm_sequence_item,
                          type CFG_T      = dv_base_agent_cfg,
                          type RSP_ITEM_T = ITEM_T)
  extends uvm_sequencer #(.REQ(ITEM_T), .RSP(RSP_ITEM_T));

  `uvm_component_param_utils(dv_base_sequencer #(.ITEM_T     (ITEM_T),
                                                 .CFG_T      (CFG_T),
                                                 .RSP_ITEM_T (RSP_ITEM_T)))

  // These FIFOs collect items when req/rsp is received, which are used to communicate between
  // monitor and sequences. These FIFOs are optional
  // When device is re-active, it gets items from req_analysis_fifo and send rsp to driver
  // When this is a high-level agent, monitors put items to these 2 FIFOs for high-level seq
  uvm_tlm_analysis_fifo #(ITEM_T)     req_analysis_fifo;
  uvm_tlm_analysis_fifo #(RSP_ITEM_T) rsp_analysis_fifo;

  CFG_T cfg;

  bit do_not_reset;
  bit is_virtual_sequencer;

  function new (string name="", uvm_component parent=null);
    super.new(name, parent);
    do_not_reset = 0;
    is_virtual_sequencer = 0;
  endfunction : new

  function void build_phase(uvm_phase phase);
    dv_pair#(uvm_sequencer_base, bit) seqr_pair;

    super.build_phase(phase);

    // Avoid null pointer if the cfg is not defined.
    if (!is_virtual_sequencer && cfg == null) begin
      `uvm_fatal(`gfn, "cfg handle is null.")
    end else if (cfg != null) begin
      if (cfg.has_req_fifo) req_analysis_fifo = new("req_analysis_fifo", this);
      if (cfg.has_rsp_fifo) rsp_analysis_fifo = new("rsp_analysis_fifo", this);
    end

    // Push the sequencer into the static sequencer list
    seqr_pair = new();
    seqr_pair.fill_pair (this, do_not_reset);
    dv_sequencer_list::register_sequencer(seqr_pair);
  endfunction : build_phase

endclass
