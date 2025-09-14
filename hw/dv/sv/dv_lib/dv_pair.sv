// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0


class dv_pair #(type T1=uvm_sequence_item, type T2=uvm_sequence_item);
  T1 first;
  T2 second;

  function void fill_pair(T1 first_item, T2 second_item);
    first  = first_item;
    second = second_item;
  endfunction
endclass : dv_pair

