%def op_aget(load="unimp", shift="2", data_offset="MIRROR_INT_ARRAY_DATA_OFFSET", wide="0", is_object="0"):
    unimp

%def op_aget_boolean():
%  op_aget(load="unimp", shift="0", data_offset="MIRROR_BOOLEAN_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_byte():
%  op_aget(load="unimp", shift="0", data_offset="MIRROR_BYTE_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_char():
%  op_aget(load="unimp", shift="1", data_offset="MIRROR_CHAR_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_object():
%  op_aget(load="unimp", shift="2", data_offset="MIRROR_OBJECT_ARRAY_DATA_OFFSET", wide="0", is_object="1")

%def op_aget_short():
%  op_aget(load="unimp", shift="1", data_offset="MIRROR_SHORT_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aget_wide():
%  op_aget(load="unimp", shift="3", data_offset="MIRROR_WIDE_ARRAY_DATA_OFFSET", wide="1", is_object="0")

%def op_aput(store="unimp", shift="2", data_offset="MIRROR_INT_ARRAY_DATA_OFFSET", wide="0", is_object="0"):
    unimp

%def op_aput_boolean():
%  op_aput(store="unimp", shift="0", data_offset="MIRROR_BOOLEAN_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_byte():
%  op_aput(store="unimp", shift="0", data_offset="MIRROR_BYTE_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_char():
%  op_aput(store="unimp", shift="1", data_offset="MIRROR_CHAR_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_short():
%  op_aput(store="unimp", shift="1", data_offset="MIRROR_SHORT_ARRAY_DATA_OFFSET", wide="0", is_object="0")

%def op_aput_wide():
%  op_aput(store="unimp", shift="3", data_offset="MIRROR_WIDE_ARRAY_DATA_OFFSET", wide="1", is_object="0")

%def op_aput_object():
%  op_aput(store="unimp", shift="2", data_offset="MIRROR_OBJECT_ARRAY_DATA_OFFSET", wide="0", is_object="1")

%def op_array_length():
    unimp

%def op_fill_array_data():
    unimp

%def op_filled_new_array(helper="nterp_filled_new_array"):
    unimp

%def op_filled_new_array_range():
%  op_filled_new_array(helper="nterp_filled_new_array_range")

%def op_new_array():
    unimp

