// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

class dv_callback #(type ITEM_T=uvm_sequence_item) extends uvm_callback;

   virtual function void connect_to_callback();

     // The default implementation is to return a fail
     // the extended class needs to provide a specific
     // implementation
     `uvm_fatal ("DV_BASE_CALLBACK", {"dv_callback cannont be used in the default mode.",
                                      "Please use the macro \`dv_callback_imp_decl"});
   endfunction

   virtual function void indicated(ITEM_T trans);
   endfunction
endclass

`define dv_callback_imp_decl(NAME_CB_OBJ, AGENT_NAME, ITEM_T, RECEIVER_T, RECEIVER_FUNC)       \
`ifndef RECEIVER_T``_DEF                                                                       \
`define RECEIVER_T``_DEF                                                                       \
typedef class RECEIVER_T;                                                                      \
`endif                                                                                         \
                                                                                               \
class NAME_CB_OBJ extends dv_callback #(ITEM_T);                                               \
  RECEIVER_T rcv_obj;                                                                          \
  function new ( RECEIVER_T _rcv_obj);                                                         \
    rcv_obj = _rcv_obj;                                                                        \
    `uvm_info(`"NAME_CB_OBJ`", {`"NAME_CB_OBJ`", "::new()"}, UVM_MEDIUM);                      \
  endfunction                                                                                  \
                                                                                               \
  virtual function void connect_to_callback();                                                 \
    string                monitor_name;                                                        \
    dv_monitor#(ITEM_T)   _monitor;                                                            \
    uvm_component         _monitor_list[$];                                                    \
                                                                                               \
    monitor_name = {AGENT_NAME,".monitor"};                                                    \
    `uvm_info(`"NAME_CB_OBJ`", {`"NAME_CB_OBJ`", "::connect()"}, UVM_MEDIUM);                  \
    `uvm_info(`"NAME_CB_OBJ`", {"Finding Monitor: ", monitor_name}, UVM_LOW);                  \
    uvm_top.find_all (monitor_name, _monitor_list);                                            \
    if (_monitor_list.size() > 1) begin                                                        \
      `uvm_fatal(`"NAME_CB_OBJ`", {monitor_name, " - Monitor Not Unique In The Environment.",  \
                                                   "Please Specify Unique Name"});             \
    end                                                                                        \
    else if (_monitor_list.size() == 0) begin                                                  \
      `uvm_fatal(`"NAME_CB_OBJ`", {monitor_name, " - Monitor Not Found In The Environment.",   \
                                                   "Please Specify Unique Name"});             \
    end                                                                                        \
    $cast (_monitor, _monitor_list[0]);                                                        \
    uvm_callbacks#(dv_monitor#(ITEM_T), dv_callback#(ITEM_T))::add(_monitor, this);            \
    `uvm_info(`"NAME_CB_OBJ`", {"Added Callback to:", monitor_name}, UVM_LOW);                 \
  endfunction                                                                                  \
                                                                                               \
  virtual function void indicated( ITEM_T trans);                                              \
    rcv_obj.``RECEIVER_FUNC(trans);                                                            \
  endfunction                                                                                  \
endclass
