%def op_check_cast():
    unimp

%def op_check_cast_slow_path():
    unimp

%def op_instance_of():
    unimp

%def op_instance_of_slow_path():
    unimp

%def op_new_instance():
    unimp

// *** iget ***

%def op_iget(load="", wide="", is_object=""):
    unimp

%def op_iget_slow_path(load, wide, is_object):
    unimp

%def op_iget_boolean():
    unimp

%def op_iget_byte():
    unimp

%def op_iget_char():
    unimp

%def op_iget_short():
    unimp

%def op_iget_wide():
    unimp

%def op_iget_object():
    unimp

// *** iput ***

%def op_iput(wide="", is_object=""):
    unimp

%def op_iput_slow_path(wide, is_object):
    unimp

%def op_iput_boolean():
    unimp

%def op_iput_byte():
    unimp

%def op_iput_char():
    unimp

%def op_iput_short():
    unimp

%def op_iput_wide():
    unimp

%def op_iput_object():
    unimp

// *** sget ***

// sget vAA, field@BBBB
// Format 21c: AA|60 BBBB
%def op_sget(load="", wide="", is_object=""):
    unimp

%def op_sget_slow_path(load, wide, is_object):
    unimp

// sget-wide vAA, field@BBBB
// Format 21c: AA|61 BBBB
%def op_sget_wide():
    unimp

// sget-object vAA, field@BBBB
// Format 21c: AA|62 BBBB
%def op_sget_object():
    unimp

// sget-boolean vAA, field@BBBB
// Format 21c: AA|63 BBBB
%def op_sget_boolean():
    unimp

// sget-byte vAA, field@BBBB
// Format 21c: AA|64 BBBB
%def op_sget_byte():
    unimp

// sget-char vAA, field@BBBB
// Format 21c: AA|65 BBBB
%def op_sget_char():
    unimp

// sget-short vAA, field@BBBB
// Format 21c: AA|66 BBBB
%def op_sget_short():
    unimp

// *** sput ***

.macro CLEAR_STATIC_VOLATILE_MARKER reg
    andi \reg, \reg, ~0x1
.endm

// sput vAA, field@BBBB
// Format 21c: AA|67 BBBB
// Clobbers: t0, t1, t2, a0
%def op_sput(width="32", is_object=False):
   srliw t2, xINST, 8  // t2 := AA
%  if width == "64":
     GET_VREG_WIDE s7, t2
%  elif is_object:
     GET_VREG_OBJECT s7, t2
%  else:
     GET_VREG s7, t2
%#:
   // Fast path: NterpGetStaticField's resolved_field from thread-local cache.
   // Stores cache value in a0 to match slow path's return from NterpGetStaticField.
   // Slow path: updates s7 if is_object, for possible GC movement.
   FETCH_FROM_THREAD_CACHE /*resolved_field*/a0, .L${opcode}_slow, t0, t1

.L${opcode}_regular_resume:
   lwu t0, ART_FIELD_OFFSET_OFFSET(a0)
   lwu a0, ART_FIELD_DECLARING_CLASS_OFFSET(a0)  // a0 := holder
   TEST_IF_MARKING t1, .L${opcode}_mark

.L${opcode}_mark_resume:
   add t0, t0, a0
%  if width == "8":
     sb s7, (t0)
%  elif width == "16":
     sh s7, (t0)
%  elif width == "32":
     sw s7, (t0)
%  else:  # width 64:
     sd s7, (t0)
%#:
%  write_barrier_if_object(is_object=is_object, value="s7", holder="a0", z0="t0", z1="t1", uniq=f"{opcode}")
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE t0
   GOTO_OPCODE t0

.L${opcode}_slow:
%  slow_path = add_slow_path(op_sput_slow_path, width, is_object, "s7", "t0", "t1")
   tail $slow_path  // slow path offset exceeds regular branch imm in FETCH_FROM_THREAD_CACHE
                    // return a0 := resolved_field
.L${opcode}_mark:
   call art_quick_read_barrier_mark_reg10  // a0
   j .L${opcode}_mark_resume


// Input
//   - width: bit width of value. One of 8, 16, 32, 64.
//   - is_object: True if reference type
//   - value: register holding value to store. Avoid clobber set.
// Output
//   - a0: holds resolved_field.
// Hardcoded: a0, a1, a2, xREFS, xINST, xPC
// Temporaries z0, z1
%def op_sput_slow_path(width, is_object, value, z0, z1):
   // Args for nterp_get_static_field
   mv a0, xSELF
   ld a1, (sp)
   mv a2, xPC
%  if is_object:
     mv a3, $value
%  else:
     mv a3, zero
%#:
   EXPORT_PC
   call nterp_get_static_field  // result a0 := resolved_field
%  if is_object:
     // Reload value, it may have moved.
     srliw $value, xINST, 8  // value:= AA
     GET_VREG_OBJECT $value, $value // value:= v[AA]
%#:
   // Test for volatile bit
   slli $z0, a0, 63
   bltz $z0, 1f
   tail .L${opcode}_regular_resume  // resume offset exceeds branch imm
1:
   // Volatile static store.
   CLEAR_STATIC_VOLATILE_MARKER a0
   lwu $z0, ART_FIELD_OFFSET_OFFSET(a0)
   lwu a0, ART_FIELD_DECLARING_CLASS_OFFSET(a0)  // a0 := holder
   TEST_IF_MARKING $z1, 3f
2:
   add $z0, a0, $z0
   // Ensure the volatile store is released.
%  if width == "8":
     fence rw, w
     sb $value, ($z0)
     fence rw, rw
%  elif width == "16":
     fence rw, w
     sh $value, ($z0)
     fence rw, rw
%  elif width == "32":
     // \value must NOT be the destination register, the destination gets clobbered!
     // For refs, \value's original value is used in the write barrier below.
     amoswap.w.rl zero, $value, ($z0)
%  else:  # width == 64:
     amoswap.d.rl zero, $value, ($z0)
%#:
%  write_barrier_if_object(is_object=is_object, value=value, holder="a0", z0=z0, z1=z1, uniq=f"slow_{opcode}")
   FETCH_ADVANCE_INST 2
   GET_INST_OPCODE $z0
   GOTO_OPCODE $z0
3:
   call art_quick_read_barrier_mark_reg10  // a0
   j 2b


%def write_barrier_if_object(is_object, value, holder, z0, z1, uniq):
%  if is_object:
     beqz $value, .L${uniq}_skip_write_barrier  // No object, skip out.
     ld $z0, THREAD_CARD_TABLE_OFFSET(xSELF)
     srli $z1, $holder, CARD_TABLE_CARD_SHIFT
     add $z1, $z0, $z1
     sb $z0, ($z1)
.L${uniq}_skip_write_barrier:


// sput-wide vAA, field@BBBB
// Format 21c: AA|68 BBBB
%def op_sput_wide():
%  op_sput(width="64")


// sput-object vAA, field@BBBB
// Format 21c: AA|69 BBBB
%def op_sput_object():
%  op_sput(width="32", is_object=True)


// sput-object vAA, field@BBBB
// Format 21c: AA|6a BBBB
%def op_sput_boolean():
%  op_sput(width="8")


// sput-object vAA, field@BBBB
// Format 21c: AA|6b BBBB
%def op_sput_byte():
%  op_sput(width="8")


// sput-object vAA, field@BBBB
// Format 21c: AA|6c BBBB
%def op_sput_char():
%  op_sput(width="16")


// sput-object vAA, field@BBBB
// Format 21c: AA|6d BBBB
%def op_sput_short():
%  op_sput(width="16")

