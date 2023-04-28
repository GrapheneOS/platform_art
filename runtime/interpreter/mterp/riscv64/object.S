%def op_check_cast():
    unimp

%def op_check_cast_slow_path():
    unimp

%def op_instance_of():
    unimp

%def op_instance_of_slow_path():
    unimp

%def op_iget_boolean():
%  op_iget(load="ldrb", wide="0", is_object="0")

%def op_iget_byte():
%  op_iget(load="ldrsb", wide="0", is_object="0")

%def op_iget_char():
%  op_iget(load="ldrh", wide="0", is_object="0")

%def op_iget_short():
%  op_iget(load="ldrsh", wide="0", is_object="0")

%def op_iget(load="ldr", wide="0", is_object="0"):
    unimp

%def op_iget_slow_path(load, wide, is_object):

%def op_iget_wide():
%  op_iget(load="ldr", wide="1", is_object="0")

%def op_iget_object():
%  op_iget(load="ldr", wide="0", is_object="1")

%def op_iput_boolean():
%  op_iput(store="strb", wide="0", is_object="0")

%def op_iput_byte():
%  op_iput(store="strb", wide="0", is_object="0")

%def op_iput_char():
%  op_iput(store="strh", wide="0", is_object="0")

%def op_iput_short():
%  op_iput(store="strh", wide="0", is_object="0")

%def op_iput(store="str", wide="0", is_object="0"):
    unimp

%def op_iput_slow_path(store, wide, is_object):
    unimp

%def op_iput_wide():
%  op_iput(store="str", wide="1", is_object="0")

%def op_iput_object():
%  op_iput(store="str", wide="0", is_object="1")

%def op_sget_boolean():
%  op_sget(load="ldrb", wide="0", is_object="0")

%def op_sget_byte():
%  op_sget(load="ldrsb", wide="0", is_object="0")

%def op_sget_char():
%  op_sget(load="ldrh", wide="0", is_object="0")

%def op_sget_short():
%  op_sget(load="ldrsh", wide="0", is_object="0")

%def op_sget(load="ldr", wide="0", is_object="0"):
    unimp

%def op_sget_slow_path(load="ldr", wide="0", is_object="0"):
    unimp

%def op_sget_wide():
%  op_sget(load="ldr", wide="1", is_object="0")

%def op_sget_object():
%  op_sget(load="ldr", wide="0", is_object="1")

%def op_sput_boolean():
%  op_sput(store="strb", wide="0", is_object="0")

%def op_sput_byte():
%  op_sput(store="strb", wide="0", is_object="0")

%def op_sput_char():
%  op_sput(store="strh", wide="0", is_object="0")

%def op_sput_short():
%  op_sput(store="strh", wide="0", is_object="0")

%def op_sput(store="str", wide="0", is_object="0"):
    unimp

%def op_sput_slow_path(store, wide, is_object):
    unimp

%def op_sput_wide():
%  op_sput(store="str", wide="1", is_object="0")

%def op_sput_object():
%  op_sput(store="str", wide="0", is_object="1")

%def op_new_instance():
    unimp
