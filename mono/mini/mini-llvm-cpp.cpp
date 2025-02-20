//
// mini-llvm-cpp.cpp: C++ support classes for the mono LLVM integration
//
// (C) 2009-2011 Novell, Inc.
// Copyright 2011 Xamarin, Inc (http://www.xamarin.com)
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
//

//
// We need to override some stuff in LLVM, but this cannot be done using the C
// interface, so we have to use some C++ code here.
// The things which we override are:
// - the default JIT code manager used by LLVM doesn't allocate memory using
//   MAP_32BIT, we require it.
// - add some callbacks so we can obtain the size of methods and their exception
//   tables.
//

//
// Mono's internal header files are not C++ clean, so avoid including them if 
// possible
//

#ifdef _MSC_VER
// Disable warnings generated by LLVM headers.
#pragma warning(disable:4141) // modifier' : used more than once
#pragma warning(disable:4800) // type' : forcing value to bool 'true' or 'false' (performance warning)
#endif

#include "config.h"

#include <stdint.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/CallSite.h>
#include <llvm/IR/MDBuilder.h>

#include "mini-llvm-cpp.h"

using namespace llvm;

#define Acquire AtomicOrdering::Acquire
#define Release AtomicOrdering::Release
#define SequentiallyConsistent AtomicOrdering::SequentiallyConsistent

void
mono_llvm_dump_value (LLVMValueRef value)
{
	/* Same as LLVMDumpValue (), but print to stdout */
	outs () << (*unwrap<Value> (value)) << "\n";
	fflush (stdout);
}

void
mono_llvm_dump_module (LLVMModuleRef module)
{
	/* Same as LLVMDumpModule (), but print to stdout */
	outs () << (*unwrap (module));
	fflush (stdout);
}

/* Missing overload for building an alloca with an alignment */
LLVMValueRef
mono_llvm_build_alloca (LLVMBuilderRef builder, LLVMTypeRef Ty, 
						LLVMValueRef ArraySize,
						int alignment, const char *Name)
{
	return wrap (unwrap (builder)->Insert (new AllocaInst (unwrap (Ty), 0, unwrap (ArraySize), alignment), Name));
}

LLVMValueRef 
mono_llvm_build_load (LLVMBuilderRef builder, LLVMValueRef PointerVal,
					  const char *Name, gboolean is_volatile)
{
	LoadInst *ins = unwrap(builder)->CreateLoad(unwrap(PointerVal), is_volatile, Name);

	return wrap(ins);
}

LLVMValueRef
mono_llvm_build_atomic_load (LLVMBuilderRef builder, LLVMValueRef PointerVal,
							 const char *Name, gboolean is_volatile, int alignment, BarrierKind barrier)
{
	LoadInst *ins = unwrap(builder)->CreateLoad(unwrap(PointerVal), is_volatile, Name);

	ins->setAlignment (alignment);
	switch (barrier) {
	case LLVM_BARRIER_NONE:
		break;
	case LLVM_BARRIER_ACQ:
		ins->setOrdering(Acquire);
		break;
	case LLVM_BARRIER_SEQ:
		ins->setOrdering(SequentiallyConsistent);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	return wrap(ins);
}

LLVMValueRef 
mono_llvm_build_aligned_load (LLVMBuilderRef builder, LLVMValueRef PointerVal,
							  const char *Name, gboolean is_volatile, int alignment)
{
	LoadInst *ins;

	ins = unwrap(builder)->CreateLoad(unwrap(PointerVal), is_volatile, Name);
	ins->setAlignment (alignment);

	return wrap(ins);
}

LLVMValueRef 
mono_llvm_build_store (LLVMBuilderRef builder, LLVMValueRef Val, LLVMValueRef PointerVal,
					  gboolean is_volatile, BarrierKind barrier)
{
	StoreInst *ins = unwrap(builder)->CreateStore(unwrap(Val), unwrap(PointerVal), is_volatile);

	switch (barrier) {
	case LLVM_BARRIER_NONE:
		break;
	case LLVM_BARRIER_REL:
		ins->setOrdering(Release);
		break;
	case LLVM_BARRIER_SEQ:
		ins->setOrdering(SequentiallyConsistent);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	return wrap(ins);
}

LLVMValueRef 
mono_llvm_build_aligned_store (LLVMBuilderRef builder, LLVMValueRef Val, LLVMValueRef PointerVal,
							   gboolean is_volatile, int alignment)
{
	StoreInst *ins;

	ins = unwrap(builder)->CreateStore(unwrap(Val), unwrap(PointerVal), is_volatile);
	ins->setAlignment (alignment);

	return wrap (ins);
}

LLVMValueRef
mono_llvm_build_cmpxchg (LLVMBuilderRef builder, LLVMValueRef ptr, LLVMValueRef cmp, LLVMValueRef val)
{
	AtomicCmpXchgInst *ins;

	ins = unwrap(builder)->CreateAtomicCmpXchg (unwrap(ptr), unwrap (cmp), unwrap (val), SequentiallyConsistent, SequentiallyConsistent);
	return wrap (ins);
}

LLVMValueRef
mono_llvm_build_atomic_rmw (LLVMBuilderRef builder, AtomicRMWOp op, LLVMValueRef ptr, LLVMValueRef val)
{
	AtomicRMWInst::BinOp aop = AtomicRMWInst::Xchg;
	AtomicRMWInst *ins;

	switch (op) {
	case LLVM_ATOMICRMW_OP_XCHG:
		aop = AtomicRMWInst::Xchg;
		break;
	case LLVM_ATOMICRMW_OP_ADD:
		aop = AtomicRMWInst::Add;
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	ins = unwrap (builder)->CreateAtomicRMW (aop, unwrap (ptr), unwrap (val), SequentiallyConsistent);
	return wrap (ins);
}

LLVMValueRef
mono_llvm_build_fence (LLVMBuilderRef builder, BarrierKind kind)
{
	FenceInst *ins;
	AtomicOrdering ordering;

	g_assert (kind != LLVM_BARRIER_NONE);

	switch (kind) {
	case LLVM_BARRIER_ACQ:
		ordering = Acquire;
		break;
	case LLVM_BARRIER_REL:
		ordering = Release;
		break;
	case LLVM_BARRIER_SEQ:
		ordering = SequentiallyConsistent;
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	ins = unwrap (builder)->CreateFence (ordering);
	return wrap (ins);
}

LLVMValueRef
mono_llvm_build_weighted_branch (LLVMBuilderRef builder, LLVMValueRef cond, LLVMBasicBlockRef t, LLVMBasicBlockRef f, uint32_t t_weight, uint32_t f_weight)
{
	auto b = unwrap (builder);
	auto &ctx = b->getContext ();
	MDBuilder mdb{ctx};
	auto weights = mdb.createBranchWeights (t_weight, f_weight);
	auto ins = b->CreateCondBr (unwrap (cond), unwrap (t), unwrap (f), weights);
	return wrap (ins);
}

void
mono_llvm_add_string_metadata (LLVMValueRef insref, const char* label, const char* text)
{
	auto ins = unwrap<Instruction> (insref);
	auto &ctx = ins->getContext ();
	ins->setMetadata (label, MDNode::get (ctx, MDString::get (ctx, text)));
}

void
mono_llvm_set_implicit_branch (LLVMBuilderRef builder, LLVMValueRef branch)
{
	auto b = unwrap (builder);
	auto &ctx = b->getContext ();
	auto ins = unwrap<Instruction> (branch);
	ins->setMetadata (LLVMContext::MD_make_implicit, MDNode::get (ctx, {}));
}

void
mono_llvm_set_must_tailcall (LLVMValueRef call_ins)
{
	CallInst *ins = (CallInst*)unwrap (call_ins);

	ins->setTailCallKind (CallInst::TailCallKind::TCK_MustTail);
}

void
mono_llvm_replace_uses_of (LLVMValueRef var, LLVMValueRef v)
{
	Value *V = ConstantExpr::getTruncOrBitCast (unwrap<Constant> (v), unwrap (var)->getType ());
	unwrap (var)->replaceAllUsesWith (V);
}

LLVMValueRef
mono_llvm_create_constant_data_array (const uint8_t *data, int len)
{
	return wrap(ConstantDataArray::get (*unwrap(LLVMGetGlobalContext ()), makeArrayRef(data, len)));
}

void
mono_llvm_set_is_constant (LLVMValueRef global_var)
{
	unwrap<GlobalVariable>(global_var)->setConstant (true);
}

// Note that in future versions of LLVM, CallInst and InvokeInst
// share a CallBase parent class that would make the below methods
// look much better

void
mono_llvm_set_call_nonnull_arg (LLVMValueRef wrapped_calli, int argNo)
{
	Instruction *calli = unwrap<Instruction> (wrapped_calli);

	if (isa<CallInst> (calli))
		dyn_cast<CallInst>(calli)->addParamAttr (argNo, Attribute::NonNull);
	else
		dyn_cast<InvokeInst>(calli)->addParamAttr (argNo, Attribute::NonNull);
}

void
mono_llvm_set_call_nonnull_ret (LLVMValueRef wrapped_calli)
{
	Instruction *calli = unwrap<Instruction> (wrapped_calli);

	if (isa<CallInst> (calli))
		dyn_cast<CallInst>(calli)->addAttribute (AttributeList::ReturnIndex, Attribute::NonNull);
	else
		dyn_cast<InvokeInst>(calli)->addAttribute (AttributeList::ReturnIndex, Attribute::NonNull);
}

void
mono_llvm_set_func_nonnull_arg (LLVMValueRef func, int argNo)
{
	unwrap<Function>(func)->addParamAttr (argNo, Attribute::NonNull);
}

gboolean
mono_llvm_is_nonnull (LLVMValueRef wrapped)
{
	// Argument to function
	Value *val = unwrap (wrapped);

	while (val) {
		if (Argument *arg = dyn_cast<Argument> (val)) {
			return arg->hasNonNullAttr ();
		} else if (CallInst *calli = dyn_cast<CallInst> (val)) {
			return calli->hasRetAttr (Attribute::NonNull);
		} else if (InvokeInst *calli = dyn_cast<InvokeInst> (val)) {
			return calli->hasRetAttr (Attribute::NonNull);
		} else if (LoadInst *loadi = dyn_cast<LoadInst> (val)) {
			return loadi->getMetadata("nonnull") != nullptr; // nonnull <index>
		} else if (Instruction *inst = dyn_cast<Instruction> (val)) {
			// If not a load or a function argument, the only case for us to
			// consider is that it's a bitcast. If so, recurse on what was casted.
			if (inst->getOpcode () == LLVMBitCast) {
				val = inst->getOperand (0);
				continue;
			}

			return FALSE;
		} else {
			return FALSE;
		}
	}
	return FALSE;
}

GSList *
mono_llvm_calls_using (LLVMValueRef wrapped_local)
{
	GSList *usages = NULL;
	Value *local = unwrap (wrapped_local);

	for (User *user : local->users ()) {
		if (isa<CallInst> (user) || isa<InvokeInst> (user)) {
			usages = g_slist_prepend (usages, wrap (user));
		}
	}

	return usages;
}

LLVMValueRef *
mono_llvm_call_args (LLVMValueRef wrapped_calli)
{
	Value *calli = unwrap(wrapped_calli);
	CallInst *call = dyn_cast <CallInst> (calli);
	InvokeInst *invoke = dyn_cast <InvokeInst> (calli);
	g_assert (call || invoke);

	unsigned int numOperands = 0;

	if (call)
		numOperands = call->getNumArgOperands ();
	else
		numOperands = invoke->getNumArgOperands ();

	LLVMValueRef *ret = g_malloc (sizeof (LLVMValueRef) * numOperands);

	for (int i=0; i < numOperands; i++) {
		if (call)
			ret [i] = wrap (call->getArgOperand (i));
		else
			ret [i] = wrap (invoke->getArgOperand (i));
	}

	return ret;
}

void
mono_llvm_set_call_notailcall (LLVMValueRef func)
{
	unwrap<CallInst>(func)->setTailCallKind (CallInst::TailCallKind::TCK_NoTail);
}

void
mono_llvm_set_call_noalias_ret (LLVMValueRef wrapped_calli)
{
	Instruction *calli = unwrap<Instruction> (wrapped_calli);

	if (isa<CallInst> (calli))
		dyn_cast<CallInst>(calli)->addAttribute (AttributeList::ReturnIndex, Attribute::NoAlias);
	else
		dyn_cast<InvokeInst>(calli)->addAttribute (AttributeList::ReturnIndex, Attribute::NoAlias);
}

static Attribute::AttrKind
convert_attr (AttrKind kind)
{
	switch (kind) {
	case LLVM_ATTR_NO_UNWIND:
		return Attribute::NoUnwind;
	case LLVM_ATTR_NO_INLINE:
		return Attribute::NoInline;
	case LLVM_ATTR_OPTIMIZE_FOR_SIZE:
		return Attribute::OptimizeForSize;
	case LLVM_ATTR_IN_REG:
		return Attribute::InReg;
	case LLVM_ATTR_STRUCT_RET:
		return Attribute::StructRet;
	case LLVM_ATTR_NO_ALIAS:
		return Attribute::NoAlias;
	case LLVM_ATTR_BY_VAL:
		return Attribute::ByVal;
	case LLVM_ATTR_UW_TABLE:
		return Attribute::UWTable;
	default:
		assert (0);
		return Attribute::NoUnwind;
	}
}

void
mono_llvm_add_func_attr (LLVMValueRef func, AttrKind kind)
{
	unwrap<Function> (func)->addAttribute (AttributeList::FunctionIndex, convert_attr (kind));
}

void
mono_llvm_add_param_attr (LLVMValueRef param, AttrKind kind)
{
	Function *func = unwrap<Argument> (param)->getParent ();
	int n = unwrap<Argument> (param)->getArgNo ();
	func->addParamAttr (n, convert_attr (kind));
}

void
mono_llvm_add_instr_attr (LLVMValueRef val, int index, AttrKind kind)
{
	CallSite (unwrap<Instruction> (val)).addAttribute (index, convert_attr (kind));
}

void*
mono_llvm_create_di_builder (LLVMModuleRef module)
{
	return new DIBuilder (*unwrap(module));
}

void*
mono_llvm_di_create_compile_unit (void *di_builder, const char *cu_name, const char *dir, const char *producer)
{
	DIBuilder *builder = (DIBuilder*)di_builder;

	DIFile *di_file;

	di_file = builder->createFile (cu_name, dir);
	return builder->createCompileUnit (dwarf::DW_LANG_C99, di_file, producer, true, "", 0);
}

void*
mono_llvm_di_create_function (void *di_builder, void *cu, LLVMValueRef func, const char *name, const char *mangled_name, const char *dir, const char *file, int line)
{
	DIBuilder *builder = (DIBuilder*)di_builder;
	DIFile *di_file;
	DISubroutineType *type;
	DISubprogram *di_func;

	// FIXME: Share DIFile
	di_file = builder->createFile (file, dir);
	type = builder->createSubroutineType (builder->getOrCreateTypeArray (ArrayRef<Metadata*> ()));
#if LLVM_API_VERSION >= 900
	di_func = builder->createFunction (di_file, name, mangled_name, di_file, line, type, 0);
#else
	di_func = builder->createFunction (di_file, name, mangled_name, di_file, line, type, true, true, 0);
#endif

	unwrap<Function>(func)->setMetadata ("dbg", di_func);

	return di_func;
}

void*
mono_llvm_di_create_file (void *di_builder, const char *dir, const char *file)
{
	DIBuilder *builder = (DIBuilder*)di_builder;

	return builder->createFile (file, dir);
}

void*
mono_llvm_di_create_location (void *di_builder, void *scope, int row, int column)
{
	return DILocation::get (*unwrap(LLVMGetGlobalContext ()), row, column, (Metadata*)scope);
}

void
mono_llvm_set_fast_math (LLVMBuilderRef builder)
{
	FastMathFlags flags;
	flags.setFast ();
	unwrap(builder)->setFastMathFlags (flags);
}

void
mono_llvm_di_set_location (LLVMBuilderRef builder, void *loc_md)
{
	unwrap(builder)->SetCurrentDebugLocation ((DILocation*)loc_md);
}

void
mono_llvm_di_builder_finalize (void *di_builder)
{
	DIBuilder *builder = (DIBuilder*)di_builder;

	builder->finalize ();
}

LLVMValueRef
mono_llvm_get_or_insert_gc_safepoint_poll (LLVMModuleRef module)
{
#if LLVM_API_VERSION >= 900

	llvm::FunctionCallee callee = unwrap(module)->getOrInsertFunction("gc.safepoint_poll", FunctionType::get(unwrap(LLVMVoidType()), false));
	return wrap (dyn_cast<llvm::Function> (callee.getCallee ()));
#else
	llvm::Function *SafepointPoll;
	llvm::Constant *SafepointPollConstant;

	SafepointPollConstant = unwrap(module)->getOrInsertFunction("gc.safepoint_poll", FunctionType::get(unwrap(LLVMVoidType()), false));
	g_assert (SafepointPollConstant);

	SafepointPoll = dyn_cast<llvm::Function>(SafepointPollConstant);
	g_assert (SafepointPoll);
	g_assert (SafepointPoll->empty());

	return wrap(SafepointPoll);
#endif
}
