// Theory of operation. These invoke-X opcodes bounce to code labels in main.S which attempt a
// variety of fast paths; the full asm doesn't fit in the per-opcode handler's size limit.
//
// Calling convention. There are three argument transfer types.
// (A) Managed ABI -> Nterp. The ExecuteNterpImpl handles this case. We set up a fresh nterp frame
//     and move arguments from machine arg registers (and sometimes stack) into the frame.
// (B) Nterp -> Nterp. An invoke op's fast path handles this case. If we can stay in nterp, then
//     we set up a fresh nterp frame, and copy the register slots from caller to callee.
// (C) Nterp -> Managed ABI. Invoke op's remaining cases. To leave nterp, we read out arguments from
//     the caller's nterp frame and place them into machine arg registers (and sometimes stack).
//     Doing so requires obtaining and deciphering the method's shorty for arg type, width, and
//     order info.
//
// Fast path structure.
// (0) If the next method's "quick code" is nterp, then set up a fresh nterp frame and perform a
//     vreg->vreg transfer. Jump to handler for the next method's first opcode.
// - The following paths leave nterp. -
// (1) If the next method is guaranteed to avoid floats, doubles, and longs, then the managed ABI is
//     very simple: just place all arguments in the native arg registers. We don't need to know the
//     precise types or widths, just the order matters. Call the quick code.
// (2) If the next method has 0 or 1 argument, then the managed ABI is mildly overloaded by
//     pessimistically placing a singleton 32-bit arg in both a0 and fa0; we don't have to know if
//     the argument is an int or float. We might be able to avoid the shorty ...
// (2.1) If this 0/1 arg method isn't returning a scalar value, it's returning void or an object. A
//       return will come back in a0, so we definitely don't need the shorty. Call the quick code.
// (2.2) If this 0/1 arg method returns a scalar value, that return might be a float or double! We
//       must obtain the shorty to know for sure, so that a returned float is copied from fa0 into
//       a0, as expected by Nterp. But we can skip the argument setup for the managed ABI. Call the
//       quick code.
// - The fully pessimistic case. -
// (3) The next method has 2+ arguments with a mix of float/double/long, OR it is polymorphic OR
//     custom. Obtain the shorty and perform the full setup for managed ABI. Polymorphic and custom
//     invokes are specially shunted to the runtime. Otherwise we call the quick code.
//
// Code organization. These functions are organized in a three tier structure to aid readability.
// (P) The "front end" is an opcode handler, such as op_invoke_virtual(). They are defined in
//     invoke.S. Since all the invoke code cannot fit in the allotted handler region, every invoke
//     handler has code extending into a "back end".
// (Q) The opcode handler calls a "back end" label that is located in main.S. The code for that
//     label is defined in invoke.S. As a convention, the label in main.S is NterpInvokeVirtual. The
//     code in invoke.S is nterp_invoke_virtual().
// (R) For the Nterp to Nterp fast path case, the back end calls a label located in main.S, the code
//     for which is defined in invoke.S. As a convention, the label in main.S is
//     NterpToNterpInstance, and the code in invoke.S is nterp_to_nterp_instance().
// Helpers for each tier are placed just after the functions of each tier.

//
// invoke-kind {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|op BBBB F|E|D|C
//

// invoke-virtual {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|6e BBBB F|E|D|C
//
// Note: invoke-virtual is used to invoke a normal virtual method (a method that is not private,
// static, or final, and is also not a constructor).
%def op_invoke_virtual(range=""):
   unimp


// invoke-super {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|6f BBBB F|E|D|C
//
// Note: When the method_id references a method of a non-interface class, invoke-super is used to
// invoke the closest superclass's virtual method (as opposed to the one with the same method_id in
// the calling class).
// Note: In Dex files version 037 or later, if the method_id refers to an interface method,
// invoke-super is used to invoke the most specific, non-overridden version of that method defined
// on that interface. The same method restrictions hold as for invoke-virtual. In Dex files prior to
// version 037, having an interface method_id is illegal and undefined.
%def op_invoke_super(range=""):
   unimp


// invoke-direct {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|70 BBBB F|E|D|C
//
// Note: invoke-direct is used to invoke a non-static direct method (that is, an instance method
// that is by its nature non-overridable, namely either a private instance method or a constructor).
//
// For additional context on string init, see b/28555675. The object reference is replaced after
// the string factory call, so we disable thread-caching the resolution of string init, and skip
// fast paths out to managed ABI calls.
%def op_invoke_direct(range=""):
   unimp


// invoke-static {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|71 BBBB F|E|D|C
//
// Note: invoke-static is used to invoke a static method (which is always considered a direct
// method).
%def op_invoke_static(range=""):
   EXPORT_PC
   FETCH_FROM_THREAD_CACHE a0, /*slow path*/1f, t0, t1
                                   // a0 := ArtMethod*
   tail NterpInvokeStatic${range}  // arg a0
1:
%  resolve_method_into_a0()
   tail NterpInvokeStatic${range}  // arg a0


// invoke-interface {vC, vD, vE, vF, vG}, meth@BBBB
// Format 35c: A|G|72 BBBB F|E|D|C
//
// Note: invoke-interface is used to invoke an interface method, that is, on an object whose
// concrete class isn't known, using a method_id that refers to an interface.
%def op_invoke_interface(range=""):
   unimp


//
// invoke-kind/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|op BBBB CCCC
// where NNNN = CCCC + AA - 1, that is A determines the count 0..255, and C determines the first
// register.
//

// invoke-virtual/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|74 BBBB CCCC
//
// Note: invoke-virtual/range is used to invoke a normal virtual method (a method that is not
// private, static, or final, and is also not a constructor).
%def op_invoke_virtual_range():
%   op_invoke_virtual(range="Range")


// invoke-super/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|75 BBBB CCCC
//
// Note: When the method_id references a method of a non-interface class, invoke-super/range is used
// to invoke the closest superclass's virtual method (as opposed to the one with the same method_id
// in the calling class).
// Note: In Dex files version 037 or later, if the method_id refers to an interface method,
// invoke-super/range is used to invoke the most specific, non-overridden version of that method
// defined on that interface. In Dex files prior to version 037, having an interface method_id is
// illegal and undefined.
%def op_invoke_super_range():
%   op_invoke_super(range="Range")


// invoke-direct/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|76 BBBB CCCC
//
// Note: invoke-direct/range is used to invoke a non-static direct method (that is, an instance
// method that is by its nature non-overridable, namely either a private instance method or a
// constructor).
%def op_invoke_direct_range():
%   op_invoke_direct(range="Range")


// invoke-static/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|77 BBBB CCCC
//
// Note: invoke-static/range is used to invoke a static method (which is always considered a direct
// method).
%def op_invoke_static_range():
%   op_invoke_static(range="Range")


// invoke-interface/range {vCCCC .. vNNNN}, meth@BBBB
// Format 3rc: AA|78 BBBB CCCC
//
// Note: invoke-interface/range is used to invoke an interface method, that is, on an object whose
// concrete class isn't known, using a method_id that refers to an interface.
%def op_invoke_interface_range():
%   op_invoke_interface(range="Range")


// invoke-polymorphic {vC, vD, vE, vF, vG}, meth@BBBB, proto@HHHH
// Format 45cc: A|G|fa BBBB F|E|D|C HHHH
//
// Note: Invoke the indicated signature polymorphic method. The result (if any) may be stored with
// an appropriate move-result* variant as the immediately subsequent instruction.
//
// The method reference must be to a signature polymorphic method, such as
// java.lang.invoke.MethodHandle.invoke or java.lang.invoke.MethodHandle.invokeExact.
//
// The receiver must be an object supporting the signature polymorphic method being invoked.
//
// The prototype reference describes the argument types provided and the expected return type.
//
// The invoke-polymorphic bytecode may raise exceptions when it executes. The exceptions are
// described in the API documentation for the signature polymorphic method being invoked.
//
// Present in Dex files from version 038 onwards.
%def op_invoke_polymorphic(range=""):
   unimp


// invoke-polymorphic/range {vCCCC .. vNNNN}, meth@BBBB, proto@HHHH
// Format 4rcc: AA|fb BBBB CCCC HHHH
// where NNNN = CCCC + AA - 1, that is A determines the count 0..255, and C determines the first
// register.
//
// Note: Invoke the indicated method handle. See the invoke-polymorphic description above for
// details.
//
// Present in Dex files from version 038 onwards.
%def op_invoke_polymorphic_range():
%   op_invoke_polymorphic(range="Range")


// invoke-custom {vC, vD, vE, vF, vG}, call_site@BBBB
// Format 35c: A|G|fc BBBB F|E|D|C
//
// Note: Resolves and invokes the indicated call site. The result from the invocation (if any) may
// be stored with an appropriate move-result* variant as the immediately subsequent instruction.
//
// This instruction executes in two phases: call site resolution and call site invocation.
//
// Call site resolution checks whether the indicated call site has an associated
// java.lang.invoke.CallSite instance. If not, the bootstrap linker method for the indicated call
// site is invoked using arguments present in the DEX file (see call_site_item). The bootstrap
// linker method returns a java.lang.invoke.CallSite instance that will then be associated with the
// indicated call site if no association exists. Another thread may have already made the
// association first, and if so execution of the instruction continues with the first associated
// java.lang.invoke.CallSite instance.
//
// Call site invocation is made on the java.lang.invoke.MethodHandle target of the resolved
// java.lang.invoke.CallSite instance. The target is invoked as if executing invoke-polymorphic
// (described above) using the method handle and arguments to the invoke-custom instruction as the
// arguments to an exact method handle invocation.
//
// Exceptions raised by the bootstrap linker method are wrapped in a java.lang.BootstrapMethodError.
// A BootstrapMethodError is also raised if:
// - the bootstrap linker method fails to return a java.lang.invoke.CallSite instance.
// - the returned java.lang.invoke.CallSite has a null method handle target.
// - the method handle target is not of the requested type.
//
// Present in Dex files from version 038 onwards.
%def op_invoke_custom(range=""):
   unimp


// invoke-custom/range {vCCCC .. vNNNN}, call_site@BBBB
// Format 3rc: AA|fd BBBB CCCC
// where NNNN = CCCC + AA - 1, that is A determines the count 0..255, and C determines the first
// register.
//
// Note: Resolve and invoke a call site. See the invoke-custom description above for details.
//
// Present in Dex files from version 038 onwards.
%def op_invoke_custom_range():
%  op_invoke_custom(range="Range")


// handler helpers

%def resolve_method_into_a0():
   mv a0, xSELF
   ld a1, (sp)  // We can't always rely on a0 = ArtMethod*.
   mv a2, xPC
   call nterp_get_method


//
// These asm blocks are positioned in main.S for visibility to stack walking.
//

// NterpInvokeVirtual
// a0: ArtMethod*
// a1: this
%def nterp_invoke_virtual():
   unimp


// NterpInvokeSuper
// a0: ArtMethod*
// a1: this
%def nterp_invoke_super():
   unimp


// NterpInvokeDirect
// a0: ArtMethod*
// a1: this
%def nterp_invoke_direct(uniq="invoke_direct"):
   unimp


// NterpInvokeStringInit
// a0: ArtMethod*
// a1: this
%def nterp_invoke_string_init(uniq="invoke_string_init"):
   unimp


// NterpInvokeStatic
// a0: ArtMethod*
%def nterp_invoke_static(uniq="invoke_static"):
   ld s7, ART_METHOD_QUICK_CODE_OFFSET_64(a0)
                                 // s7 := quick code
%  try_nterp(quick="s7", z0="t0", skip=f".L{uniq}_simple")
   call NterpToNterpStatic       // arg a0
   j .L${uniq}_next_op

.L${uniq}_simple:
   srliw t0, xINST, 12           // t0 := A
%  try_simple_args_static(ins="t0", z0="t1", z1="t2", skip=f".L{uniq}_01", uniq=uniq)
                                 // a1, a2, a3, a4, a5 := C, D, E, F, G
   jalr s7                       // args a0 - a5
   j .L${uniq}_next_op

.L${uniq}_01:
   mv s8, zero                   // initialize shorty reg
%  try_01_args_static(ins="t0", z0="t1", z1="t2", skip=f".L{uniq}_slow", call=f".L{uniq}_01_call", uniq=uniq)
   // Return value expected. Get shorty, stash in callee-save to be available on return.
   // When getting shorty, stash this fast path's arg registers then restore.
   // Unconditionally stores a1/fa0, even if extra arg not found.
   fmv.s fs0, fa0
   mv s10, a1
%  get_shorty_preserve_a0(shorty="s8", y0="s9")
   mv a1, s10
   fmv.s fa0, fs0
.L${uniq}_01_call:
   jalr s7                       // args a0, and maybe a1, fa0
   beqz s8, .L${uniq}_next_op    // no shorty, no return value
%  maybe_float_returned(shorty="s8", z0="t0", z1="t1", uniq=f"{uniq}_0")
                                 // a0 := fa0 if float return
   j .L${uniq}_next_op

.L${uniq}_slow:
%  get_shorty_preserve_a0(shorty="s8", y0="s9")
%  slow_setup_args(shorty="s8", z0="t0", z1="t1", z2="t2", z3="t3", z4="t4", arg_start="0", uniq=uniq)
   jalr s7                       // args in a0-a5, fa0-fa4
%  maybe_float_returned(shorty="s8", z0="t0", z1="t1", uniq=f"{uniq}_1")
                                 // a0 := fa0 if float return
.L${uniq}_next_op:
   FETCH_ADVANCE_INST 3
   GET_INST_OPCODE t0
   GOTO_OPCODE t0


// NterpInvokeInterface
// a0: ArtMethod*
// a1: this
// a2: the target interface method
//     - ignored in nterp-to-nterp transfer
//     - side-loaded into T0 as a "hidden argument" in managed ABI transfer
%def nterp_invoke_interface(uniq="invoke_interface"):
   unimp


// NterpInvokePolymorphic
%def nterp_invoke_polymorphic(uniq="invoke_polymorphic"):
   unimp


// NterpInvokeCustom
%def nterp_invoke_custom(uniq="invoke_custom"):
   unimp


// NterpInvokeVirtualRange
%def nterp_invoke_virtual_range():
%  nterp_invoke_direct_range(uniq="invoke_virtual_range")


// NterpInvokeSuperRange
%def nterp_invoke_super_range():
%  nterp_invoke_direct_range(uniq="invoke_super_range")


// NterpInvokeDirectRange
%def nterp_invoke_direct_range(uniq="invoke_direct_range"):
   unimp


// NterpInvokeStringInitRange
%def nterp_invoke_string_init_range(uniq="invoke_string_init_range"):
   unimp


// NterpInvokeStaticRange
%def nterp_invoke_static_range(uniq="invoke_static_range"):
   unimp


// NterpInvokeInterfaceRange
// a0: ArtMethod*
// a1: this
// a2: the target interface method
//     - ignored in nterp-to-nterp transfer
//     - side-loaded into T0 as a "hidden argument" in managed ABI transfer
%def nterp_invoke_interface_range(uniq="invoke_interface_range"):
   unimp


// NterpInvokePolymorphicRange
%def nterp_invoke_polymorphic_range(uniq="invoke_polymorphic_range"):
   unimp


// NterpInvokeCustomRange
%def nterp_invoke_custom_range(uniq="invoke_custom_range"):
   unimp


// fast path and slow path helpers


// Input
// - quick: quick code ptr
// Temporaries: z0
%def try_nterp(quick="", z0="", skip=""):
   lla $z0, ExecuteNterpImpl
   bne $z0, $quick, $skip

// Static variant.
%def try_simple_args_static(ins="", z0="", z1="", skip="", uniq=""):
   lwu $z0, ART_METHOD_ACCESS_FLAGS_OFFSET(a0)
   bexti $z0, $z0, ART_METHOD_NTERP_INVOKE_FAST_PATH_FLAG_BIT
   beqz $z0, $skip
   beqz $ins, .L${uniq}_simple_done   // A = 0: no further args.
   FETCH $z1, count=2                 // z1 := F|E|D|C
   li $z0, 2
   blt $ins, $z0, .L${uniq}_simple_1  // A = 1
   beq $ins, $z0, .L${uniq}_simple_2  // A = 2
   li $z0, 4
   blt $ins, $z0, .L${uniq}_simple_3  // A = 3
   beq $ins, $z0, .L${uniq}_simple_4  // A = 4
   // A = 5
   srliw $z0, xINST, 8                // z0 := A|G
   andi $z0, $z0, 0xF                 // z0 := G
   GET_VREG a5, $z0
.L${uniq}_simple_4:
   srliw $z0, $z1, 12                 // z0 := F
   GET_VREG a4, $z0
.L${uniq}_simple_3:
   srliw $z0, $z1, 8                  // z0 := F|E
   andi $z0, $z0, 0xF                 // z0 := E
   GET_VREG a3, $z0
.L${uniq}_simple_2:
   srliw $z0, $z1, 4                  // z0 := F|E|D
   andi $z0, $z0, 0xF                 // z0 := D
   GET_VREG a2, $z0
.L${uniq}_simple_1:
   andi $z0, $z1, 0xF                 // z0 := C
   GET_VREG a1, $z0
.L${uniq}_simple_done:


// Static variant.
%def try_01_args_static(ins="", z0="", z1="", skip="", call="", uniq=""):
   beqz $ins, .L${uniq}_01_peek_next  // A = 0
   li $z0, 1                    // z0 := imm 1
   bgt $ins, $z0, $skip         // A >= 2
   // A = 1
   FETCH $z1, count=2, width=8, byte=0
                                // z1 := D|C
   andi $z1, $z1, 0xF           // z1 := C
   GET_VREG a1, $z1
   fmv.w.x fa0, a1
.L${uniq}_01_peek_next:
%  try_01_args_peek_next(z0=z0)  # z0 is zero if invoke has return value
   bnez $z0, $call


%def try_01_args_peek_next(z0=""):
   FETCH $z0, count=3, width=8, byte=0
                                // z0 := next op
   bclri $z0, $z0, 0            // clear bit #0
   addi $z0, $z0, -0x0A         // z0 := zero if op is 0x0A or 0x0B


// The invoked method might return in FA0, via managed ABI.
// The next opcode, MOVE-RESULT{-WIDE}, expects the value in A0.
%def maybe_float_returned(shorty="", z0="", z1="", uniq=""):
   lb $z0, ($shorty)  // z0 := first byte of shorty; type of return
   li $z1, 'F'        //
   beq $z0, $z1, .L${uniq}_float_return_move
   li $z1, 'D'        //
   bne $z0, $z1, .L${uniq}_float_return_done
.L${uniq}_float_return_move:
   // If fa0 carries a 32-bit float, the hi bits of fa0 will contain all 1's (NaN boxing).
   // The use of fmv.x.d will transfer those hi bits into a0, and that's okay, because the next
   // opcode, move-result, will only read the lo 32-bits of a0 - the box bits are correctly ignored.
   // If fa0 carries a 64-bit float, then fmv.x.d works as expected.
   fmv.x.d a0, fa0
.L${uniq}_float_return_done:


// Static variant.
%def get_shorty_preserve_a0(shorty="", y0=""):
   mv $y0, a0
   call NterpGetShorty  // arg a0
   mv $shorty, a0
   mv a0, $y0


// Hardcoded
// - a0: ArtMethod*
// - a1: this
%def slow_setup_args(shorty="", z0="", z1="", z2="", z3="", z4="", arg_start="1", uniq=""):
   srliw $z0, xINST, 12    // z0 := A
   li $z1, 5
   FETCH $z2, count=2      // z2 := F|E|D|C
   blt $z0, $z1, .L${uniq}_slow_gpr
   // A = 5: need vreg G
   srliw $z1, xINST, 8     // z1 := A|G
   andi $z1, $z1, 0xF      // z1 := G
   slliw $z1, $z1, 16      // z1 := G0000
   add $z2, $z1, $z2       // z2 := G|F|E|D|C

.L${uniq}_slow_gpr:
   addi $z0, $shorty, 1    // z0 := first arg of shorty
   srliw $z1, $z2, 4*$arg_start  // z1 := (instance) F|E|D or G|F|E|D, (static) F|E|D|C or G|F|E|D|C
   // linear scan through shorty: extract non-float vregs
%  if arg_start == "0":  # static can place vC into a1; instance already loaded "this" into a1.
%    load_vreg_in_gpr(gpr="a1", shorty=z0, vregs=z1, z0=z3, z1=z4, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_0")
%  load_vreg_in_gpr(gpr="a2", shorty=z0, vregs=z1, z0=z3, z1=z4, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_1")
%  load_vreg_in_gpr(gpr="a3", shorty=z0, vregs=z1, z0=z3, z1=z4, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_2")
%  load_vreg_in_gpr(gpr="a4", shorty=z0, vregs=z1, z0=z3, z1=z4, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_3")
%  load_vreg_in_gpr(gpr="a5", shorty=z0, vregs=z1, z0=z3, z1=z4, done=f".L{uniq}_slow_fpr", uniq=f"{uniq}_4")

.L${uniq}_slow_fpr:
   addi $z0, $shorty, 1    // z0 := first arg of shorty
   srliw $z1, $z2, 4*$arg_start  // z1 := (instance) F|E|D or G|F|E|D, (static) F|E|D|C or G|F|E|D|C
   // linear scan through shorty: extract float/double vregs
%  load_vreg_in_fpr(fpr="fa0", shorty=z0, vregs=z1, z0=z3, z1=z4, done=f".L{uniq}_slow_done", uniq=f"{uniq}_0")
%  load_vreg_in_fpr(fpr="fa1", shorty=z0, vregs=z1, z0=z3, z1=z4, done=f".L{uniq}_slow_done", uniq=f"{uniq}_1")
%  load_vreg_in_fpr(fpr="fa2", shorty=z0, vregs=z1, z0=z3, z1=z4, done=f".L{uniq}_slow_done", uniq=f"{uniq}_2")
%  load_vreg_in_fpr(fpr="fa3", shorty=z0, vregs=z1, z0=z3, z1=z4, done=f".L{uniq}_slow_done", uniq=f"{uniq}_3")
%  if arg_start == "0":  # static can place G into fa4; instance has only 4 args.
%    load_vreg_in_fpr(fpr="fa4", shorty=z0, vregs=z1, z0=z3, z1=z4, done=f".L{uniq}_slow_done", uniq=f"{uniq}_4")
%#:
.L${uniq}_slow_done:


// Iterate through 4-bit vreg ids in the "vregs" register, load a non-FP value
// into one argument register.
%def load_vreg_in_gpr(gpr="", shorty="", vregs="", z0="", z1="", done="", uniq=""):
.L${uniq}_gpr_find:
    lb $z0, ($shorty)         // z0 := next shorty arg spec
    addi $shorty, $shorty, 1  // increment char ptr
    beqz $z0, $done           // z0 == \0
    li $z1, 'F'               // float
    beq $z0, $z1, .L${uniq}_gpr_skip_4_bytes
    li $z1, 'D'               // double
    beq $z0, $z1, .L${uniq}_gpr_skip_8_bytes

    li $z1, 'J'               // long
    andi $gpr, $vregs, 0xF    // gpr := vreg id
    beq $z0, $z1, .L${uniq}_gpr_load_8_bytes
    GET_VREG $gpr, $gpr       // gpr := 32-bit load
    srliw $vregs, $vregs, 4   // shift out the processed arg, one vreg
    j .L${uniq}_gpr_set       // and exit
.L${uniq}_gpr_load_8_bytes:
    GET_VREG_WIDE $gpr, $gpr  // gpr := 64-bit load
    srliw $vregs, $vregs, 8   // shift out the processed arg, a vreg pair
    j .L${uniq}_gpr_set       // and exit

.L${uniq}_gpr_skip_8_bytes:
    srliw $vregs, $vregs, 4   // shift out a skipped arg
.L${uniq}_gpr_skip_4_bytes:
    srliw $vregs, $vregs, 4   // shift out a skipped arg
    j .L${uniq}_gpr_find
.L${uniq}_gpr_set:


// Iterate through 4-bit vreg ids in the "vregs" register, load a float or double
// value into one floating point argument register.
%def load_vreg_in_fpr(fpr="", shorty="", vregs="", z0="", z1="", done="", uniq=""):
.L${uniq}_fpr_find:
    lb $z0, ($shorty)         // z0 := next shorty arg spec
    addi $shorty, $shorty, 1  // increment char ptr
    beqz $z0, $done           // z0 == \0
    li $z1, 'F'               // float
    beq $z0, $z1, .L${uniq}_fpr_load_4_bytes
    li $z1, 'D'               // double
    beq $z0, $z1, .L${uniq}_fpr_load_8_bytes

    li $z1, 'J'               // long
    srliw $vregs, $vregs, 4   // shift out a skipped arg, one vreg
    bne $z0, $z1, .L${uniq}_fpr_find
    srliw $vregs, $vregs, 4   // shift out one more skipped arg, for J
    j .L${uniq}_fpr_find

.L${uniq}_fpr_load_4_bytes:
    andi $z1, $vregs, 0xF
    GET_VREG_FLOAT $fpr, $z1
    srliw $vregs, $vregs, 4   // shift out the processed arg, one vreg
    j .L${uniq}_fpr_set
.L${uniq}_fpr_load_8_bytes:
    andi $z1, $vregs, 0xF
    GET_VREG_DOUBLE $fpr, $z1
    srliw $vregs, $vregs, 8   // shift out the processed arg, a vreg pair
.L${uniq}_fpr_set:


// NterpToNterpInstance
// a0: ArtMethod*
// a1: this
%def nterp_to_nterp_instance():
   .cfi_startproc
   unimp
   .cfi_endproc


// NterpToNterpStringInit
// a0: ArtMethod*
// a1: this
%def nterp_to_nterp_string_init():
   .cfi_startproc
   unimp
   .cfi_endproc


// NterpToNterpStatic
// a0: ArtMethod*
%def nterp_to_nterp_static():
%  nterp_to_nterp(a1_instance=False, how_vC="load", uniq="n2n_static")


// NterpToNterpInstanceRange
%def nterp_to_nterp_instance_range():
%  nterp_to_nterp_range()


// NterpToNterpStringInitRange
%def nterp_to_nterp_string_init_range():
%  nterp_to_nterp_range()


// NterpToNterpStaticRange
%def nterp_to_nterp_static_range():
%  nterp_to_nterp_range()


// helpers

%def nterp_to_nterp(a1_instance=True, how_vC="", uniq=""):
   .cfi_startproc
%  setup_nterp_frame(cfi_refs="23", refs="s7", fp="s8", pc="s9", regs="s10", spills_sp="s11", z0="t0", z1="t1", z2="t2", z3="t3", uniq=uniq)
       // sp := callee sp
       // s7 := callee xREFS
       // s8 := callee xFP
       // s9 := callee xPC
       // s10 := fp/refs vreg count
       // s11 := caller sp (unused here)
%  n2n_arg_move(refs="s7", fp="s8", pc="s9", regs="s10", z0="t0", z1="t1", z2="t2", z3="t3", z4="t4", a1_instance=a1_instance, how_vC=how_vC, uniq=uniq)
   mv xREFS, s7
   mv xFP, s8
   mv xPC, s9
   CFI_DEFINE_DEX_PC_WITH_OFFSET(/*tmpReg*/CFI_TMP, /*dexReg*/CFI_DEX, /*dexOffset*/0)

   START_EXECUTING_INSTRUCTIONS
   .cfi_endproc


%def nterp_to_nterp_range(a1_instance=True, how_vC="", uniq=""):
   .cfi_startproc
   unimp
   .cfi_endproc

// See runtime/nterp_helpers.cc for a diagram of the setup.
// Hardcoded
// - a0 - ArtMethod*
// Input
// - \cfi_refs: dwarf register number of \refs, for CFI
// - \uniq: string to ensure unique symbolic labels between instantiations
// Output
// - sp: adjusted downward for callee-saves and nterp frame
// - \refs: callee xREFS
// - \fp: callee xFP
// - \pc: callee xPC
// - \regs: register count in \refs
// - \ins: in count
// - \spills_sp: stack pointer after reg spills
%def setup_nterp_frame(cfi_refs="", refs="", fp="", pc="", regs="", ins="zero", spills_sp="", z0="", z1="", z2="", z3="", uniq=""):
   // Check guard page for stack overflow.
   li $z0, -STACK_OVERFLOW_RESERVED_BYTES
   add $z0, $z0, sp
   ld zero, ($z0)

   INCREASE_FRAME NTERP_SIZE_SAVE_CALLEE_SAVES
                        // sp := sp + callee-saves
   SETUP_NTERP_SAVE_CALLEE_SAVES

   ld $pc, ART_METHOD_DATA_OFFSET_64(a0)
   FETCH_CODE_ITEM_INFO code_item=$pc, regs=$regs, outs=$z0, ins=$ins
                        // pc   := callee dex array
                        // regs := vreg count for fp array and refs array
                        // z0   := vreg count for outs array
                        // ins  := vreg count for ins array

   // Compute required frame size: ((2 * \regs) + \z0) * 4 + 24
   // - The register array and reference array each have \regs number of slots.
   // - The out array has \z0 slots.
   // - Each register slot is 4 bytes.
   // - Additional 24 bytes for 3 fields: saved frame pointer, dex pc, and ArtMethod*.
   sh1add $z1, $regs, $z0
   slli $z1, $z1, 2
   addi $z1, $z1, 24    // z1 := frame size, without alignment padding

   // compute new stack pointer
   sub $z1, sp, $z1
   // 16-byte alignment.
   andi $z1, $z1, ~0xF  // z1 := new sp

   // Set \refs to base of reference array. Align to pointer size for the frame pointer and dex pc
   // pointer, below the reference array.
   sh2add $z0, $z0, $z1  // z0 := out array size in bytes
   addi $z0, $z0, 28     //     + 24 bytes for 3 fields, plus 4 for alignment slack.
   andi $refs, $z0, -__SIZEOF_POINTER__
                         // refs := refs array base

   // Set \fp to base of register array, above the reference array. This region is already aligned.
   sh2add $fp, $regs, $refs
                         // fp := fp array base

   // Set up the stack pointer.
   mv $spills_sp, sp     // spills_sp := old sp
   .cfi_def_cfa_register $spills_sp
   mv sp, $z1            // sp := new sp
   sd $spills_sp, -8($refs)
   // The CFA rule is now a dwarf expression, because the nterp frame offset for SP is a dynamic
   // value, and thus SP cannot help compute CFA. For the duration of the nterp frame, CFI
   // directives cannot adjust this CFA rule, but may still capture CFI for register spills as
   // "register + offset" with a dwarf expression.
   CFI_DEF_CFA_BREG_PLUS_UCONST $cfi_refs, -8, NTERP_SIZE_SAVE_CALLEE_SAVES

   // Put nulls in reference array.
   beqz $regs, .L${uniq}_ref_zero_done
   mv $z0, $refs         // z0 := address iterator
.L${uniq}_ref_zero:
   // Write in 8-byte increments, so fp[0] gets zero'ed too, if \regs is odd.
   sd zero, ($z0)
   addi $z0, $z0, 8
   bltu $z0, $fp, .L${uniq}_ref_zero
.L${uniq}_ref_zero_done:
   // Save the ArtMethod*.
   sd a0, (sp)


// Hardcoded
// - (caller) xINST, xFP, xREFS, xPC
// - a0: ArtMethod*
// - a1: this, for instance invoke
%def n2n_arg_move(refs="", fp="", regs="", pc="", z0="", z1="", z2="", z3="", z4="", a1_instance=True, how_vC="", uniq=""):
   srliw $z0, xINST, 12       // z0 := A (arg count)

%  if not a1_instance:
     beqz $z0, .L${uniq}_arg_done
%#:
   // A >= 1, decide and branch
   li $z1, 2
   sub $z2, $regs, $z0        // z2 := regs - A; vC's index in fp
   sh2add $z3, $z2, $fp       // z3 := addr of fp[C]
   sh2add $z2, $z2, $refs     // z2 := addr of refs[C]
   FETCH $z4, 2               // z4 := F|E|D|C
   blt $z0, $z1, .L${uniq}_arg_1
   beq $z0, $z1, .L${uniq}_arg_2
   li $z1, 4
   blt $z0, $z1, .L${uniq}_arg_3
   beq $z0, $z1, .L${uniq}_arg_4

   // A = 5
   slliw $z0, xINST, 8
   andi $z0, $z0, 0xF         // z0 := G
   GET_VREG $z1, $z0          // z1 := xFP[G]
   sw $z1, (4*4)($z3)         // fp[G] := z1
   GET_VREG_OBJECT $z0, $z0   // z0 := xREFS[G]
   sw $z0, (4*4)($z2)         // refs[G] := z0
.L${uniq}_arg_4:
   srliw $z0, $z4, 12         // z0 := F
   GET_VREG $z1, $z0          // z1 := xFP[F]
   sw $z1, (3*4)($z3)         // fp[F] := z1
   GET_VREG_OBJECT $z0, $z0   // z0 := xREFS[F]
   sw $z0, (3*4)($z2)         // refs[F] := z0
.L${uniq}_arg_3:
   srliw $z0, $z4, 8          // z0 := F|E
   andi $z0, $z0, 0xF         // z0 := E
   GET_VREG $z1, $z0          // z1 := xFP[E]
   sw $z1, (2*4)($z3)         // fp[E] := z1
   GET_VREG_OBJECT $z0, $z0   // z0 := xREFS[E]
   sw $z0, (2*4)($z2)         // refs[E] := z0
.L${uniq}_arg_2:
   srliw $z0, $z4, 4          // z0 := F|E|D
   andi $z0, $z0, 0xF         // z0 := D
   GET_VREG $z1, $z0          // z1 := xFP[D]
   sw $z1, (1*4)($z3)         // fp[D] := z1
   GET_VREG_OBJECT $z0, $z0   // z0 := xREFS[D]
   sw $z0, (1*4)($z2)         // refs[D] := z0
.L${uniq}_arg_1:
%  if how_vC == "in_a1":
     // a1 = xFP[C] from earlier stage of instance invoke
     sw a1, (0*4)($z3)        // fp[C] := a1
     sw a1, (0*4)($z2)        // refs[C] := a1
%  elif how_vC == "skip":
     // string init doesn't read "this"
%  elif how_vC == "load":
     // static method loads vC just like other vregs
     andi $z0, $z4, 0xF       // z0 := C
     GET_VREG $z1, $z0        // z1 := xFP[C]
     sw $z1, (0*4)($z3)       // fp[C] := z1
     GET_VREG_OBJECT $z0, $z0 // z0 := xREFS[C]
     sw $z0, (0*4)($z2)       // refs[C] := z0
%#:
.L${uniq}_arg_done:


//
// Nterp entry point helpers
//


// Hardcoded:
// - a0: ArtMethod*
%def setup_ref_args_and_go(fp="", refs="", refs_end="", spills_sp="", z0="", z1="", done=""):
   // Store managed-ABI register args into fp/refs arrays.
%  store_ref_to_vreg(gpr="a1", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a2", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a3", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a4", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a5", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a6", fp=fp, refs=refs, refs_end=refs_end, done=done)
%  store_ref_to_vreg(gpr="a7", fp=fp, refs=refs, refs_end=refs_end, done=done)
   // We drained arg registers, so continue from caller's stack.
   // A ref arg is 4 bytes, so the continuation offset is well known.
   addi $z0, $spills_sp, (NTERP_SIZE_SAVE_CALLEE_SAVES + 8 + 7*4)
       // z0 := out array base addr + 7 vreg slots
.Lentry_ref_stack:
   lwu $z1, ($z0)
   sw $z1, ($fp)
   sw $z1, ($refs)
   addi $z0, $z0, 4
   addi $fp, $fp, 4
   addi $refs, $refs, 4
   bne $refs, $refs_end, .Lentry_ref_stack

   j $done


%def store_ref_to_vreg(gpr="", fp="", refs="", refs_end="", done=""):
   sw $gpr, ($fp)
   sw $gpr, ($refs)
   addi $fp, $fp, 4
   addi $refs, $refs, 4
   beq $refs, $refs_end, $done


// \fp and \refs are used as array base addrs, unmodified.
%def store_gpr_to_vreg(gpr="", offset="", shorty="", fp="", refs="", z0="", z1="", D="", F="", J="", L="", next=""):
.Lentry_arg_${gpr}:
   lb $z0, ($shorty)         // z0 := shorty type
   addi $shorty, $shorty, 1  // Increment char ptr.
   beqz $z0, $next           // z0 = \0: finished shorty pass
   beq $z0, $D, .Lentry_arg_skip_double_${gpr}
   beq $z0, $F, .Lentry_arg_skip_float_${gpr}

   add $z1, $offset, $fp
   beq $z0, $J, .Lentry_arg_long_${gpr}
   sw $gpr, ($z1)
   bne $z0, $L, .Lentry_arg_finish_${gpr}
   add $z1, $offset, $refs
   sw $gpr, ($z1)
   j .Lentry_arg_finish_${gpr}
.Lentry_arg_skip_double_${gpr}:
   addi $offset, $offset, 4
.Lentry_arg_skip_float_${gpr}:
   addi $offset, $offset, 4
   j .Lentry_arg_${gpr}
.Lentry_arg_long_${gpr}:
   sd $gpr, ($z1)
   addi $offset, $offset, 4
.Lentry_arg_finish_${gpr}:
   addi $offset, $offset, 4


// \fp is used as array base addr, unmodified.
%def store_fpr_to_vreg(fpr="", offset="", shorty="", fp="", z0="", z1="", D="", F="", J="", next=""):
.Lentry_farg_${fpr}:
   lb $z0, ($shorty)         // z0 := shorty type
   addi $shorty, $shorty, 1  // Increment char ptr.
   beqz $z0, $next           // z0 = \0: finished shorty pass
   beq $z0, $D, .Lentry_farg_double_${fpr}
   beq $z0, $F, .Lentry_farg_float_${fpr}
   addi $offset, $offset, 4
   bne $z0, $J, .Lentry_farg_${fpr}
   addi $offset, $offset, 4
   j .Lentry_farg_${fpr}

.Lentry_farg_float_${fpr}:
   add $z1, $offset, $fp
   fsw $fpr, ($z1)
   j .Lentry_farg_finish_${fpr}
.Lentry_farg_double_${fpr}:
   add $z1, $offset, $fp
   fsd $fpr, ($z1)
   addi $offset, $offset, 4
.Lentry_farg_finish_${fpr}:
   addi $offset, $offset, 4


// \outs, \fp, \refs are used as iterators, modified.
%def store_outs_to_vregs(outs="", shorty="", fp="", refs="", z0="", z1="", D="", F="", J="", L="", next=""):
.Lentry_stack:
   lb $z0, ($shorty)         // z0 := next shorty arg spec
   addi $shorty, $shorty, 1  // Increment char ptr.
   beqz $z0, $next           // z0 == \0
   beq $z0, $F, .Lentry_stack_next_4
   beq $z0, $D, .Lentry_stack_next_8
   beq $z0, $J, .Lentry_stack_long
   // 32-bit arg
   lwu $z1, ($outs)
   sw $z1, ($fp)
   bne $z0, $L, .Lentry_stack_next_4
   // and also a ref
   sw $z1, ($refs)
.Lentry_stack_next_4:
   addi $outs, $outs, 4
   addi $fp, $fp, 4
   addi $refs, $refs, 4
   j .Lentry_stack
.Lentry_stack_long:
   ld $z1, ($outs)
   sd $z1, ($fp)
.Lentry_stack_next_8:
   addi $outs, $outs, 8
   addi $fp, $fp, 8
   addi $refs, $refs, 8
   j .Lentry_stack


// \outs, \fp are used as iterators, modified.
%def store_float_outs_to_vregs(outs="", shorty="", fp="", z0="", D="", F="", J="", next=""):
.Lentry_fstack:
   lb $z0, ($shorty)         // z0 := next shorty arg spec
   addi $shorty, $shorty, 1  // Increment char ptr.
   beqz $z0, $next           // z0 == \0
   beq $z0, $F, .Lentry_fstack_float
   beq $z0, $D, .Lentry_fstack_double
   beq $z0, $J, .Lentry_fstack_next_8
   // 32-bit arg
   addi $outs, $outs, 4
   addi $fp, $fp, 4
   j .Lentry_fstack
.Lentry_fstack_float:
   lwu $z0, ($outs)
   sw $z0, ($fp)
   addi $outs, $outs, 4
   addi $fp, $fp, 4
   j .Lentry_fstack
.Lentry_fstack_double:
   ld $z0, ($outs)
   sd $z0, ($fp)
.Lentry_fstack_next_8:
   addi $outs, $outs, 8
   addi $fp, $fp, 8
   j .Lentry_fstack

