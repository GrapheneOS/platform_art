%def bincmp(condition=""):
    unimp

%def zcmp(condition=""):
    unimp

%def op_goto():
    unimp

%def op_goto_16():
    unimp

%def op_goto_32():
    unimp

%def op_if_eq():
%  bincmp(condition="eq")

%def op_if_eqz():
%  zcmp(condition="eq")

%def op_if_ge():
%  bincmp(condition="ge")

%def op_if_gez():
%  zcmp(condition="ge")

%def op_if_gt():
%  bincmp(condition="gt")

%def op_if_gtz():
%  zcmp(condition="gt")

%def op_if_le():
%  bincmp(condition="le")

%def op_if_lez():
%  zcmp(condition="le")

%def op_if_lt():
%  bincmp(condition="lt")

%def op_if_ltz():
%  zcmp(condition="lt")

%def op_if_ne():
%  bincmp(condition="ne")

%def op_if_nez():
%  zcmp(condition="ne")

%def op_packed_switch(func="NterpDoPackedSwitch"):
    unimp

%def op_sparse_switch():
%  op_packed_switch(func="NterpDoSparseSwitch")

/*
 * Return a 32-bit value.
 */
%def op_return(is_object="0", is_void="0", is_wide="0"):
    unimp

%def op_return_object():
%  op_return(is_object="1", is_void="0", is_wide="0")

%def op_return_void():
%  op_return(is_object="0", is_void="1", is_wide="0")

%def op_return_wide():
%  op_return(is_object="0", is_void="0", is_wide="1")

%def op_throw():
    unimp
