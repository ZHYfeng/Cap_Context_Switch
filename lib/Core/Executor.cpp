//===-- Executor.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Executor.h"

#include "../Encode/ListenerService.h"
#include "klee/CommandLine.h"
#include "klee/Common.h"
#include "klee/Constraints.h"
#include "klee/Internal/ADT/ImmutableMap.h"
#include "klee/Internal/ADT/ImmutableTree.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KInstIterator.h"
#include "klee/Internal/Support/ErrorHandling.h"
#include "klee/Internal/System/MemoryUsage.h"
#include "klee/Internal/System/Time.h"
#include "klee/Solver.h"
#include "klee/Statistic.h"
#include "klee/Statistics.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprSMTLIBPrinter.h"
#include "klee/util/GetElementPtrTypeIterator.h"
#include "klee/Internal/ADT/KTest.h"
#include "../Encode/Event.h"
#include "../Encode/Prefix.h"
#include "../Encode/RuntimeDataManager.h"
#include "../Encode/Trace.h"
#include "../Encode/Transfer.h"
#include "../Thread/MutexManager.h"
#include "../Thread/StackFrame.h"
#include "../Thread/Thread.h"
#include "../Thread/ThreadScheduler.h"
#include "CallPathManager.h"
#include "Context.h"
#include "CoreStats.h"
#include "ExecutorTimerInfo.h"
#include "ExternalDispatcher.h"
#include "ImpliedValue.h"
#include "MemoryManager.h"
#include "PTree.h"
#include "Searcher.h"
#include "SpecialFunctionHandler.h"
#include "StatsTracker.h"
#include "TimingSolver.h"
#include "UserSearcher.h"

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ilist.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/DebugInfo.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalAlias.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/User.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/CallSite.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MathExtras.h>

#include <sys/types.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <sstream>
#include <alloca.h>
#include <errno.h>

using namespace llvm;
using namespace klee;

namespace {
	cl::opt<bool> DumpStatesOnHalt("dump-states-on-halt", cl::init(true),
			cl::desc("Dump test cases for all active states on exit (default=on)"));

	cl::opt<bool> RandomizeFork("randomize-fork", cl::init(false),
			cl::desc("Randomly swap the true and false states on a fork (default=off)"));

	cl::opt<bool> AllowExternalSymCalls("allow-external-sym-calls", cl::init(false),
			cl::desc(
					"Allow calls with symbolic arguments to external functions.  This concretizes the symbolic arguments.  (default=off)"));

/// The different query logging solvers that can switched on/off
	enum PrintDebugInstructionsType {
		STDERR_ALL, ///
		STDERR_SRC,
		STDERR_COMPACT,
		FILE_ALL,    ///
		FILE_SRC,    ///
		FILE_COMPACT ///
	};

	llvm::cl::list<PrintDebugInstructionsType> DebugPrintInstructions("debug-print-instructions",
			llvm::cl::desc("Log instructions during execution."),
			llvm::cl::values(clEnumValN(STDERR_ALL, "all:stderr", "Log all instructions to stderr "
					"in format [src, inst_id, "
					"llvm_inst]"), clEnumValN(STDERR_SRC, "src:stderr", "Log all instructions to stderr in format [src, inst_id]"),
					clEnumValN(STDERR_COMPACT, "compact:stderr", "Log all instructions to stderr in format [inst_id]"),
					clEnumValN(FILE_ALL, "all:file", "Log all instructions to file "
							"instructions.txt in format [src, "
							"inst_id, llvm_inst]"), clEnumValN(FILE_SRC, "src:file", "Log all instructions to file "
							"instructions.txt in format [src, "
							"inst_id]"), clEnumValN(FILE_COMPACT, "compact:file", "Log all instructions to file instructions.txt in format "
							"[inst_id]"),
					clEnumValEnd), llvm::cl::CommaSeparated);

	cl::opt<bool> DebugCheckForImpliedValues("debug-check-for-implied-values");

	cl::opt<bool> SimplifySymIndices("simplify-sym-indices", cl::init(false),
			cl::desc("Simplify symbolic accesses using equalities from other constraints (default=off)"));

	cl::opt<bool> EqualitySubstitution("equality-substitution", cl::init(true),
			cl::desc("Simplify equality expressions before querying the solver (default=on)."));

	cl::opt<unsigned> MaxSymArraySize("max-sym-array-size", cl::init(0));

	cl::opt<bool> SuppressExternalWarnings("suppress-external-warnings", cl::init(false),
			cl::desc("Supress warnings about calling external functions."));

	cl::opt<bool> AllExternalWarnings("all-external-warnings", cl::init(false),
			cl::desc("Issue an warning everytime an external call is made,"
					"as opposed to once per function (default=off)"));

	cl::opt<bool> OnlyOutputStatesCoveringNew("only-output-states-covering-new", cl::init(false),
			cl::desc("Only output test cases covering new code (default=off)."));

	cl::opt<bool> EmitAllErrors("emit-all-errors", cl::init(false), cl::desc("Generate tests cases for all errors "
			"(default=off, i.e. one per (error,instruction) pair)"));

	cl::opt<bool> NoExternals("no-externals", cl::desc("Do not allow external function calls (default=off)"));

	cl::opt<bool> AlwaysOutputSeeds("always-output-seeds", cl::init(true));

	cl::opt<bool> OnlyReplaySeeds("only-replay-seeds", cl::init(false), cl::desc("Discard states that do not have a seed (default=off)."));

	cl::opt<bool> OnlySeed("only-seed", cl::init(false),
			cl::desc("Stop execution after seeding is done without doing regular search (default=off)."));

	cl::opt<bool> AllowSeedExtension("allow-seed-extension", cl::init(false),
			cl::desc("Allow extra (unbound) values to become symbolic during seeding (default=false)."));

	cl::opt<bool> ZeroSeedExtension("zero-seed-extension", cl::init(false), cl::desc("(default=off)"));

	cl::opt<bool> AllowSeedTruncation("allow-seed-truncation", cl::init(false),
			cl::desc("Allow smaller buffers than in seeds (default=off)."));

	cl::opt<bool> NamedSeedMatching("named-seed-matching", cl::init(false),
			cl::desc("Use names to match symbolic objects to inputs (default=off)."));

	cl::opt<double> MaxStaticForkPct("max-static-fork-pct", cl::init(1.), cl::desc("(default=1.0)"));

	cl::opt<double> MaxStaticSolvePct("max-static-solve-pct", cl::init(1.), cl::desc("(default=1.0)"));

	cl::opt<double> MaxStaticCPForkPct("max-static-cpfork-pct", cl::init(1.), cl::desc("(default=1.0)"));

	cl::opt<double> MaxStaticCPSolvePct("max-static-cpsolve-pct", cl::init(1.), cl::desc("(default=1.0)"));

	cl::opt<double> MaxInstructionTime("max-instruction-time",
			cl::desc("Only allow a single instruction to take this much time (default=0s (off)). Enables --use-forked-solver"),
			cl::init(0));

	cl::opt<double> SeedTime("seed-time", cl::desc("Amount of time to dedicate to seeds, before normal search (default=0 (off))"),
			cl::init(0));

	cl::opt<unsigned int> StopAfterNInstructions("stop-after-n-instructions",
			cl::desc("Stop execution after specified number of instructions (default=0 (off))"), cl::init(0));

	cl::opt<unsigned> MaxForks("max-forks", cl::desc("Only fork this many times (default=-1 (off))"), cl::init(~0u));

	cl::opt<unsigned> MaxDepth("max-depth", cl::desc("Only allow this many symbolic branches (default=0 (off))"), cl::init(0));

	cl::opt<unsigned> MaxMemory("max-memory", cl::desc("Refuse to fork when above this amount of memory (in MB, default=2000)"),
			cl::init(16000));

	cl::opt<bool> MaxMemoryInhibit("max-memory-inhibit", cl::desc("Inhibit forking at memory cap (vs. random terminate) (default=on)"),
			cl::init(true));
}

namespace klee {
	RNG theRNG;
}

bool Executor::hasInitialized = false;

Executor::Executor(const InterpreterOptions &opts, InterpreterHandler *ih) :
		Interpreter(opts), kmodule(0), interpreterHandler(ih), searcher(0), externalDispatcher(new ExternalDispatcher()), statsTracker(0), pathWriter(
				0), symPathWriter(0), specialFunctionHandler(0), processTree(0), replayKTest(0), replayPath(0), replayPosition(0), usingSeeds(
				0), atMemoryLimit(false), inhibitForking(false), haltExecution(false), ivcEnabled(false), coreSolverTimeout(
				MaxCoreSolverTime != 0 && MaxInstructionTime != 0 ?
						std::min(MaxCoreSolverTime, MaxInstructionTime) : std::max(MaxCoreSolverTime, MaxInstructionTime)), debugInstFile(
				0), debugLogBuffer(debugBufferString), isFinished(false), prefix(NULL), executionNum(0), execStatus(SUCCESS) {

	if (coreSolverTimeout)
		UseForkedCoreSolver = true;
	Solver *coreSolver = klee::createCoreSolver(CoreSolverToUse);
	if (!coreSolver) {
		llvm::errs() << "Failed to create core solver\n";
		exit(1);
	}
	Solver *solver = constructSolverChain(coreSolver, interpreterHandler->getOutputFilename(ALL_QUERIES_SMT2_FILE_NAME),
			interpreterHandler->getOutputFilename(SOLVER_QUERIES_SMT2_FILE_NAME),
			interpreterHandler->getOutputFilename(ALL_QUERIES_PC_FILE_NAME),
			interpreterHandler->getOutputFilename(SOLVER_QUERIES_PC_FILE_NAME));

	this->solver = new TimingSolver(solver, EqualitySubstitution);
	memory = new MemoryManager(&arrayCache);

	if (optionIsSet(DebugPrintInstructions, FILE_ALL) || optionIsSet(DebugPrintInstructions, FILE_COMPACT)
			|| optionIsSet(DebugPrintInstructions, FILE_SRC)) {
		std::string debug_file_name = interpreterHandler->getOutputFilename("instructions.txt");
		std::string ErrorInfo;
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 5)
		debugInstFile = new llvm::raw_fd_ostream(debug_file_name.c_str(), ErrorInfo,
				llvm::sys::fs::OpenFlags::F_Text),
#else
		debugInstFile = new llvm::raw_fd_ostream(debug_file_name.c_str(), ErrorInfo);
#endif
	}

	///@hy
	listenerService = new ListenerService(this);

}

const Module *Executor::setModule(llvm::Module *module, const ModuleOptions &opts) {
	assert(!kmodule && module && "can only register one module"); // XXX gross

	kmodule = new KModule(module);

	// Initialize the context.
#if LLVM_VERSION_CODE <= LLVM_VERSION(3, 1)
	TargetData *TD = kmodule->targetData;
#else
	DataLayout *TD = kmodule->targetData;
#endif
	Context::initialize(TD->isLittleEndian(), (Expr::Width) TD->getPointerSizeInBits());

	specialFunctionHandler = new SpecialFunctionHandler(*this);

	specialFunctionHandler->prepare();
	kmodule->prepare(opts, interpreterHandler);
	specialFunctionHandler->bind();

	if (StatsTracker::useStatistics()) {
		statsTracker = new StatsTracker(*this, interpreterHandler->getOutputFilename("assembly.ll"), userSearcherRequiresMD2U());
	}

	return module;
}

Executor::~Executor() {
	delete memory;
	delete externalDispatcher;
	if (processTree)
		delete processTree;
	if (specialFunctionHandler)
		delete specialFunctionHandler;
	if (statsTracker)
		delete statsTracker;
	delete solver;
	delete kmodule;
	while (!timers.empty()) {
		delete timers.back();
		timers.pop_back();
	}
	if (debugInstFile) {
		delete debugInstFile;
	}

	delete listenerService;
}

/***/

void Executor::initializeGlobalObject(ExecutionState &state, ObjectState *os, const Constant *c, unsigned offset) {
#if LLVM_VERSION_CODE <= LLVM_VERSION(3, 1)
	TargetData *targetData = kmodule->targetData;
#else
	DataLayout *targetData = kmodule->targetData;
#endif
	if (const ConstantVector *cp = dyn_cast<ConstantVector>(c)) {
		unsigned elementSize = targetData->getTypeStoreSize(cp->getType()->getElementType());
		for (unsigned i = 0, e = cp->getNumOperands(); i != e; ++i)
			initializeGlobalObject(state, os, cp->getOperand(i), offset + i * elementSize);
	} else if (isa<ConstantAggregateZero>(c)) {
		unsigned i, size = targetData->getTypeStoreSize(c->getType());
		for (i = 0; i < size; i++)
			os->write8(offset + i, (uint8_t) 0);
	} else if (const ConstantArray *ca = dyn_cast<ConstantArray>(c)) {
		unsigned elementSize = targetData->getTypeStoreSize(ca->getType()->getElementType());
		for (unsigned i = 0, e = ca->getNumOperands(); i != e; ++i)
			initializeGlobalObject(state, os, ca->getOperand(i), offset + i * elementSize);
	} else if (const ConstantStruct *cs = dyn_cast<ConstantStruct>(c)) {
		const StructLayout *sl = targetData->getStructLayout(cast<StructType>(cs->getType()));
		for (unsigned i = 0, e = cs->getNumOperands(); i != e; ++i)
			initializeGlobalObject(state, os, cs->getOperand(i), offset + sl->getElementOffset(i));
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 1)
	} else if (const ConstantDataSequential *cds = dyn_cast<ConstantDataSequential>(c)) {
		unsigned elementSize = targetData->getTypeStoreSize(cds->getElementType());
		for (unsigned i = 0, e = cds->getNumElements(); i != e; ++i)
			initializeGlobalObject(state, os, cds->getElementAsConstant(i), offset + i * elementSize);
#endif
	} else if (!isa<UndefValue>(c)) {
		unsigned StoreBits = targetData->getTypeStoreSizeInBits(c->getType());
		ref<ConstantExpr> C = evalConstant(c);

		// Extend the constant if necessary;
		assert(StoreBits >= C->getWidth() && "Invalid store size!");
		if (StoreBits > C->getWidth())
			C = C->ZExt(StoreBits);

		os->write(offset, C);
	}
}

MemoryObject * Executor::addExternalObject(ExecutionState &state, void *addr, unsigned size, bool isReadOnly) {
	MemoryObject *mo = memory->allocateFixed((uint64_t) (unsigned long) addr, size, 0);
	ObjectState *os = bindObjectInState(state, mo, false);
	for (unsigned i = 0; i < size; i++)
		os->write8(i, ((uint8_t*) addr)[i]);
	if (isReadOnly)
		os->setReadOnly(true);
	return mo;
}

extern void *__dso_handle __attribute__ ((__weak__));

void Executor::initializeGlobals(ExecutionState &state) {
	Module *m = kmodule->module;

	if (m->getModuleInlineAsm() != "")
		klee_warning("executable has module level assembly (ignoring)");
#if LLVM_VERSION_CODE < LLVM_VERSION(3, 3)
	assert(m->lib_begin() == m->lib_end() &&
			"XXX do not support dependent libraries");
#endif
	// represent function globals using the address of the actual llvm function
	// object. given that we use malloc to allocate memory in states this also
	// ensures that we won't conflict. we don't need to allocate a memory object
	// since reading/writing via a function pointer is unsupported anyway.
	for (Module::iterator i = m->begin(), ie = m->end(); i != ie; ++i) {
		Function *f = i;
		ref<ConstantExpr> addr(0);

		// If the symbol has external weak linkage then it is implicitly
		// not defined in this module; if it isn't resolvable then it
		// should be null.
		if (f->hasExternalWeakLinkage() && !externalDispatcher->resolveSymbol(f->getName())) {
			addr = Expr::createPointer(0);
		} else {
			addr = Expr::createPointer((unsigned long) (void*) f);
			legalFunctions.insert((uint64_t) (unsigned long) (void*) f);
		}

		globalAddresses.insert(std::make_pair(f, addr));
	}

	// Disabled, we don't want to promote use of live externals.
#ifdef HAVE_CTYPE_EXTERNALS
#ifndef WINDOWS
#ifndef DARWIN
	/* From /usr/include/errno.h: it [errno] is a per-thread variable. */
	int *errno_addr = __errno_location();
	addExternalObject(state, (void *) errno_addr, sizeof *errno_addr, false);

	/* from /usr/include/ctype.h:
	 These point into arrays of 384, so they can be indexed by any `unsigned
	 char' value [0,255]; by EOF (-1); or by any `signed char' value
	 [-128,-1).  ISO C requires that the ctype functions work for `unsigned */
	const uint16_t **addr = __ctype_b_loc();
	addExternalObject(state, const_cast<uint16_t*>(*addr - 128), 384 * sizeof **addr, true);
	addExternalObject(state, addr, sizeof(*addr), true);

	const int32_t **lower_addr = __ctype_tolower_loc();
	addExternalObject(state, const_cast<int32_t*>(*lower_addr - 128), 384 * sizeof **lower_addr, true);
	addExternalObject(state, lower_addr, sizeof(*lower_addr), true);

	const int32_t **upper_addr = __ctype_toupper_loc();
	addExternalObject(state, const_cast<int32_t*>(*upper_addr - 128), 384 * sizeof **upper_addr, true);
	addExternalObject(state, upper_addr, sizeof(*upper_addr), true);
#endif
#endif
#endif

	// allocate and initialize globals, done in two passes since we may
	// need address of a global in order to initialize some other one.

	// allocate memory objects for all globals
	for (Module::const_global_iterator i = m->global_begin(), e = m->global_end(); i != e; ++i) {
		if (i->isDeclaration()) {
			// FIXME: We have no general way of handling unknown external
			// symbols. If we really cared about making external stuff work
			// better we could support user definition, or use the EXE style
			// hack where we check the object file information.

			LLVM_TYPE_Q Type *ty = i->getType()->getElementType();
			uint64_t size = kmodule->targetData->getTypeStoreSize(ty);

			// XXX - DWD - hardcode some things until we decide how to fix.
#ifndef WINDOWS
			if (i->getName() == "_ZTVN10__cxxabiv117__class_type_infoE") {
				size = 0x2C;
			} else if (i->getName() == "_ZTVN10__cxxabiv120__si_class_type_infoE") {
				size = 0x2C;
			} else if (i->getName() == "_ZTVN10__cxxabiv121__vmi_class_type_infoE") {
				size = 0x2C;
			}
#endif

			if (size == 0) {
				llvm::errs() << "Unable to find size for global variable: " << i->getName()
						<< " (use will result in out of bounds access)\n";
			}

			MemoryObject *mo = memory->allocate(size, false, true, i);
			ObjectState *os = bindObjectInState(state, mo, false);
			globalObjects.insert(std::make_pair(i, mo));
			globalAddresses.insert(std::make_pair(i, mo->getBaseExpr()));

			// Program already running = object already initialized.  Read
			// concrete value and write it to our copy.
			if (size) {
				void *addr;
				if (i->getName() == "__dso_handle") {
					addr = &__dso_handle; // wtf ?
				} else {
					addr = externalDispatcher->resolveSymbol(i->getName());
				}
				if (!addr)
					klee_error("unable to load symbol(%s) while initializing globals.", i->getName().data());

				for (unsigned offset = 0; offset < mo->size; offset++)
					os->write8(offset, ((unsigned char*) addr)[offset]);
			}
		} else {
			LLVM_TYPE_Q Type *ty = i->getType()->getElementType();
			uint64_t size = kmodule->targetData->getTypeStoreSize(ty);
			MemoryObject *mo = memory->allocate(size, false, true, &*i);
			if (!mo)
				llvm::report_fatal_error("out of memory");
			ObjectState *os = bindObjectInState(state, mo, false);
			globalObjects.insert(std::make_pair(i, mo));
			globalAddresses.insert(std::make_pair(i, mo->getBaseExpr()));

			if (!i->hasInitializer())
				os->initializeToRandom();
		}
	}

	// link aliases to their definitions (if bound)
	for (Module::alias_iterator i = m->alias_begin(), ie = m->alias_end(); i != ie; ++i) {
		// Map the alias to its aliasee's address. This works because we have
		// addresses for everything, even undefined functions.
		globalAddresses.insert(std::make_pair(i, evalConstant(i->getAliasee())));
	}

	// once all objects are allocated, do the actual initialization
	for (Module::const_global_iterator i = m->global_begin(), e = m->global_end(); i != e; ++i) {
		if (i->hasInitializer()) {
			MemoryObject *mo = globalObjects.find(i)->second;
			const ObjectState *os = state.currentStack->addressSpace->findObject(mo);
			assert(os);
			ObjectState *wos = state.currentStack->addressSpace->getWriteable(mo, os);

			initializeGlobalObject(state, wos, i->getInitializer(), 0);
			// if(i->isConstant()) os->setReadOnly(true);
		}
	}
}

void Executor::branch(ExecutionState &state, const std::vector<ref<Expr> > &conditions, std::vector<ExecutionState*> &result) {
	TimerStatIncrementer timer(stats::forkTime);
	unsigned N = conditions.size();
	assert(N);

	if (MaxForks != ~0u && stats::forks >= MaxForks) {
		unsigned next = theRNG.getInt32() % N;
		for (unsigned i = 0; i < N; ++i) {
			if (i == next) {
				result.push_back(&state);
			} else {
				result.push_back(NULL);
			}
		}
	} else {
		stats::forks += N - 1;

		// XXX do proper balance or keep random?
		result.push_back(&state);
		for (unsigned i = 1; i < N; ++i) {
			ExecutionState *es = result[theRNG.getInt32() % i];
			ExecutionState *ns = es->branch();
			addedStates.insert(ns);
			result.push_back(ns);
			es->ptreeNode->data = 0;
			std::pair<PTree::Node*, PTree::Node*> res = processTree->split(es->ptreeNode, ns, es);
			ns->ptreeNode = res.first;
			es->ptreeNode = res.second;
		}
	}

	// If necessary redistribute seeds to match conditions, killing
	// states if necessary due to OnlyReplaySeeds (inefficient but
	// simple).

	std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it = seedMap.find(&state);
	if (it != seedMap.end()) {
		std::vector<SeedInfo> seeds = it->second;
		seedMap.erase(it);

		// Assume each seed only satisfies one condition (necessarily true
		// when conditions are mutually exclusive and their conjunction is
		// a tautology).
		for (std::vector<SeedInfo>::iterator siit = seeds.begin(), siie = seeds.end(); siit != siie; ++siit) {
			unsigned i;
			for (i = 0; i < N; ++i) {
				ref<ConstantExpr> res;
				bool success = solver->getValue(state, siit->assignment.evaluate(conditions[i]), res);
				assert(success && "FIXME: Unhandled solver failure");
				(void) success;
				if (res->isTrue())
					break;
			}

			// If we didn't find a satisfying condition randomly pick one
			// (the seed will be patched).
			if (i == N)
				i = theRNG.getInt32() % N;

			// Extra check in case we're replaying seeds with a max-fork
			if (result[i])
				seedMap[result[i]].push_back(*siit);
		}

		if (OnlyReplaySeeds) {
			for (unsigned i = 0; i < N; ++i) {
				if (result[i] && !seedMap.count(result[i])) {
					terminateState(*result[i]);
					result[i] = NULL;
				}
			}
		}
	}

	for (unsigned i = 0; i < N; ++i)
		if (result[i])
			addConstraint(*result[i], conditions[i]);
}

Executor::StatePair Executor::fork(ExecutionState &current, ref<Expr> condition, bool isInternal) {
	Solver::Validity res;
	std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it = seedMap.find(&current);
	bool isSeeding = it != seedMap.end();

	if (!isSeeding && !isa<ConstantExpr>(condition)
			&& (MaxStaticForkPct != 1. || MaxStaticSolvePct != 1. || MaxStaticCPForkPct != 1. || MaxStaticCPSolvePct != 1.)
			&& statsTracker->elapsed() > 60.) {
		StatisticManager &sm = *theStatisticManager;
		CallPathNode *cpn = current.currentStack->realStack.back().callPathNode;
		if ((MaxStaticForkPct < 1. && sm.getIndexedValue(stats::forks, sm.getIndex()) > stats::forks * MaxStaticForkPct)
				|| (MaxStaticCPForkPct < 1. && cpn && (cpn->statistics.getValue(stats::forks) > stats::forks * MaxStaticCPForkPct))
				|| (MaxStaticSolvePct < 1 && sm.getIndexedValue(stats::solverTime, sm.getIndex()) > stats::solverTime * MaxStaticSolvePct)
				|| (MaxStaticCPForkPct < 1. && cpn
						&& (cpn->statistics.getValue(stats::solverTime) > stats::solverTime * MaxStaticCPSolvePct))) {
			ref<ConstantExpr> value;
			bool success = solver->getValue(current, condition, value);
			assert(success && "FIXME: Unhandled solver failure");
			(void) success;
			addConstraint(current, EqExpr::create(value, condition));
			condition = value;
		}
	}

	double timeout = coreSolverTimeout;
	if (isSeeding)
		timeout *= it->second.size();
	solver->setTimeout(timeout);
	bool success = solver->evaluate(current, condition, res);
	solver->setTimeout(0);
	if (!success) {
		current.currentThread->pc = current.currentThread->prevPC;
		terminateStateEarly(current, "Query timed out (fork).");
		return StatePair(0, 0);
	}

	if (!isSeeding) {
		if (replayPath && !isInternal) {
			assert(replayPosition < replayPath->size() && "ran out of branches in replay path mode");
			bool branch = (*replayPath)[replayPosition++];

			if (res == Solver::True) {
				assert(branch && "hit invalid branch in replay path mode");
			} else if (res == Solver::False) {
				assert(!branch && "hit invalid branch in replay path mode");
			} else {
				// add constraints
				if (branch) {
					res = Solver::True;
					addConstraint(current, condition);
				} else {
					res = Solver::False;
					addConstraint(current, Expr::createIsZero(condition));
				}
			}
		} else if (res == Solver::Unknown) {
			assert(!replayKTest && "in replay mode, only one branch can be true.");

			if ((MaxMemoryInhibit && atMemoryLimit) || current.forkDisabled || inhibitForking
					|| (MaxForks != ~0u && stats::forks >= MaxForks)) {

				if (MaxMemoryInhibit && atMemoryLimit)
					klee_warning_once(0, "skipping fork (memory cap exceeded)");
				else if (current.forkDisabled)
					klee_warning_once(0, "skipping fork (fork disabled on current path)");
				else if (inhibitForking)
					klee_warning_once(0, "skipping fork (fork disabled globally)");
				else
					klee_warning_once(0, "skipping fork (max-forks reached)");

				TimerStatIncrementer timer(stats::forkTime);
				if (theRNG.getBool()) {
					addConstraint(current, condition);
					res = Solver::True;
				} else {
					addConstraint(current, Expr::createIsZero(condition));
					res = Solver::False;
				}
			}
		}
	}

	// Fix branch in only-replay-seed mode, if we don't have both true
	// and false seeds.
	if (isSeeding && (current.forkDisabled || OnlyReplaySeeds) && res == Solver::Unknown) {
		bool trueSeed = false, falseSeed = false;
		// Is seed extension still ok here?
		for (std::vector<SeedInfo>::iterator siit = it->second.begin(), siie = it->second.end(); siit != siie; ++siit) {
			ref<ConstantExpr> res;
			bool success = solver->getValue(current, siit->assignment.evaluate(condition), res);
			assert(success && "FIXME: Unhandled solver failure");
			(void) success;
			if (res->isTrue()) {
				trueSeed = true;
			} else {
				falseSeed = true;
			}
			if (trueSeed && falseSeed)
				break;
		}
		if (!(trueSeed && falseSeed)) {
			assert(trueSeed || falseSeed);

			res = trueSeed ? Solver::True : Solver::False;
			addConstraint(current, trueSeed ? condition : Expr::createIsZero(condition));
		}
	}

	// XXX - even if the constraint is provable one way or the other we
	// can probably benefit by adding this constraint and allowing it to
	// reduce the other constraints. For example, if we do a binary
	// search on a particular value, and then see a comparison against
	// the value it has been fixed at, we should take this as a nice
	// hint to just use the single constraint instead of all the binary
	// search ones. If that makes sense.
	if (res == Solver::True) {
		if (!isInternal) {
			if (pathWriter) {
				current.pathOS << "1";
			}
		}

		return StatePair(&current, 0);
	} else if (res == Solver::False) {
		if (!isInternal) {
			if (pathWriter) {
				current.pathOS << "0";
			}
		}

		return StatePair(0, &current);
	} else {
		TimerStatIncrementer timer(stats::forkTime);
		ExecutionState *falseState, *trueState = &current;

		++stats::forks;

		falseState = trueState->branch();
		addedStates.insert(falseState);

		if (RandomizeFork && theRNG.getBool())
			std::swap(trueState, falseState);

		if (it != seedMap.end()) {
			std::vector<SeedInfo> seeds = it->second;
			it->second.clear();
			std::vector<SeedInfo> &trueSeeds = seedMap[trueState];
			std::vector<SeedInfo> &falseSeeds = seedMap[falseState];
			for (std::vector<SeedInfo>::iterator siit = seeds.begin(), siie = seeds.end(); siit != siie; ++siit) {
				ref<ConstantExpr> res;
				bool success = solver->getValue(current, siit->assignment.evaluate(condition), res);
				assert(success && "FIXME: Unhandled solver failure");
				(void) success;
				if (res->isTrue()) {
					trueSeeds.push_back(*siit);
				} else {
					falseSeeds.push_back(*siit);
				}
			}

			bool swapInfo = false;
			if (trueSeeds.empty()) {
				if (&current == trueState)
					swapInfo = true;
				seedMap.erase(trueState);
			}
			if (falseSeeds.empty()) {
				if (&current == falseState)
					swapInfo = true;
				seedMap.erase(falseState);
			}
			if (swapInfo) {
				std::swap(trueState->coveredNew, falseState->coveredNew);
				std::swap(trueState->coveredLines, falseState->coveredLines);
			}
		}

		current.ptreeNode->data = 0;
		std::pair<PTree::Node*, PTree::Node*> res = processTree->split(current.ptreeNode, falseState, trueState);
		falseState->ptreeNode = res.first;
		trueState->ptreeNode = res.second;

		if (!isInternal) {
			if (pathWriter) {
				falseState->pathOS = pathWriter->open(current.pathOS);
				trueState->pathOS << "1";
				falseState->pathOS << "0";
			}
			if (symPathWriter) {
				falseState->symPathOS = symPathWriter->open(current.symPathOS);
				trueState->symPathOS << "1";
				falseState->symPathOS << "0";
			}
		}

		addConstraint(*trueState, condition);
		addConstraint(*falseState, Expr::createIsZero(condition));

		// Kinda gross, do we even really still want this option?
		if (MaxDepth && MaxDepth <= trueState->depth) {
			terminateStateEarly(*trueState, "max-depth exceeded.");
			terminateStateEarly(*falseState, "max-depth exceeded.");
			return StatePair(0, 0);
		}

		return StatePair(trueState, falseState);
	}
}

void Executor::addConstraint(ExecutionState &state, ref<Expr> condition) {
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
		if (!CE->isTrue())
			llvm::report_fatal_error("attempt to add invalid constraint");
		return;
	}

	// Check to see if this constraint violates seeds.
	std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it = seedMap.find(&state);
	if (it != seedMap.end()) {
		bool warn = false;
		for (std::vector<SeedInfo>::iterator siit = it->second.begin(), siie = it->second.end(); siit != siie; ++siit) {
			bool res;
			bool success = solver->mustBeFalse(state, siit->assignment.evaluate(condition), res);
			assert(success && "FIXME: Unhandled solver failure");
			(void) success;
			if (res) {
				siit->patchSeed(state, condition, solver);
				warn = true;
			}
		}
		if (warn)
			klee_warning("seeds patched for violating constraint");
	}

	state.addConstraint(condition);
	if (ivcEnabled)
		doImpliedValueConcretization(state, condition, ConstantExpr::alloc(1, Expr::Bool));
}

ref<klee::ConstantExpr> Executor::evalConstant(const Constant *c) {
	if (const llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(c)) {
		return evalConstantExpr(ce);
	} else {
		if (const ConstantInt *ci = dyn_cast<ConstantInt>(c)) {
			return ConstantExpr::alloc(ci->getValue());
		} else if (const ConstantFP *cf = dyn_cast<ConstantFP>(c)) {
			ref<Expr> result = ConstantExpr::alloc(cf->getValueAPF().bitcastToAPInt());
			result->isFloat = true;
			return cast<ConstantExpr>(result);
		} else if (const GlobalValue *gv = dyn_cast<GlobalValue>(c)) {
			return globalAddresses.find(gv)->second;
		} else if (isa<ConstantPointerNull>(c)) {
			return Expr::createPointer(0);
		} else if (isa<UndefValue>(c) || isa<ConstantAggregateZero>(c)) {
			return ConstantExpr::create(0, getWidthForLLVMType(c->getType()));
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 1)
		} else if (const ConstantDataSequential *cds = dyn_cast<ConstantDataSequential>(c)) {
			std::vector<ref<Expr> > kids;
			for (unsigned i = 0, e = cds->getNumElements(); i != e; ++i) {
				ref<Expr> kid = evalConstant(cds->getElementAsConstant(i));
				kids.push_back(kid);
			}
			ref<Expr> res = ConcatExpr::createN(kids.size(), kids.data());
			return cast<ConstantExpr>(res);
#endif
		} else if (const ConstantStruct *cs = dyn_cast<ConstantStruct>(c)) {
			const StructLayout *sl = kmodule->targetData->getStructLayout(cs->getType());
			llvm::SmallVector<ref<Expr>, 4> kids;
			for (unsigned i = cs->getNumOperands(); i != 0; --i) {
				unsigned op = i - 1;
				ref<Expr> kid = evalConstant(cs->getOperand(op));

				uint64_t thisOffset = sl->getElementOffsetInBits(op), nextOffset =
						(op == cs->getNumOperands() - 1) ? sl->getSizeInBits() : sl->getElementOffsetInBits(op + 1);
				if (nextOffset - thisOffset > kid->getWidth()) {
					uint64_t paddingWidth = nextOffset - thisOffset - kid->getWidth();
					kids.push_back(ConstantExpr::create(0, paddingWidth));
				}

				kids.push_back(kid);
			}
			ref<Expr> res = ConcatExpr::createN(kids.size(), kids.data());
			return cast<ConstantExpr>(res);
		} else if (const ConstantArray *ca = dyn_cast<ConstantArray>(c)) {
			llvm::SmallVector<ref<Expr>, 4> kids;
			for (unsigned i = ca->getNumOperands(); i != 0; --i) {
				unsigned op = i - 1;
				ref<Expr> kid = evalConstant(ca->getOperand(op));
				kids.push_back(kid);
			}
			ref<Expr> res = ConcatExpr::createN(kids.size(), kids.data());
			return cast<ConstantExpr>(res);
		} else {
			// Constant{Vector}
			llvm::report_fatal_error("invalid argument to evalConstant()");
		}
	}
}

const Cell& Executor::eval(KInstruction *ki, unsigned index, ExecutionState &state) const {
	assert(index < ki->inst->getNumOperands());
	int vnumber = ki->operands[index];
	assert(vnumber != -1 && "Invalid operand to eval(), not a value or constant!");

	// Determine if this is a constant or not.
	if (vnumber < 0) {
		unsigned index = -vnumber - 2;
		return kmodule->constantTable[index];
	} else {
		unsigned index = vnumber;
		StackFrame &sf = state.currentStack->realStack.back();
		return sf.locals[index];
	}
}

const Cell& Executor::evalCurrent(KInstruction *ki, unsigned index, ExecutionState &state) const {
	assert(index < ki->inst->getNumOperands());
	int vnumber = ki->operands[index];
	assert(vnumber != -1 && "Invalid operand to eval(), not a value or constant!");

	// Determine if this is a constant or not.
	if (vnumber < 0) {
		unsigned index = -vnumber - 2;
		return kmodule->constantTable[index];
	} else {
		unsigned index = vnumber;
		StackFrame &sf = state.currentThread->stack->realStack.back();
		return sf.locals[index];
	}
}

void Executor::ineval(KInstruction *ki, unsigned index, ExecutionState &state, ref<Expr> value) const {
	assert(index < ki->inst->getNumOperands());
	int vnumber = ki->operands[index];

	assert(vnumber != -1 && "Invalid operand to eval(), not a value or constant!");

	// Determine if this is a constant or not.
	if (vnumber < 0) {
		unsigned index = -vnumber - 2;
		kmodule->constantTable[index].value = value;
	} else {
		unsigned index = vnumber;
		StackFrame &sf = state.currentStack->realStack.back();
		sf.locals[index].value = value;
	}
}

void Executor::bindLocal(KInstruction *target, ExecutionState &state, ref<Expr> value) {
	getDestCell(state, target).value = value;
}

void Executor::bindArgument(KFunction *kf, unsigned index, ExecutionState &state, ref<Expr> value) {
	getArgumentCell(state, kf, index).value = value;
}

ref<Expr> Executor::toUnique(const ExecutionState &state, ref<Expr> &e) {
	ref<Expr> result = e;

	if (!isa<ConstantExpr>(e)) {
		ref<ConstantExpr> value;
		bool isTrue = false;

		solver->setTimeout(coreSolverTimeout);
		if (solver->getValue(state, e, value) && solver->mustBeTrue(state, EqExpr::create(e, value), isTrue) && isTrue)
			result = value;
		solver->setTimeout(0);
	}
	return result;
}

/* Concretize the given expression, and return a possible constant value.
 'reason' is just a documentation string stating the reason for concretization. */
ref<klee::ConstantExpr> Executor::toConstant(ExecutionState &state, ref<Expr> e, const char *reason) {
	e = state.constraints.simplifyExpr(e);
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
		return CE;

	ref<ConstantExpr> value;
	bool success = solver->getValue(state, e, value);
	assert(success && "FIXME: Unhandled solver failure");
	(void) success;

	std::string str;
	llvm::raw_string_ostream os(str);
	os << "silently concretizing (reason: " << reason << ") expression " << e << " to value " << value << " ("
			<< (*(state.currentThread->pc)).info->file << ":" << (*(state.currentThread->pc)).info->line << ")";

	if (AllExternalWarnings)
		klee_warning(reason, os.str().c_str());
	else
		klee_warning_once(reason, "%s", os.str().c_str());

	addConstraint(state, EqExpr::create(e, value));

	return value;
}

void Executor::executeGetValue(ExecutionState &state, ref<Expr> e, KInstruction *target) {
	e = state.constraints.simplifyExpr(e);
	std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it = seedMap.find(&state);
	if (it == seedMap.end() || isa<ConstantExpr>(e)) {
		ref<ConstantExpr> value;
		bool success = solver->getValue(state, e, value);
		assert(success && "FIXME: Unhandled solver failure");
		(void) success;
		bindLocal(target, state, value);
	} else {
		std::set<ref<Expr> > values;
		for (std::vector<SeedInfo>::iterator siit = it->second.begin(), siie = it->second.end(); siit != siie; ++siit) {
			ref<ConstantExpr> value;
			bool success = solver->getValue(state, siit->assignment.evaluate(e), value);
			assert(success && "FIXME: Unhandled solver failure");
			(void) success;
			values.insert(value);
		}

		std::vector<ref<Expr> > conditions;
		for (std::set<ref<Expr> >::iterator vit = values.begin(), vie = values.end(); vit != vie; ++vit)
			conditions.push_back(EqExpr::create(e, *vit));

		std::vector<ExecutionState*> branches;
		branch(state, conditions, branches);

		std::vector<ExecutionState*>::iterator bit = branches.begin();
		for (std::set<ref<Expr> >::iterator vit = values.begin(), vie = values.end(); vit != vie; ++vit) {
			ExecutionState *es = *bit;
			if (es)
				bindLocal(target, *es, *vit);
			++bit;
		}
	}
}

void Executor::printDebugInstructions(ExecutionState &state) {
	// check do not print
	if (DebugPrintInstructions.size() == 0)
		return;

	llvm::raw_ostream *stream = 0;
	if (optionIsSet(DebugPrintInstructions, STDERR_ALL) || optionIsSet(DebugPrintInstructions, STDERR_SRC)
			|| optionIsSet(DebugPrintInstructions, STDERR_COMPACT))
		stream = &llvm::errs();
	else
		stream = &debugLogBuffer;

	if (!optionIsSet(DebugPrintInstructions, STDERR_COMPACT) && !optionIsSet(DebugPrintInstructions, FILE_COMPACT))
		printFileLine(state, state.currentThread->pc, *stream);

	(*stream) << state.currentThread->pc->info->id;

	if (optionIsSet(DebugPrintInstructions, STDERR_ALL) || optionIsSet(DebugPrintInstructions, FILE_ALL))
		(*stream) << ":" << *(state.currentThread->pc->inst);
	(*stream) << "\n";

	if (optionIsSet(DebugPrintInstructions, FILE_ALL) || optionIsSet(DebugPrintInstructions, FILE_COMPACT)
			|| optionIsSet(DebugPrintInstructions, FILE_SRC)) {
		debugLogBuffer.flush();
		(*debugInstFile) << debugLogBuffer.str();
		debugBufferString = "";
	}
}

void Executor::stepInstruction(ExecutionState &state) {
	printDebugInstructions(state);
	if (statsTracker)
		statsTracker->stepInstruction(state);

	++stats::instructions;
	state.currentThread->prevPC = state.currentThread->pc;
	++state.currentThread->pc;

	if (stats::instructions == StopAfterNInstructions)
		haltExecution = true;
}

void Executor::executeCall(ExecutionState &state, KInstruction *ki, Function *f, std::vector<ref<Expr> > &arguments) {
	Instruction *i = ki->inst;
	if (f && f->isDeclaration()) {
		switch (f->getIntrinsicID()) {
			case Intrinsic::not_intrinsic:
				// state may be destroyed by this call, cannot touch
				callExternalFunction(state, ki, f, arguments);
				break;

				// va_arg is handled by caller and intrinsic lowering, see comment for
				// ExecutionState::varargs
			case Intrinsic::vastart: {
				StackFrame &sf = state.currentStack->realStack.back();
				assert(sf.varargs && "vastart called in function with no vararg object");

				// FIXME: This is really specific to the architecture, not the pointer
				// size. This happens to work fir x86-32 and x86-64, however.
				Expr::Width WordSize = Context::get().getPointerWidth();
				if (WordSize == Expr::Int32) {
					executeMemoryOperation(state, true, arguments[0], sf.varargs->getBaseExpr(), 0);
				} else {
					assert(WordSize == Expr::Int64 && "Unknown word size!");

					// X86-64 has quite complicated calling convention. However,
					// instead of implementing it, we can do a simple hack: just
					// make a function believe that all varargs are on stack.
					executeMemoryOperation(state, true, arguments[0], ConstantExpr::create(48, 32), 0); // gp_offset
					executeMemoryOperation(state, true, AddExpr::create(arguments[0], ConstantExpr::create(4, 64)),
							ConstantExpr::create(304, 32), 0); // fp_offset
					executeMemoryOperation(state, true, AddExpr::create(arguments[0], ConstantExpr::create(8, 64)),
							sf.varargs->getBaseExpr(), 0); // overflow_arg_area
					executeMemoryOperation(state, true, AddExpr::create(arguments[0], ConstantExpr::create(16, 64)),
							ConstantExpr::create(0, 64), 0); // reg_save_area
				}
				break;
			}
			case Intrinsic::vaend:
				// va_end is a noop for the interpreter.
				//
				// FIXME: We should validate that the target didn't do something bad
				// with vaeend, however (like call it twice).
				break;

			case Intrinsic::vacopy:
				// va_copy should have been lowered.
				//
				// FIXME: It would be nice to check for errors in the usage of this as
				// well.
			default:
				klee_error("unknown intrinsic: %s", f->getName().data());
		}

		if (InvokeInst *ii = dyn_cast<InvokeInst>(i))
			transferToBasicBlock(ii->getNormalDest(), i->getParent(), state);
	} else {
		// FIXME: I'm not really happy about this reliance on prevPC but it is ok, I
		// guess. This just done to avoid having to pass KInstIterator everywhere
		// instead of the actual instruction, since we can't make a KInstIterator
		// from just an instruction (unlike LLVM).
		KFunction *kf = kmodule->functionMap[f];
		state.currentStack->pushFrame(state.currentThread->prevPC, kf);
		state.currentThread->pc = kf->instructions;

			statsTracker->framePushed(state, &state.currentStack->realStack[state.currentStack->realStack.size() - 2]);

		// TODO: support "byval" parameter attribute
		// TODO: support zeroext, signext, sret attributes

		unsigned callingArgs = arguments.size();
		unsigned funcArgs = f->arg_size();
		if (!f->isVarArg()) {
			if (callingArgs > funcArgs) {
				klee_warning_once(f, "calling %s with extra arguments.", f->getName().data());
			} else if (callingArgs < funcArgs) {
				terminateStateOnError(state, "calling function with too few arguments", "user.err");
				return;
			}
		} else {
			Expr::Width WordSize = Context::get().getPointerWidth();

			if (callingArgs < funcArgs) {
				terminateStateOnError(state, "calling function with too few arguments", "user.err");
				return;
			}

			StackFrame &sf = state.currentStack->realStack.back();
			unsigned size = 0;
			for (unsigned i = funcArgs; i < callingArgs; i++) {
				// FIXME: This is really specific to the architecture, not the pointer
				// size. This happens to work fir x86-32 and x86-64, however.
				if (WordSize == Expr::Int32) {
					size += Expr::getMinBytesForWidth(arguments[i]->getWidth());
				} else {
					Expr::Width argWidth = arguments[i]->getWidth();
					// AMD64-ABI 3.5.7p5: Step 7. Align l->overflow_arg_area upwards to a 16
					// byte boundary if alignment needed by type exceeds 8 byte boundary.
					//
					// Alignment requirements for scalar types is the same as their size
					if (argWidth > Expr::Int64) {
						size = llvm::RoundUpToAlignment(size, 16);
					}
					size += llvm::RoundUpToAlignment(argWidth, WordSize) / 8;
				}
			}

			MemoryObject *mo = sf.varargs = memory->allocate(size, true, false, state.currentThread->prevPC->inst);
			if (!mo) {
				terminateStateOnExecError(state, "out of memory (varargs)");
				return;
			}

			if ((WordSize == Expr::Int64) && (mo->address & 15)) {
				// Both 64bit Linux/Glibc and 64bit MacOSX should align to 16 bytes.
				klee_warning_once(0, "While allocating varargs: malloc did not align to 16 bytes.");
			}

			ObjectState *os = bindObjectInState(state, mo, true);
			unsigned offset = 0;
			for (unsigned i = funcArgs; i < callingArgs; i++) {
				// FIXME: This is really specific to the architecture, not the pointer
				// size. This happens to work fir x86-32 and x86-64, however.
				if (WordSize == Expr::Int32) {
					os->write(offset, arguments[i]);
					offset += Expr::getMinBytesForWidth(arguments[i]->getWidth());
				} else {
					assert(WordSize == Expr::Int64 && "Unknown word size!");

					Expr::Width argWidth = arguments[i]->getWidth();
					if (argWidth > Expr::Int64) {
						offset = llvm::RoundUpToAlignment(offset, 16);
					}
					os->write(offset, arguments[i]);
					offset += llvm::RoundUpToAlignment(argWidth, WordSize) / 8;
				}
			}
		}

		unsigned numFormals = f->arg_size();
		for (unsigned i = 0; i < numFormals; ++i)
			bindArgument(kf, i, state, arguments[i]);
	}
}

void Executor::transferToBasicBlock(BasicBlock *dst, BasicBlock *src, ExecutionState &state) {
	// Note that in general phi nodes can reuse phi values from the same
	// block but the incoming value is the eval() result *before* the
	// execution of any phi nodes. this is pathological and doesn't
	// really seem to occur, but just in case we run the PhiCleanerPass
	// which makes sure this cannot happen and so it is safe to just
	// eval things in order. The PhiCleanerPass also makes sure that all
	// incoming blocks have the same order for each PHINode so we only
	// have to compute the index once.
	//
	// With that done we simply set an index in the state so that PHI
	// instructions know which argument to eval, set the pc, and continue.

	// XXX this lookup has to go ?
	KFunction *kf = state.currentStack->realStack.back().kf;
	unsigned entry = kf->basicBlockEntry[dst];
	state.currentThread->pc = &kf->instructions[entry];
	if (state.currentThread->pc->inst->getOpcode() == Instruction::PHI) {
		PHINode *first = static_cast<PHINode*>(state.currentThread->pc->inst);
		state.currentThread->incomingBBIndex = first->getBasicBlockIndex(src);
	}
}

void Executor::printFileLine(ExecutionState &state, KInstruction *ki, llvm::raw_ostream &debugFile) {
	const InstructionInfo &ii = *ki->info;
	if (ii.file != "")
		debugFile << "     " << ii.file << ":" << ii.line << ":";
	else
		debugFile << "     [no debug info]:";
}

/// Compute the true target of a function call, resolving LLVM and KLEE aliases
/// and bitcasts.
Function* Executor::getTargetFunction(Value *calledVal, ExecutionState &state) {
	SmallPtrSet<const GlobalValue*, 3> Visited;

	Constant *c = dyn_cast<Constant>(calledVal);
	if (!c)
		return 0;

	while (true) {
		if (GlobalValue *gv = dyn_cast<GlobalValue>(c)) {
			if (!Visited.insert(gv))
				return 0;

			std::string alias = state.getFnAlias(gv->getName());
			if (alias != "") {
				llvm::Module* currModule = kmodule->module;
				GlobalValue *old_gv = gv;
				gv = currModule->getNamedValue(alias);
				if (!gv) {
					llvm::errs() << "Function " << alias << "(), alias for " << old_gv->getName() << " not found!\n";
					assert(0 && "function alias not found");
				}
			}

			if (Function *f = dyn_cast<Function>(gv))
				return f;
			else if (GlobalAlias *ga = dyn_cast<GlobalAlias>(gv))
				c = ga->getAliasee();
			else
				return 0;
		} else if (llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(c)) {
			if (ce->getOpcode() == Instruction::BitCast)
				c = ce->getOperand(0);
			else
				return 0;
		} else
			return 0;
	}
}

/// TODO remove?
static bool isDebugIntrinsic(const Function *f, KModule *KM) {
	return false;
}

static inline const llvm::fltSemantics * fpWidthToSemantics(unsigned width) {
	switch (width) {
		case Expr::Int32:
			return &llvm::APFloat::IEEEsingle;
		case Expr::Int64:
			return &llvm::APFloat::IEEEdouble;
		case Expr::Fl80:
			return &llvm::APFloat::x87DoubleExtended;
		default:
			return 0;
	}
}

void Executor::executeInstruction(ExecutionState &state, KInstruction *ki) {
	Instruction *i = ki->inst;
	switch (i->getOpcode()) {
		// Control flow
		case Instruction::Ret: {
			ReturnInst *ri = cast<ReturnInst>(i);
			KInstIterator kcaller = state.currentStack->realStack.back().caller;
			Instruction *caller = kcaller ? kcaller->inst : 0;
//			llvm::errs() << "caller : " << caller << "\n";
			bool isVoidReturn = (ri->getNumOperands() == 0);
			ref<Expr> result = ConstantExpr::alloc(0, Expr::Bool);

			if (!isVoidReturn) {
				result = eval(ki, 0, state).value;
			}

			if (state.currentStack->realStack.size() <= 1) {
				assert(!caller && "caller set on initial stack frame");
//				llvm::errs() << "state.currentStack->realStack.size() <= 1\n";
				//recover join thread
				std::map<unsigned, std::vector<unsigned> >::iterator ji = state.joinRecord.find(state.currentThread->threadId);
				if (ji != state.joinRecord.end()) {
					for (std::vector<unsigned>::iterator bi = ji->second.begin(), be = ji->second.end(); bi != be; bi++) {
						state.swapInThread(*bi, true, false);
					}
				}
				state.swapOutThread(state.currentThread, false, false, false, true);
//				terminateStateOnExit(state);
			} else {
				state.currentStack->popFrame();

				if (statsTracker)
					statsTracker->framePopped(state);

				if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
					transferToBasicBlock(ii->getNormalDest(), caller->getParent(), state);
				} else {
					state.currentThread->pc = kcaller;
					++state.currentThread->pc;
				}

				if (!isVoidReturn) {
					LLVM_TYPE_Q Type *t = caller->getType();
					if (t != Type::getVoidTy(getGlobalContext())) {
						// may need to do coercion due to bitcasts
						Expr::Width from = result->getWidth();
						Expr::Width to = getWidthForLLVMType(t);

						if (from != to) {
							CallSite cs = (isa<InvokeInst>(caller) ? CallSite(cast<InvokeInst>(caller)) : CallSite(cast<CallInst>(caller)));

							// XXX need to check other param attrs ?
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
							bool isSExt = cs.paramHasAttr(0, llvm::Attribute::SExt);
#elif LLVM_VERSION_CODE >= LLVM_VERSION(3, 2)
							bool isSExt = cs.paramHasAttr(0, llvm::Attributes::SExt);
#else
							bool isSExt = cs.paramHasAttr(0, llvm::Attribute::SExt);
#endif
							if (isSExt) {
								result = SExtExpr::create(result, to);
							} else {
								result = ZExtExpr::create(result, to);
							}
						}

						bindLocal(kcaller, state, result);
					}
				} else {
					// We check that the return value has no users instead of
					// checking the type, since C defaults to returning int for
					// undeclared functions.
					if (!caller->use_empty()) {
						terminateStateOnExecError(state, "return void when caller expected a result");
					}
				}
			}
			break;
		}
#if LLVM_VERSION_CODE < LLVM_VERSION(3, 1)
			case Instruction::Unwind: {
				for (;;) {
					KInstruction *kcaller = state.stack.back().caller;
					state.popFrame();

					if (statsTracker)
					statsTracker->framePopped(state);

					if (state.stack.empty()) {
						terminateStateOnExecError(state, "unwind from initial stack frame");
						break;
					} else {
						Instruction *caller = kcaller->inst;
						if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
							transferToBasicBlock(ii->getUnwindDest(), caller->getParent(), state);
							break;
						}
					}
				}
				break;
			}
#endif
		case Instruction::Br: {
			BranchInst *bi = cast<BranchInst>(i);
			if (bi->isUnconditional()) {
				transferToBasicBlock(bi->getSuccessor(0), bi->getParent(), state);
			} else {
				// FIXME: Find a way that we don't have this hidden dependency.
				assert(bi->getCondition() == bi->getOperand(0) && "Wrong operand index!");
				ref<Expr> cond = eval(ki, 0, state).value;
				Executor::StatePair branches = fork(state, cond, false);

				// NOTE: There is a hidden dependency here, markBranchVisited
				// requires that we still be in the context of the branch
				// instruction (it reuses its statistic id). Should be cleaned
				// up with convenient instruction specific data.
				if (statsTracker && state.currentStack->realStack.back().kf->trackCoverage)
					statsTracker->markBranchVisited(branches.first, branches.second);

				if (branches.first)
					transferToBasicBlock(bi->getSuccessor(0), bi->getParent(), *branches.first);
				if (branches.second)
					transferToBasicBlock(bi->getSuccessor(1), bi->getParent(), *branches.second);
			}
			break;
		}
		case Instruction::Switch: {
			SwitchInst *si = cast<SwitchInst>(i);
			ref<Expr> cond = eval(ki, 0, state).value;
			BasicBlock *bb = si->getParent();

			cond = toUnique(state, cond);
			if (ConstantExpr *CE = dyn_cast<ConstantExpr>(cond)) {
				// Somewhat gross to create these all the time, but fine till we
				// switch to an internal rep.
				LLVM_TYPE_Q llvm::IntegerType *Ty = cast<IntegerType>(si->getCondition()->getType());
				ConstantInt *ci = ConstantInt::get(Ty, CE->getZExtValue());
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 1)
				unsigned index = si->findCaseValue(ci).getSuccessorIndex();
#else
				unsigned index = si->findCaseValue(ci);
#endif
				transferToBasicBlock(si->getSuccessor(index), si->getParent(), state);
			} else {
				std::map<BasicBlock*, ref<Expr> > targets;
				ref<Expr> isDefault = ConstantExpr::alloc(1, Expr::Bool);
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 1)      
				for (SwitchInst::CaseIt i = si->case_begin(), e = si->case_end(); i != e; ++i) {
					ref<Expr> value = evalConstant(i.getCaseValue());
#else
					for (unsigned i=1, cases = si->getNumCases(); i<cases; ++i) {
						ref<Expr> value = evalConstant(si->getCaseValue(i));
#endif
					ref<Expr> match = EqExpr::create(cond, value);
					isDefault = AndExpr::create(isDefault, Expr::createIsZero(match));
					bool result;
					bool success = solver->mayBeTrue(state, match, result);
					assert(success && "FIXME: Unhandled solver failure");
					(void) success;
					if (result) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 1)
						BasicBlock *caseSuccessor = i.getCaseSuccessor();
#else
						BasicBlock *caseSuccessor = si->getSuccessor(i);
#endif
						std::map<BasicBlock*, ref<Expr> >::iterator it = targets.insert(
								std::make_pair(caseSuccessor, ConstantExpr::alloc(0, Expr::Bool))).first;

						it->second = OrExpr::create(match, it->second);
					}
				}
				bool res;
				bool success = solver->mayBeTrue(state, isDefault, res);
				assert(success && "FIXME: Unhandled solver failure");
				(void) success;
				if (res)
					targets.insert(std::make_pair(si->getDefaultDest(), isDefault));

				std::vector<ref<Expr> > conditions;
				for (std::map<BasicBlock*, ref<Expr> >::iterator it = targets.begin(), ie = targets.end(); it != ie; ++it)
					conditions.push_back(it->second);

				std::vector<ExecutionState*> branches;
				branch(state, conditions, branches);

				std::vector<ExecutionState*>::iterator bit = branches.begin();
				for (std::map<BasicBlock*, ref<Expr> >::iterator it = targets.begin(), ie = targets.end(); it != ie; ++it) {
					ExecutionState *es = *bit;
					if (es)
						transferToBasicBlock(it->first, bb, *es);
					++bit;
				}
			}
			break;
		}
		case Instruction::Unreachable:
			// Note that this is not necessarily an internal bug, llvm will
			// generate unreachable instructions in cases where it knows the
			// program will crash. So it is effectively a SEGV or internal
			// error.
			terminateStateOnExecError(state, "reached \"unreachable\" instruction");
			break;

		case Instruction::Invoke:
		case Instruction::Call: {
			CallSite cs(i);

			unsigned numArgs = cs.arg_size();
			Value *fp = cs.getCalledValue();
			Function *f = getTargetFunction(fp, state);

			// Skip debug intrinsics, we can't evaluate their metadata arguments.
			if (f && isDebugIntrinsic(f, kmodule))
				break;

			if (isa<InlineAsm>(fp)) {
				terminateStateOnExecError(state, "inline assembly is unsupported");
				break;
			}
			// evaluate arguments
			std::vector<ref<Expr> > arguments;
			arguments.reserve(numArgs);

			for (unsigned j = 0; j < numArgs; ++j)
				arguments.push_back(eval(ki, j + 1, state).value);

			if (f) {
				const FunctionType *fType = dyn_cast<FunctionType>(cast<PointerType>(f->getType())->getElementType());
				const FunctionType *fpType = dyn_cast<FunctionType>(cast<PointerType>(fp->getType())->getElementType());

				// special case the call with a bitcast case
				if (fType != fpType) {
					assert(fType && fpType && "unable to get function type");

					// XXX check result coercion

					// XXX this really needs thought and validation
					unsigned i = 0;
					for (std::vector<ref<Expr> >::iterator ai = arguments.begin(), ie = arguments.end(); ai != ie; ++ai) {
						Expr::Width to, from = (*ai)->getWidth();

						if (i < fType->getNumParams()) {
							to = getWidthForLLVMType(fType->getParamType(i));

							if (from != to) {
								// XXX need to check other param attrs ?
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
								bool isSExt = cs.paramHasAttr(i + 1, llvm::Attribute::SExt);
#elif LLVM_VERSION_CODE >= LLVM_VERSION(3, 2)
								bool isSExt = cs.paramHasAttr(i+1, llvm::Attributes::SExt);
#else
								bool isSExt = cs.paramHasAttr(i+1, llvm::Attribute::SExt);
#endif
								if (isSExt) {
									arguments[i] = SExtExpr::create(arguments[i], to);
								} else {
									arguments[i] = ZExtExpr::create(arguments[i], to);
								}
							}
						}

						i++;
					}
				}

				executeCall(state, ki, f, arguments);
			} else {
				ref<Expr> v = eval(ki, 0, state).value;

				ExecutionState *free = &state;
				bool hasInvalid = false, first = true;

				/* XXX This is wasteful, no need to do a full evaluate since we
				 have already got a value. But in the end the caches should
				 handle it for us, albeit with some overhead. */
				do {
					ref<ConstantExpr> value;
					bool success = solver->getValue(*free, v, value);
					assert(success && "FIXME: Unhandled solver failure");
					(void) success;
					StatePair res = fork(*free, EqExpr::create(v, value), true);
					if (res.first) {
						uint64_t addr = value->getZExtValue();
						if (legalFunctions.count(addr)) {
							f = (Function*) addr;

							// Don't give warning on unique resolution
							if (res.second || !first)
								klee_warning_once((void*) (unsigned long) addr, "resolved symbolic function pointer to: %s",
										f->getName().data());

							executeCall(*res.first, ki, f, arguments);
						} else {
							if (!hasInvalid) {
								terminateStateOnExecError(state, "invalid function pointer");
								hasInvalid = true;
							}
						}
					}

					first = false;
					free = res.second;
				} while (free);
			}
			break;
		}
		case Instruction::PHI: {
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 0)
			ref<Expr> result = eval(ki, state.currentThread->incomingBBIndex, state).value;
#else
			ref<Expr> result = eval(ki, state.incomingBBIndex * 2, state).value;
#endif
			bindLocal(ki, state, result);
			break;
		}

			// Special instructions
		case Instruction::Select: {
			ref<Expr> cond = eval(ki, 0, state).value;
			ref<Expr> tExpr = eval(ki, 1, state).value;
			ref<Expr> fExpr = eval(ki, 2, state).value;
			ref<Expr> result = SelectExpr::create(cond, tExpr, fExpr);
			bindLocal(ki, state, result);
			break;
		}

		case Instruction::VAArg:
			terminateStateOnExecError(state, "unexpected VAArg instruction");
			break;

			// Arithmetic / logical

		case Instruction::Add: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			bindLocal(ki, state, AddExpr::create(left, right));
			break;
		}

		case Instruction::Sub: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			bindLocal(ki, state, SubExpr::create(left, right));
			break;
		}

		case Instruction::Mul: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			bindLocal(ki, state, MulExpr::create(left, right));
			break;
		}

		case Instruction::UDiv: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			ref<Expr> result = UDivExpr::create(left, right);
			bindLocal(ki, state, result);
			break;
		}

		case Instruction::SDiv: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			ref<Expr> result = SDivExpr::create(left, right);
			bindLocal(ki, state, result);
			break;
		}

		case Instruction::URem: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			ref<Expr> result = URemExpr::create(left, right);
			bindLocal(ki, state, result);
			break;
		}

		case Instruction::SRem: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			ref<Expr> result = SRemExpr::create(left, right);
			bindLocal(ki, state, result);
			break;
		}

		case Instruction::And: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			ref<Expr> result = AndExpr::create(left, right);
			bindLocal(ki, state, result);
			break;
		}

		case Instruction::Or: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			ref<Expr> result = OrExpr::create(left, right);
			bindLocal(ki, state, result);
			break;
		}

		case Instruction::Xor: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			ref<Expr> result = XorExpr::create(left, right);
			bindLocal(ki, state, result);
			break;
		}

		case Instruction::Shl: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			ref<Expr> result = ShlExpr::create(left, right);
			bindLocal(ki, state, result);
			break;
		}

		case Instruction::LShr: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			ref<Expr> result = LShrExpr::create(left, right);
			bindLocal(ki, state, result);
			break;
		}

		case Instruction::AShr: {
			ref<Expr> left = eval(ki, 0, state).value;
			ref<Expr> right = eval(ki, 1, state).value;
			ref<Expr> result = AShrExpr::create(left, right);
			bindLocal(ki, state, result);
			break;
		}

			// Compare

		case Instruction::ICmp: {
			CmpInst *ci = cast<CmpInst>(i);
			ICmpInst *ii = cast<ICmpInst>(ci);

			switch (ii->getPredicate()) {
				case ICmpInst::ICMP_EQ: {
					ref<Expr> left = eval(ki, 0, state).value;
					ref<Expr> right = eval(ki, 1, state).value;
					ref<Expr> result = EqExpr::create(left, right);
					bindLocal(ki, state, result);
					break;
				}

				case ICmpInst::ICMP_NE: {
					ref<Expr> left = eval(ki, 0, state).value;
					ref<Expr> right = eval(ki, 1, state).value;
					ref<Expr> result = NeExpr::create(left, right);
					bindLocal(ki, state, result);
					break;
				}

				case ICmpInst::ICMP_UGT: {
					ref<Expr> left = eval(ki, 0, state).value;
					ref<Expr> right = eval(ki, 1, state).value;
					ref<Expr> result = UgtExpr::create(left, right);
					bindLocal(ki, state, result);
					break;
				}

				case ICmpInst::ICMP_UGE: {
					ref<Expr> left = eval(ki, 0, state).value;
					ref<Expr> right = eval(ki, 1, state).value;
					ref<Expr> result = UgeExpr::create(left, right);
					bindLocal(ki, state, result);
					break;
				}

				case ICmpInst::ICMP_ULT: {
					ref<Expr> left = eval(ki, 0, state).value;
					ref<Expr> right = eval(ki, 1, state).value;
					ref<Expr> result = UltExpr::create(left, right);
					bindLocal(ki, state, result);
					break;
				}

				case ICmpInst::ICMP_ULE: {
					ref<Expr> left = eval(ki, 0, state).value;
					ref<Expr> right = eval(ki, 1, state).value;
					ref<Expr> result = UleExpr::create(left, right);
					bindLocal(ki, state, result);
					break;
				}

				case ICmpInst::ICMP_SGT: {
					ref<Expr> left = eval(ki, 0, state).value;
					ref<Expr> right = eval(ki, 1, state).value;
					ref<Expr> result = SgtExpr::create(left, right);
					bindLocal(ki, state, result);
					break;
				}

				case ICmpInst::ICMP_SGE: {
					ref<Expr> left = eval(ki, 0, state).value;
					ref<Expr> right = eval(ki, 1, state).value;
					ref<Expr> result = SgeExpr::create(left, right);
					bindLocal(ki, state, result);
					break;
				}

				case ICmpInst::ICMP_SLT: {
					ref<Expr> left = eval(ki, 0, state).value;
					ref<Expr> right = eval(ki, 1, state).value;
					ref<Expr> result = SltExpr::create(left, right);
					bindLocal(ki, state, result);
					break;
				}

				case ICmpInst::ICMP_SLE: {
					ref<Expr> left = eval(ki, 0, state).value;
					ref<Expr> right = eval(ki, 1, state).value;
					ref<Expr> result = SleExpr::create(left, right);
					bindLocal(ki, state, result);
					break;
				}

				default:
					terminateStateOnExecError(state, "invalid ICmp predicate");
			}
			break;
		}

			// Memory instructions...
		case Instruction::Alloca: {
			AllocaInst *ai = cast<AllocaInst>(i);
			unsigned elementSize = kmodule->targetData->getTypeStoreSize(ai->getAllocatedType());
			ref<Expr> size = Expr::createPointer(elementSize);
			if (ai->isArrayAllocation()) {
				ref<Expr> count = eval(ki, 0, state).value;
				count = Expr::createZExtToPointerWidth(count);
				size = MulExpr::create(size, count);
			}
			executeAlloc(state, size, true, ki);
			break;
		}

		case Instruction::Load: {
			ref<Expr> base = eval(ki, 0, state).value;
#if DEBUG_RUNTIME
			llvm::errs() << "Load base : " << base << "\n";
#endif
			executeMemoryOperation(state, false, base, 0, ki);
			break;
		}
		case Instruction::Store: {
			ref<Expr> base = eval(ki, 1, state).value;
			ref<Expr> value = eval(ki, 0, state).value;
#if DEBUG_RUNTIME
			llvm::errs() << "Store base : " << base << "\n";
			llvm::errs() << "Store value : " << value << "\n";
#endif
			executeMemoryOperation(state, true, base, value, 0);
			break;
		}

		case Instruction::GetElementPtr: {
			KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);
			ref<Expr> base = eval(ki, 0, state).value;
//			llvm::errs() << "kgepi->base : ";
//			base->dump();
			for (std::vector<std::pair<unsigned, uint64_t> >::iterator it = kgepi->indices.begin(), ie = kgepi->indices.end(); it != ie;
					++it) {
				uint64_t elementSize = it->second;
				ref<Expr> index = eval(ki, it->first, state).value;
//				llvm::errs() << "kgepi->index : ";
//				index->dump();
				base = AddExpr::create(base, MulExpr::create(Expr::createSExtToPointerWidth(index), Expr::createPointer(elementSize)));
			}
			if (kgepi->offset){
				base = AddExpr::create(base, Expr::createPointer(kgepi->offset));
			}
			bindLocal(ki, state, base);
			break;
		}

			// Conversion
		case Instruction::Trunc: {
			CastInst *ci = cast<CastInst>(i);
			ref<Expr> result = ExtractExpr::create(eval(ki, 0, state).value, 0, getWidthForLLVMType(ci->getType()));
			bindLocal(ki, state, result);
			break;
		}
		case Instruction::ZExt: {
			CastInst *ci = cast<CastInst>(i);
			ref<Expr> result = ZExtExpr::create(eval(ki, 0, state).value, getWidthForLLVMType(ci->getType()));
			bindLocal(ki, state, result);
			break;
		}
		case Instruction::SExt: {
			CastInst *ci = cast<CastInst>(i);
			ref<Expr> result = SExtExpr::create(eval(ki, 0, state).value, getWidthForLLVMType(ci->getType()));
			bindLocal(ki, state, result);
			break;
		}

		case Instruction::IntToPtr: {
			CastInst *ci = cast<CastInst>(i);
			Expr::Width pType = getWidthForLLVMType(ci->getType());
			ref<Expr> arg = eval(ki, 0, state).value;
			bindLocal(ki, state, ZExtExpr::create(arg, pType));
			break;
		}
		case Instruction::PtrToInt: {
			CastInst *ci = cast<CastInst>(i);
			Expr::Width iType = getWidthForLLVMType(ci->getType());
			ref<Expr> arg = eval(ki, 0, state).value;
			bindLocal(ki, state, ZExtExpr::create(arg, iType));
			break;
		}

		case Instruction::BitCast: {
			ref<Expr> result = eval(ki, 0, state).value;
			bindLocal(ki, state, result);
//			llvm::errs() << "BitCast : ";
//			result->dump();
			break;
		}

			// Floating point instructions

		case Instruction::FAdd: {
			ref<Expr> originLeft = eval(ki, 0, state).value;
			ref<Expr> originRight = eval(ki, 1, state).value;

			ConstantExpr *leftCE = dyn_cast<ConstantExpr>(originLeft);
			ConstantExpr *rightCE = dyn_cast<ConstantExpr>(originRight);
			if (leftCE != NULL && rightCE != NULL) {
				ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value, "floating point");
				ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value, "floating point");
				if (!fpWidthToSemantics(left->getWidth()) || !fpWidthToSemantics(right->getWidth()))
					return terminateStateOnExecError(state, "Unsupported FAdd operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
				llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
				Res.add(APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()), APFloat::rmNearestTiesToEven);
#else
				llvm::APFloat Res(left->getAPValue());
				Res.add(APFloat(right->getAPValue()), APFloat::rmNearestTiesToEven);
#endif
				ref<Expr> result = ConstantExpr::alloc(Res.bitcastToAPInt());
				result->isFloat = true;
				bindLocal(ki, state, result);
			} else {
				ref<Expr> res = AddExpr::create(originLeft, originRight);
				res->isFloat = true;
				bindLocal(ki, state, res);
			}
			break;
		}

		case Instruction::FSub: {
			ref<Expr> originLeft = eval(ki, 0, state).value;
			ref<Expr> originRight = eval(ki, 1, state).value;

			ConstantExpr *leftCE = dyn_cast<ConstantExpr>(originLeft);
			ConstantExpr *rightCE = dyn_cast<ConstantExpr>(originRight);
			if (leftCE != NULL && rightCE != NULL) {
				ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value, "floating point");
				ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value, "floating point");
				if (!fpWidthToSemantics(left->getWidth()) || !fpWidthToSemantics(right->getWidth()))
					return terminateStateOnExecError(state, "Unsupported FSub operation");
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
				llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
				Res.subtract(APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()), APFloat::rmNearestTiesToEven);
#else
				llvm::APFloat Res(left->getAPValue());
				Res.subtract(APFloat(right->getAPValue()), APFloat::rmNearestTiesToEven);
#endif
				ref<Expr> result = ConstantExpr::alloc(Res.bitcastToAPInt());
				result->isFloat = true;
				bindLocal(ki, state, result);
			} else {
				originLeft->isFloat = true;
				originRight->isFloat = true;
				ref<Expr> res = SubExpr::create(originLeft, originRight);
				res->isFloat = true;
				bindLocal(ki, state, res);
			}
			break;
		}

		case Instruction::FMul: {
			ref<Expr> originLeft = eval(ki, 0, state).value;
			ref<Expr> originRight = eval(ki, 1, state).value;

			ConstantExpr *leftCE = dyn_cast<ConstantExpr>(originLeft);
			ConstantExpr *rightCE = dyn_cast<ConstantExpr>(originRight);
			if (leftCE != NULL && rightCE != NULL) {
				ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value, "floating point");
				ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value, "floating point");
				if (!fpWidthToSemantics(left->getWidth()) || !fpWidthToSemantics(right->getWidth()))
					return terminateStateOnExecError(state, "Unsupported FMul operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
				llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
				Res.multiply(APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()), APFloat::rmNearestTiesToEven);
#else
				llvm::APFloat Res(left->getAPValue());
				Res.multiply(APFloat(right->getAPValue()), APFloat::rmNearestTiesToEven);
#endif
				ref<Expr> result = ConstantExpr::alloc(Res.bitcastToAPInt());
				result->isFloat = true;
				bindLocal(ki, state, result);
//    bindLocal(ki, state.currentStack, ConstantExpr::alloc(Res.bitcastToAPInt()));
			} else {
				originLeft->isFloat = true;
				originRight->isFloat = true;
//			cerr << "MulExpr : "<< originLeft << " * "<< originRight << "\n";
				ref<Expr> res = MulExpr::create(originLeft, originRight);
//			cerr << "MulExpr : "<< res << "\n";
				res->isFloat = true;
				bindLocal(ki, state, res);
			}
			break;
		}

		case Instruction::FDiv: {
			ref<Expr> originLeft = eval(ki, 0, state).value;
			ref<Expr> originRight = eval(ki, 1, state).value;

			ConstantExpr *leftCE = dyn_cast<ConstantExpr>(originLeft);
			ConstantExpr *rightCE = dyn_cast<ConstantExpr>(originRight);
			if (leftCE != NULL && rightCE != NULL) {
				ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value, "floating point");
				ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value, "floating point");
				if (!fpWidthToSemantics(left->getWidth()) || !fpWidthToSemantics(right->getWidth()))
					return terminateStateOnExecError(state, "Unsupported FDiv operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
				llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
				Res.divide(APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()), APFloat::rmNearestTiesToEven);
#else
				llvm::APFloat Res(left->getAPValue());
				Res.divide(APFloat(right->getAPValue()), APFloat::rmNearestTiesToEven);
#endif
				ref<Expr> result = ConstantExpr::alloc(Res.bitcastToAPInt());
				result->isFloat = true;
				bindLocal(ki, state, result);
//    bindLocal(ki, state.currentStack, ConstantExpr::alloc(Res.bitcastToAPInt()));
			} else {
				originLeft->isFloat = true;
				originRight->isFloat = true;
				ref<Expr> result = SDivExpr::create(originLeft, originRight);
				result->isFloat = true;
				bindLocal(ki, state, result);
			}
			break;
		}

		case Instruction::FRem: {
			ref<Expr> originLeft = eval(ki, 0, state).value;
			ref<Expr> originRight = eval(ki, 1, state).value;

			ConstantExpr *leftCE = dyn_cast<ConstantExpr>(originLeft);
			ConstantExpr *rightCE = dyn_cast<ConstantExpr>(originRight);
			if (leftCE != NULL && rightCE != NULL) {
				ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value, "floating point");
				ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value, "floating point");
				if (!fpWidthToSemantics(left->getWidth()) || !fpWidthToSemantics(right->getWidth()))
					return terminateStateOnExecError(state, "Unsupported FRem operation");
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
				llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
				Res.remainder(APFloat(*fpWidthToSemantics(right->getWidth()), right->getAPValue()));
#else
				llvm::APFloat Res(left->getAPValue());
				Res.mod(APFloat(right->getAPValue()), APFloat::rmNearestTiesToEven);
#endif
				ref<Expr> result = ConstantExpr::alloc(Res.bitcastToAPInt());
				result->isFloat = true;
				bindLocal(ki, state, result);
//    bindLocal(ki, state.currentStack, ConstantExpr::alloc(Res.bitcastToAPInt()));
			} else {
				ref<Expr> result = SRemExpr::create(originLeft, originRight);
				result->isFloat = true;
				bindLocal(ki, state, result);
			}
			break;
		}

		case Instruction::FPTrunc: {

			FPTruncInst *fi = cast<FPTruncInst>(i);
			ref<Expr> srcExpr = eval(ki, 0, state).value;
			ConstantExpr *srcCons = dyn_cast<ConstantExpr>(srcExpr);
			if (srcCons != NULL) {
//	  FPTruncInst *fi = cast<FPTruncInst>(i);
				Expr::Width resultType = getWidthForLLVMType(fi->getType());
				ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value, "floating point");
				if (!fpWidthToSemantics(arg->getWidth()) || resultType > arg->getWidth())
					return terminateStateOnExecError(state, "Unsupported FPTrunc operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
				llvm::APFloat Res(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());
#else
				llvm::APFloat Res(arg->getAPValue());
#endif
				bool losesInfo = false;
				Res.convert(*fpWidthToSemantics(resultType), llvm::APFloat::rmNearestTiesToEven, &losesInfo);
				ref<Expr> result = ConstantExpr::alloc(Res);
				result->isFloat = true;
				bindLocal(ki, state, result);
//    bindLocal(ki, state.currentStack, ConstantExpr::alloc(Res));
			} else {
				ref<Expr> result = ExtractExpr::create(eval(ki, 0, state).value, 0, getWidthForLLVMType(fi->getType()));
				result->isFloat = true;
				bindLocal(ki, state, result);
			}
			break;
		}

		case Instruction::FPExt: {
			FPExtInst *fi = cast<FPExtInst>(i);
			ref<Expr> srcExpr = eval(ki, 0, state).value;
			ConstantExpr *srcCons = dyn_cast<ConstantExpr>(srcExpr);
			if (srcCons != NULL) {
//    FPExtInst *fi = cast<FPExtInst>(i);
				Expr::Width resultType = getWidthForLLVMType(fi->getType());
				ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value, "floating point");
				if (!fpWidthToSemantics(arg->getWidth()) || arg->getWidth() > resultType)
					return terminateStateOnExecError(state, "Unsupported FPExt operation");
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
				llvm::APFloat Res(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());
#else
				llvm::APFloat Res(arg->getAPValue());
#endif
				bool losesInfo = false;
				Res.convert(*fpWidthToSemantics(resultType), llvm::APFloat::rmNearestTiesToEven, &losesInfo);
				ref<Expr> result = ConstantExpr::alloc(Res);
				result->isFloat = true;
				bindLocal(ki, state, result);
			} else {
				ref<Expr> result = SExtExpr::create(eval(ki, 0, state).value, getWidthForLLVMType(fi->getType()));
				result->isFloat = true;
				bindLocal(ki, state, result);
			}
			break;
		}

		case Instruction::FPToUI: {
			FPToUIInst *fi = cast<FPToUIInst>(i);
			ref<Expr> srcExpr = eval(ki, 0, state).value;
			ConstantExpr *srcCons = dyn_cast<ConstantExpr>(srcExpr);
			if (srcCons != NULL) {
//    FPToUIInst *fi = cast<FPToUIInst>(i);
				Expr::Width resultType = getWidthForLLVMType(fi->getType());
				ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value, "floating point");
				if (!fpWidthToSemantics(arg->getWidth()) || resultType > 64)
					return terminateStateOnExecError(state, "Unsupported FPToUI operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
				llvm::APFloat Arg(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());
#else
				llvm::APFloat Arg(arg->getAPValue());
#endif
				uint64_t value = 0;
				bool isExact = true;
				Arg.convertToInteger(&value, resultType, false, llvm::APFloat::rmTowardZero, &isExact);
				bindLocal(ki, state, ConstantExpr::alloc(value, resultType));
			} else {
				ref<Expr> result = ExtractExpr::alloc(eval(ki, 0, state).value, 0, getWidthForLLVMType(fi->getType()));
				result->isFloat = false;
				bindLocal(ki, state, result);
			}
			break;
		}

		case Instruction::FPToSI: {

			FPToSIInst *fi = cast<FPToSIInst>(i);
			ref<Expr> srcExpr = eval(ki, 0, state).value;
			ConstantExpr *srcCons = dyn_cast<ConstantExpr>(srcExpr);
			if (srcCons != NULL) {
//	  FPToSIInst *fi = cast<FPToSIInst>(i);
				Expr::Width resultType = getWidthForLLVMType(fi->getType());
				ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value, "floating point");
				if (!fpWidthToSemantics(arg->getWidth()) || resultType > 64)
					return terminateStateOnExecError(state, "Unsupported FPToSI operation");
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
				llvm::APFloat Arg(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());
#else
				llvm::APFloat Arg(arg->getAPValue());

#endif
				uint64_t value = 0;
				bool isExact = true;
				Arg.convertToInteger(&value, resultType, true, llvm::APFloat::rmTowardZero, &isExact);
				bindLocal(ki, state, ConstantExpr::alloc(value, resultType));
			} else {
				ref<Expr> result = ExtractExpr::alloc(eval(ki, 0, state).value, 0, getWidthForLLVMType(fi->getType()));
				result->isFloat = false;
//			llvm::errs() << "fptosi in exe ";
//			llvm::errs() << result->getKind() << "\n";
//			result->dump();
				bindLocal(ki, state, result);
			}
			break;
		}

		case Instruction::UIToFP: {
			UIToFPInst *fi = cast<UIToFPInst>(i);
			ref<Expr> srcExpr = eval(ki, 0, state).value;
			ConstantExpr *srcCons = dyn_cast<ConstantExpr>(srcExpr);
			if (srcCons != NULL) {
//    UIToFPInst *fi = cast<UIToFPInst>(i);
				Expr::Width resultType = getWidthForLLVMType(fi->getType());
				ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value, "floating point");
				const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
				if (!semantics)
					return terminateStateOnExecError(state, "Unsupported UIToFP operation");
				llvm::APFloat f(*semantics, 0);
				f.convertFromAPInt(arg->getAPValue(), false, llvm::APFloat::rmNearestTiesToEven);

				ref<Expr> result = ConstantExpr::alloc(f);
				result->isFloat = true;
				bindLocal(ki, state, result);
			} else {
				ref<Expr> result = SExtExpr::alloc(eval(ki, 0, state).value, getWidthForLLVMType(fi->getType()));
				result->isFloat = true;
				bindLocal(ki, state, result);
			}
			break;
		}

		case Instruction::SIToFP: {
			SIToFPInst *fi = cast<SIToFPInst>(i);

			ref<Expr> srcExpr = eval(ki, 0, state).value;
			ConstantExpr *srcCons = dyn_cast<ConstantExpr>(srcExpr);
			if (srcCons != NULL) {
//	  SIToFPInst *fi = cast<SIToFPInst>(i);
				Expr::Width resultType = getWidthForLLVMType(fi->getType());
				ref<ConstantExpr> arg = toConstant(state, eval(ki, 0, state).value, "floating point");
				const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
				if (!semantics)
					return terminateStateOnExecError(state, "Unsupported SIToFP operation");
				llvm::APFloat f(*semantics, 0);
				f.convertFromAPInt(arg->getAPValue(), true, llvm::APFloat::rmNearestTiesToEven);

				ref<Expr> result = ConstantExpr::alloc(f);
				result->isFloat = true;
				bindLocal(ki, state, result);
			} else {
				ref<Expr> result = SExtExpr::alloc(eval(ki, 0, state).value, getWidthForLLVMType(fi->getType()));
				result->isFloat = true;
				bindLocal(ki, state, result);
			}
			break;
		}

		case Instruction::FCmp: {
			FCmpInst *fi = cast<FCmpInst>(i);
			ref<Expr> originLeft = eval(ki, 0, state).value;
			ref<Expr> originRight = eval(ki, 1, state).value;
			ConstantExpr *leftCE = dyn_cast<ConstantExpr>(originLeft);
			ConstantExpr *rightCE = dyn_cast<ConstantExpr>(originRight);
			if (leftCE != NULL && rightCE != NULL) {
//    FCmpInst *fi = cast<FCmpInst>(i);
				ref<ConstantExpr> left = toConstant(state, eval(ki, 0, state).value, "floating point");
				ref<ConstantExpr> right = toConstant(state, eval(ki, 1, state).value, "floating point");
				if (!fpWidthToSemantics(left->getWidth()) || !fpWidthToSemantics(right->getWidth()))
					return terminateStateOnExecError(state, "Unsupported FCmp operation");

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
				APFloat LHS(*fpWidthToSemantics(left->getWidth()), left->getAPValue());
				APFloat RHS(*fpWidthToSemantics(right->getWidth()), right->getAPValue());
#else
				APFloat LHS(left->getAPValue());
				APFloat RHS(right->getAPValue());
#endif
				APFloat::cmpResult CmpRes = LHS.compare(RHS);

				bool Result = false;
				switch (fi->getPredicate()) {
					// Predicates which only care about whether or not the operands are NaNs.
					case FCmpInst::FCMP_ORD:
						Result = CmpRes != APFloat::cmpUnordered;
						break;

					case FCmpInst::FCMP_UNO:
						Result = CmpRes == APFloat::cmpUnordered;
						break;

						// Ordered comparisons return false if either operand is NaN.  Unordered
						// comparisons return true if either operand is NaN.
					case FCmpInst::FCMP_UEQ:
						if (CmpRes == APFloat::cmpUnordered) {
							Result = true;
							break;
						}
					case FCmpInst::FCMP_OEQ:
						Result = CmpRes == APFloat::cmpEqual;
						break;

					case FCmpInst::FCMP_UGT:
						if (CmpRes == APFloat::cmpUnordered) {
							Result = true;
							break;
						}
					case FCmpInst::FCMP_OGT:
						Result = CmpRes == APFloat::cmpGreaterThan;
						break;

					case FCmpInst::FCMP_UGE:
						if (CmpRes == APFloat::cmpUnordered) {
							Result = true;
							break;
						}
					case FCmpInst::FCMP_OGE:
						Result = CmpRes == APFloat::cmpGreaterThan || CmpRes == APFloat::cmpEqual;
						break;

					case FCmpInst::FCMP_ULT:
						if (CmpRes == APFloat::cmpUnordered) {
							Result = true;
							break;
						}
					case FCmpInst::FCMP_OLT:
						Result = CmpRes == APFloat::cmpLessThan;
						break;

					case FCmpInst::FCMP_ULE:
						if (CmpRes == APFloat::cmpUnordered) {
							Result = true;
							break;
						}
					case FCmpInst::FCMP_OLE:
						Result = CmpRes == APFloat::cmpLessThan || CmpRes == APFloat::cmpEqual;
						break;

					case FCmpInst::FCMP_UNE:
						Result = CmpRes == APFloat::cmpUnordered || CmpRes != APFloat::cmpEqual;
						break;
					case FCmpInst::FCMP_ONE:
						Result = CmpRes != APFloat::cmpUnordered && CmpRes != APFloat::cmpEqual;
						break;

					default:
						assert(0 && "Invalid FCMP predicate!");
					case FCmpInst::FCMP_FALSE:
						Result = false;
						break;
					case FCmpInst::FCMP_TRUE:
						Result = true;
						break;
				}
				bindLocal(ki, state, ConstantExpr::alloc(Result, Expr::Bool));
			} else {
				switch (fi->getPredicate()) {
					case FCmpInst::FCMP_ORD:
						break;
					case FCmpInst::FCMP_UNO:
						break;
					case FCmpInst::FCMP_UEQ:
						bindLocal(ki, state, EqExpr::alloc(originLeft, originRight));
						break;
					case FCmpInst::FCMP_OEQ:
						bindLocal(ki, state, EqExpr::alloc(originLeft, originRight));
						break;
					case FCmpInst::FCMP_UGT:
						bindLocal(ki, state, SltExpr::alloc(originRight, originLeft));
						break;
					case FCmpInst::FCMP_OGT:
						bindLocal(ki, state, SltExpr::alloc(originRight, originLeft));
						break;
					case FCmpInst::FCMP_UGE:
						bindLocal(ki, state, SleExpr::alloc(originRight, originLeft));
						break;
					case FCmpInst::FCMP_OGE:
						bindLocal(ki, state, SleExpr::alloc(originRight, originLeft));
						break;
					case FCmpInst::FCMP_ULT:
						bindLocal(ki, state, SltExpr::alloc(originLeft, originRight));
						break;
					case FCmpInst::FCMP_OLT:
						bindLocal(ki, state, SltExpr::alloc(originLeft, originRight));
						break;
					case FCmpInst::FCMP_ULE:
						bindLocal(ki, state, SleExpr::alloc(originLeft, originRight));
						break;
					case FCmpInst::FCMP_OLE:
						bindLocal(ki, state, SleExpr::alloc(originLeft, originRight));
						break;
					case FCmpInst::FCMP_UNE:
						bindLocal(ki, state, NeExpr::alloc(originLeft, originRight));
						break;
					case FCmpInst::FCMP_ONE:
						bindLocal(ki, state, NeExpr::alloc(originLeft, originRight));
						break;
					case FCmpInst::FCMP_FALSE:
						bindLocal(ki, state, ConstantExpr::alloc(0, 1));
					case FCmpInst::FCMP_TRUE:
						bindLocal(ki, state, ConstantExpr::alloc(1, 1));
						break;
					default:
						assert(0 && "Invalid FCMP predicate!");
						break;
				}
			}
			break;
		}
		case Instruction::InsertValue: {
			KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);

			ref<Expr> agg = eval(ki, 0, state).value;
			ref<Expr> val = eval(ki, 1, state).value;

			ref<Expr> l = NULL, r = NULL;
			unsigned lOffset = kgepi->offset * 8, rOffset = kgepi->offset * 8 + val->getWidth();

			if (lOffset > 0)
				l = ExtractExpr::create(agg, 0, lOffset);
			if (rOffset < agg->getWidth())
				r = ExtractExpr::create(agg, rOffset, agg->getWidth() - rOffset);

			ref<Expr> result;
			if (!l.isNull() && !r.isNull())
				result = ConcatExpr::create(r, ConcatExpr::create(val, l));
			else if (!l.isNull())
				result = ConcatExpr::create(val, l);
			else if (!r.isNull())
				result = ConcatExpr::create(r, val);
			else
				result = val;

			bindLocal(ki, state, result);
			break;
		}
		case Instruction::ExtractValue: {
			KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);

			ref<Expr> agg = eval(ki, 0, state).value;

			ref<Expr> result = ExtractExpr::create(agg, kgepi->offset * 8, getWidthForLLVMType(i->getType()));

			bindLocal(ki, state, result);
			break;
		}

			// Other instructions...
			// Unhandled
		case Instruction::ExtractElement:
		case Instruction::InsertElement:
		case Instruction::ShuffleVector:
			terminateStateOnError(state, "XXX vector instructions unhandled", "xxx.err");
			break;

		default:
			terminateStateOnExecError(state, "illegal instruction");
			break;
	}
}

void Executor::updateStates(ExecutionState *current) {
	if (searcher) {
		searcher->update(current, addedStates, removedStates);
	}

	states.insert(addedStates.begin(), addedStates.end());
	addedStates.clear();

	for (std::set<ExecutionState*>::iterator it = removedStates.begin(), ie = removedStates.end(); it != ie; ++it) {
		ExecutionState *es = *it;
		std::set<ExecutionState*>::iterator it2 = states.find(es);
		assert(it2 != states.end());
		states.erase(it2);
		std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it3 = seedMap.find(es);
		if (it3 != seedMap.end())
			seedMap.erase(it3);
		processTree->remove(es->ptreeNode);
		delete es;
	}
	removedStates.clear();
}

template<typename TypeIt>
void Executor::computeOffsets(KGEPInstruction *kgepi, TypeIt ib, TypeIt ie) {
	ref<ConstantExpr> constantOffset = ConstantExpr::alloc(0, Context::get().getPointerWidth());
	uint64_t index = 1;
	for (TypeIt ii = ib; ii != ie; ++ii) {
		if (LLVM_TYPE_Q StructType *st = dyn_cast<StructType>(*ii)) {
			const StructLayout *sl = kmodule->targetData->getStructLayout(st);
			const ConstantInt *ci = cast<ConstantInt>(ii.getOperand());
			uint64_t addend = sl->getElementOffset((unsigned) ci->getZExtValue());
			constantOffset = constantOffset->Add(ConstantExpr::alloc(addend, Context::get().getPointerWidth()));
		} else {
			const SequentialType *set = cast<SequentialType>(*ii);
			uint64_t elementSize = kmodule->targetData->getTypeStoreSize(set->getElementType());
			Value *operand = ii.getOperand();
			if (Constant *c = dyn_cast<Constant>(operand)) {
				ref<ConstantExpr> index = evalConstant(c)->SExt(Context::get().getPointerWidth());
				ref<ConstantExpr> addend = index->Mul(ConstantExpr::alloc(elementSize, Context::get().getPointerWidth()));
				constantOffset = constantOffset->Add(addend);
			} else {
				kgepi->indices.push_back(std::make_pair(index, elementSize));
			}
		}
		index++;
	}
	kgepi->offset = constantOffset->getZExtValue();
}

void Executor::bindInstructionConstants(KInstruction *KI) {
	KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(KI);

	if (GetElementPtrInst *gepi = dyn_cast<GetElementPtrInst>(KI->inst)) {
		computeOffsets(kgepi, gep_type_begin(gepi), gep_type_end(gepi));
	} else if (InsertValueInst *ivi = dyn_cast<InsertValueInst>(KI->inst)) {
		computeOffsets(kgepi, iv_type_begin(ivi), iv_type_end(ivi));
		assert(kgepi->indices.empty() && "InsertValue constant offset expected");
	} else if (ExtractValueInst *evi = dyn_cast<ExtractValueInst>(KI->inst)) {
		computeOffsets(kgepi, ev_type_begin(evi), ev_type_end(evi));
		assert(kgepi->indices.empty() && "ExtractValue constant offset expected");
	}
}

void Executor::bindModuleConstants() {
	// avoid multiple assignment of getElementPtr's indices
	// add by ylc
	if (!hasInitialized) {
		hasInitialized = true;
		for (std::vector<KFunction*>::iterator it = kmodule->functions.begin(), ie = kmodule->functions.end(); it != ie; ++it) {
			KFunction *kf = *it;
			for (unsigned i = 0; i < kf->numInstructions; ++i)
				bindInstructionConstants(kf->instructions[i]);
		}
	}

	kmodule->constantTable = new Cell[kmodule->constants.size()];
	for (unsigned i = 0; i < kmodule->constants.size(); ++i) {
		Cell &c = kmodule->constantTable[i];
		c.value = evalConstant(kmodule->constants[i]);
	}
}

void Executor::checkMemoryUsage() {
	if (MaxMemory)
		return;
	if ((stats::instructions & 0xFFFF) == 0) {
		// We need to avoid calling GetTotalMallocUsage() often because it
		// is O(elts on freelist). This is really bad since we start
		// to pummel the freelist once we hit the memory cap.
		unsigned mbs = util::GetTotalMallocUsage() >> 20;
		if (mbs > MaxMemory) {
			if (mbs > MaxMemory + 100) {
				// just guess at how many to kill
				unsigned numStates = states.size();
				unsigned toKill = std::max(1U, numStates - numStates * MaxMemory / mbs);
				klee_warning("killing %d states (over memory cap)", toKill);
				std::vector<ExecutionState *> arr(states.begin(), states.end());
				for (unsigned i = 0, N = arr.size(); N && i < toKill; ++i, --N) {
					unsigned idx = rand() % N;
					// Make two pulls to try and not hit a state that
					// covered new code.
					if (arr[idx]->coveredNew)
						idx = rand() % N;

					std::swap(arr[idx], arr[N - 1]);
					terminateStateEarly(*arr[N - 1], "Memory limit exceeded.");
				}
			}
			atMemoryLimit = true;
		} else {
			atMemoryLimit = false;
		}
	}
}

void Executor::run(ExecutionState &initialState) {
	bindModuleConstants();

	// Delay init till now so that ticks don't accrue during
	// optimization and such.
	initTimers();

	states.insert(&initialState);

	if (usingSeeds) {
		std::vector<SeedInfo> &v = seedMap[&initialState];

		for (std::vector<KTest*>::const_iterator it = usingSeeds->begin(), ie = usingSeeds->end(); it != ie; ++it)
			v.push_back(SeedInfo(*it));

		int lastNumSeeds = usingSeeds->size() + 10;
		double lastTime, startTime = lastTime = util::getWallTime();
		ExecutionState *lastState = 0;
		while (!seedMap.empty()) {
			if (haltExecution)
				goto dump;

			std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it = seedMap.upper_bound(lastState);
			if (it == seedMap.end())
				it = seedMap.begin();
			lastState = it->first;
			unsigned numSeeds = it->second.size();
			ExecutionState &state = *lastState;
			KInstruction *ki = state.currentThread->pc;
			stepInstruction(state);

			executeInstruction(state, ki);
			processTimers(&state, MaxInstructionTime * numSeeds);
			updateStates(&state);

			if ((stats::instructions % 1000) == 0) {
				int numSeeds = 0, numStates = 0;
				for (std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it = seedMap.begin(), ie = seedMap.end(); it != ie; ++it) {
					numSeeds += it->second.size();
					numStates++;
				}
				double time = util::getWallTime();
				if (SeedTime > 0. && time > startTime + SeedTime) {
					klee_warning("seed time expired, %d seeds remain over %d states", numSeeds, numStates);
					break;
				} else if (numSeeds <= lastNumSeeds - 10 || time >= lastTime + 10) {
					lastTime = time;
					lastNumSeeds = numSeeds;
					klee_message("%d seeds remaining over: %d states", numSeeds, numStates);
				}
			}
		}

		klee_message("seeding done (%d states remain)", (int) states.size());

		// XXX total hack, just because I like non uniform better but want
		// seed results to be equally weighted.
		for (std::set<ExecutionState*>::iterator it = states.begin(), ie = states.end(); it != ie; ++it) {
			(*it)->weight = 1.;
		}

		if (OnlySeed)
			goto dump;
	}

	searcher = constructUserSearcher(*this);
	searcher->update(0, states, std::set<ExecutionState*>());

	handleInitializers(initialState);

	while (!states.empty() && !haltExecution) {
		ExecutionState &state = searcher->selectState();
		Thread* thread = state.getNextThread();

		bool isAbleToRun = true;
		switch (thread->threadState) {
			case Thread::RUNNABLE: {
				break;
			}

			case Thread::MUTEX_BLOCKED: {
				//TODO:　死锁检测可以完善
				Thread* origin = thread;
				bool deadlock = false;
				do {
					std::string errorMsg;
					bool isBlocked;
					if (state.mutexManager.tryToLockForBlockedThread(thread->threadId, isBlocked, errorMsg)) {
						if (isBlocked) {
							if (prefix && !prefix->isFinished()) {
								llvm::errs() << "thread" << thread->threadId << ": " << thread->pc->info->file << "/"
										<< thread->pc->info->line << " " << thread->pc->inst->getOpcodeName() << "\n";
								thread->pc->inst->dump();
								llvm::errs() << "thread state is MUTEX_BLOCKED, try to get lock but failed\n";
								isAbleToRun = false;
								break;
								//assert(0 && "thread state is MUTEX_BLOCKED, try to get lock but failed");
							}
							state.reSchedule();
							thread = state.getNextThread();
							if (thread == origin) {
								if (deadlock) {
									llvm::errs() << "perhaps deadlock happen\n";
									isAbleToRun = false;
									break;
									//assert(0 && "perhaps deadlock happen");
								} else {
									deadlock = true;
								}
							}
						} else {
							state.switchThreadToRunnable(thread);
						}
					} else {
						llvm::errs() << errorMsg << "\n";
						assert(0 && "try to get lock but failed");
					}
				} while (!thread->isRunnable());

				break;
			}

			default: {
				isAbleToRun = false;
			}

		}

		//处理前缀出错以及执行出错
		if (!isAbleToRun) {
			//isExecutionSuccess = false;
			execStatus = RUNTIMEERROR;
			listenerService->executionFailed(state, state.currentThread->pc);
			llvm::errs() << "thread unable to run, Id: " << thread->threadId << " state: " << thread->threadState << "\n";
			terminateState(state);
			updateStates(&state);
			break;
			//assert(0 && "thread are unable to execute!");
		}
//    if (!thread->isRunnable()) {
//    	if (prefix && !prefix->isFinished()) {
//    		isExecutionSuccess = false;
//    		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
//    			(*bit)->executionFailed(state, ki);
//    		}
//    		terminateState(state);
//    		updateStates(&state);
//    		break;
//    	} else {
//    		cerr << "thread id: " << thread->threadId << " state: " << thread->threadState << endl;
//    		assert(0 && "thread unrunnable");
//    	}
//    }

		listenerService->ContextSwitch(this, state);

		KInstruction *ki = thread->pc;
		if (prefix && !prefix->isFinished() && ki != prefix->getCurrentInst()) {
			//cerr << "prefix: " << prefix->getCurrentInst() << " " << prefix->getCurrentInst()->inst->getOpcodeName() << " reality: " << ki << " " << ki->inst->getOpcodeName() << endl;
			llvm::errs() << "thread id : " << thread->threadId << "\n";
			llvm::errs() << "real : ";
			ki->inst->print(errs());
			llvm::errs() << "\n";
			llvm::errs() << "prefix : ";
			prefix->getCurrentInst()->inst->print(errs());
			llvm::errs() << "\n";
			llvm::errs() << "prefix unmatched\n";
			execStatus = IGNOREDERROR;
			terminateState(state);
			break;
			//assert(0 && "prefix unmatched");
		}
		stepInstruction(state);

		listenerService->beforeExecuteInstruction(this, state, ki);

		executeInstruction(state, ki);

		listenerService->afterExecuteInstruction(this, state, ki);

		if (prefix) {
			prefix->increasePosition();
		}
		if (execStatus != SUCCESS) {
			updateStates(&state);
			break;
		}
		processTimers(&state, MaxInstructionTime);

		if (state.threadScheduler->isSchedulerEmpty()) {
			if (!state.examineAllThreadFinalState()) {
				execStatus = RUNTIMEERROR;
			} else {
				execStatus = SUCCESS;
			}
			terminateState(state);
		}

		checkMemoryUsage();
		updateStates(&state);
	}

	delete searcher;
	searcher = 0;

	dump: if (DumpStatesOnHalt && !states.empty()) {
		llvm::errs() << "KLEE: halting execution, dumping remaining states\n";
		for (std::set<ExecutionState*>::iterator it = states.begin(), ie = states.end(); it != ie; ++it) {
			ExecutionState &state = **it;
			stepInstruction(state); // keep stats rolling
			terminateStateEarly(state, "Execution halting.");
		}
		updateStates(0);
	}
}

std::string Executor::getAddressInfo(ExecutionState &state, ref<Expr> address) const {
	std::string Str;
	llvm::raw_string_ostream info(Str);
	info << "\taddress: " << address << "\n";
	uint64_t example;
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(address)) {
		example = CE->getZExtValue();
	} else {
		ref<ConstantExpr> value;
		bool success = solver->getValue(state, address, value);
		assert(success && "FIXME: Unhandled solver failure");
		(void) success;
		example = value->getZExtValue();
		info << "\texample: " << example << "\n";
		std::pair<ref<Expr>, ref<Expr> > res = solver->getRange(state, address);
		info << "\trange: [" << res.first << ", " << res.second << "]\n";
	}

	MemoryObject hack((unsigned) example);
	MemoryMap::iterator lower = state.currentStack->addressSpace->objects.upper_bound(&hack);
	info << "\tnext: ";
	if (lower == state.currentStack->addressSpace->objects.end()) {
		info << "none\n";
	} else {
		const MemoryObject *mo = lower->first;
		std::string alloc_info;
		mo->getAllocInfo(alloc_info);
		info << "object at " << mo->address << " of size " << mo->size << "\n" << "\t\t" << alloc_info << "\n";
	}
	if (lower != state.currentStack->addressSpace->objects.begin()) {
		--lower;
		info << "\tprev: ";
		if (lower == state.currentStack->addressSpace->objects.end()) {
			info << "none\n";
		} else {
			const MemoryObject *mo = lower->first;
			std::string alloc_info;
			mo->getAllocInfo(alloc_info);
			info << "object at " << mo->address << " of size " << mo->size << "\n" << "\t\t" << alloc_info << "\n";
		}
	}

	return info.str();
}

void Executor::terminateState(ExecutionState &state) {
	if (replayKTest && replayPosition != replayKTest->numObjects) {
		klee_warning_once(replayKTest, "replay did not consume all objects in test input.");
	}

	interpreterHandler->incPathsExplored();

	std::set<ExecutionState*>::iterator it = addedStates.find(&state);
	if (it == addedStates.end()) {
		state.currentThread->pc = state.currentThread->prevPC;

		removedStates.insert(&state);
	} else {
		// never reached searcher, just delete immediately
		std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it3 = seedMap.find(&state);
		if (it3 != seedMap.end())
			seedMap.erase(it3);
		addedStates.erase(it);
		processTree->remove(state.ptreeNode);
		delete &state;
	}
}

void Executor::terminateStateEarly(ExecutionState &state, const Twine &message) {
	if (!OnlyOutputStatesCoveringNew || state.coveredNew || (AlwaysOutputSeeds && seedMap.count(&state)))
		interpreterHandler->processTestCase(state, (message + "\n").str().c_str(), "early");
	terminateState(state);
}

void Executor::terminateStateOnExit(ExecutionState &state) {
	if (!OnlyOutputStatesCoveringNew || state.coveredNew || (AlwaysOutputSeeds && seedMap.count(&state)))
		interpreterHandler->processTestCase(state, 0, 0);
	terminateState(state);
}

const InstructionInfo & Executor::getLastNonKleeInternalInstruction(const ExecutionState &state, Instruction ** lastInstruction) {
	// unroll the stack of the applications state and find
	// the last instruction which is not inside a KLEE internal function
	ExecutionState::stack_ty::const_reverse_iterator it = state.currentStack->realStack.rbegin(), itE =
			state.currentStack->realStack.rend();

	// don't check beyond the outermost function (i.e. main())
	itE--;
	const InstructionInfo * ii = 0;
	if (kmodule->internalFunctions.count(it->kf->function) == 0) {
		ii = state.currentThread->prevPC->info;
		*lastInstruction = state.currentThread->prevPC->inst;
		//  Cannot return yet because even though
		//  it->function is not an internal function it might of
		//  been called from an internal function.
	}
	// Wind up the stack and check if we are in a KLEE internal function.
	// We visit the entire stack because we want to return a CallInstruction
	// that was not reached via any KLEE internal functions.
	for (; it != itE; ++it) {
		// check calling instruction and if it is contained in a KLEE internal function
		const Function * f = (*it->caller).inst->getParent()->getParent();
		if (kmodule->internalFunctions.count(f)) {
			ii = 0;
			continue;
		}
		if (!ii) {
			ii = (*it->caller).info;
			*lastInstruction = (*it->caller).inst;
		}
	}
	if (!ii) {
		// something went wrong, play safe and return the current instruction info
		*lastInstruction = state.currentThread->prevPC->inst;
		return *state.currentThread->prevPC->info;
	}
	return *ii;
}
void Executor::terminateStateOnError(ExecutionState &state, const llvm::Twine &messaget, const char *suffix, const llvm::Twine &info) {
	std::string message = messaget.str();
	static std::set<std::pair<Instruction*, std::string> > emittedErrors;
	Instruction * lastInst;
	const InstructionInfo &ii = getLastNonKleeInternalInstruction(state, &lastInst);

	if (EmitAllErrors || emittedErrors.insert(std::make_pair(lastInst, message)).second) {
		if (ii.file != "") {
			klee_message("ERROR: %s:%d: %s", ii.file.c_str(), ii.line, message.c_str());
		} else {
			klee_message("ERROR: (location information missing) %s", message.c_str());
		}
		if (!EmitAllErrors)
			klee_message("NOTE: now ignoring this error at this location");

		std::string MsgString;
		llvm::raw_string_ostream msg(MsgString);
		msg << "Error: " << message << "\n";
		if (ii.file != "") {
			msg << "File: " << ii.file << "\n";
			msg << "Line: " << ii.line << "\n";
			msg << "assembly.ll line: " << ii.assemblyLine << "\n";
		}
		msg << "Stack: \n";
		state.currentThread->dumpStack(msg);

		std::string info_str = info.str();
		if (info_str != "")
			msg << "Info: \n" << info_str;
		//存在问题,暂时处理
		//add by ylc
//		interpreterHandler->processTestCase(state, msg.str().c_str(), suffix);
	}

	terminateState(state);
	llvm::errs() << "encounter runtime error\n";
	execStatus = RUNTIMEERROR;
	listenerService->executionFailed(state, state.currentThread->pc);
}

// XXX shoot me
static const char *okExternalsList[] = { "printf", "fprintf", "puts", "getpid" };
static std::set<std::string> okExternals(okExternalsList, okExternalsList + (sizeof(okExternalsList) / sizeof(okExternalsList[0])));

void Executor::callExternalFunction(ExecutionState &state, KInstruction *target, Function *function, std::vector<ref<Expr> > &arguments) {
	// check if specialFunctionHandler wants it
	if (specialFunctionHandler->handle(state, function, target, arguments))
		return;

	if (NoExternals && !okExternals.count(function->getName())) {
		llvm::errs() << "KLEE:ERROR: Calling not-OK external function : " << function->getName().str() << "\n";
		terminateStateOnError(state, "externals disallowed", "user.err");
		return;
	}

	// normal external function handling path
	// allocate 128 bits for each argument (+return value) to support fp80's;
	// we could iterate through all the arguments first and determine the exact
	// size we need, but this is faster, and the memory usage isn't significant.
	uint64_t *args = (uint64_t*) alloca(2 * sizeof(*args) * (arguments.size() + 1));
	memset(args, 0, 2 * sizeof(*args) * (arguments.size() + 1));
	unsigned wordIndex = 2;
	for (std::vector<ref<Expr> >::iterator ai = arguments.begin(), ae = arguments.end(); ai != ae; ++ai) {
		if (AllowExternalSymCalls) { // don't bother checking uniqueness
			ref<ConstantExpr> ce;
			bool success = solver->getValue(state, *ai, ce);
			assert(success && "FIXME: Unhandled solver failure");
			(void) success;
			ce->toMemory(&args[wordIndex]);
			wordIndex += (ce->getWidth() + 63) / 64;
		} else {
			ref<Expr> arg = toUnique(state, *ai);
			if (ConstantExpr *ce = dyn_cast<ConstantExpr>(arg)) {
				// XXX kick toMemory functions from here
				ce->toMemory(&args[wordIndex]);
				wordIndex += (ce->getWidth() + 63) / 64;
			} else {
				terminateStateOnExecError(state, "external call with symbolic argument: " + function->getName());
				return;
			}
		}
	}

	state.addressSpace.copyOutConcretes();

	if (!SuppressExternalWarnings) {

		std::string TmpStr;
		llvm::raw_string_ostream os(TmpStr);
		os << "calling external: " << function->getName().str() << "(";
		for (unsigned i = 0; i < arguments.size(); i++) {
			os << arguments[i];
			if (i != arguments.size() - 1)
				os << ", ";
		}
		os << ")";

		if (AllExternalWarnings)
			klee_warning("%s", os.str().c_str());
		else
			klee_warning_once(function, "%s", os.str().c_str());
	}

	bool success = externalDispatcher->executeCall(function, target->inst, args);
	if (!success) {
		terminateStateOnError(state, "failed external call: " + function->getName(), "external.err");
		return;
	}

	if (!state.addressSpace.copyInConcretes()) {
		terminateStateOnError(state, "external modified read-only object", "external.err");
		return;
	}

	LLVM_TYPE_Q Type *resultType = target->inst->getType();
	if (resultType != Type::getVoidTy(getGlobalContext())) {
		ref<Expr> e = ConstantExpr::fromMemory((void*) args, getWidthForLLVMType(resultType));
		bindLocal(target, state, e);
	}
}

/***/

ref<Expr> Executor::replaceReadWithSymbolic(ExecutionState &state, ref<Expr> e) {
	unsigned n = interpreterOpts.MakeConcreteSymbolic;
	if (!n || replayKTest || replayPath)
		return e;

	// right now, we don't replace symbolics (is there any reason to?)
	if (!isa<ConstantExpr>(e))
		return e;

	if (n != 1 && random() % n)
		return e;

	// create a new fresh location, assert it is equal to concrete value in e
	// and return it.

	static unsigned id;
	const Array *array = arrayCache.CreateArray("rrws_arr" + llvm::utostr(++id), Expr::getMinBytesForWidth(e->getWidth()));
	ref<Expr> res = Expr::createTempRead(array, e->getWidth());
	ref<Expr> eq = NotOptimizedExpr::create(EqExpr::create(e, res));
	llvm::errs() << "Making symbolic: " << eq << "\n";
	state.addConstraint(eq);
	return res;
}

ObjectState *Executor::bindObjectInState(ExecutionState &state, const MemoryObject *mo, bool isLocal, const Array *array) {
	ObjectState *os = array ? new ObjectState(mo, array) : new ObjectState(mo);
//	llvm::errs() << "ObjectState *os = array ? new ObjectState(mo, array) : new ObjectState(mo);\n";
	state.currentStack->addressSpace->bindObject(mo, os);
//	llvm::errs() << "state.currentStack->addressSpace->bindObject(mo, os);\n";

	// Its possible that multiple bindings of the same mo in the state
	// will put multiple copies on this list, but it doesn't really
	// matter because all we use this list for is to unbind the object
	// on function return.
	if (isLocal) {
		state.currentStack->realStack.back().allocas.push_back(mo);
	}
	return os;
}

void Executor::executeAlloc(ExecutionState &state, ref<Expr> size, bool isLocal, KInstruction *target, bool zeroMemory,
		const ObjectState *reallocFrom) {
	size = toUnique(state, size);
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(size)) {
		MemoryObject *mo = memory->allocate(CE->getZExtValue(), isLocal, false, state.currentThread->prevPC->inst);
		if (!mo) {
			bindLocal(target, state, ConstantExpr::alloc(0, Context::get().getPointerWidth()));
		} else {
//			llvm::errs() << "alloc address ; " << mo->address << " size : " << CE->getZExtValue() << "\n";
			ObjectState *os = bindObjectInState(state, mo, isLocal);
			if (zeroMemory) {
				os->initializeToZero();
			} else {
				os->initializeToRandom();
			}
			bindLocal(target, state, mo->getBaseExpr());

			if (reallocFrom) {
				unsigned count = std::min(reallocFrom->size, os->size);
				for (unsigned i = 0; i < count; i++)
					os->write(i, reallocFrom->read8(i));
				state.currentStack->addressSpace->unbindObject(reallocFrom->getObject());
			}
		}
	} else {
		// XXX For now we just pick a size. Ideally we would support
		// symbolic sizes fully but even if we don't it would be better to
		// "smartly" pick a value, for example we could fork and pick the
		// min and max values and perhaps some intermediate (reasonable
		// value).
		//
		// It would also be nice to recognize the case when size has
		// exactly two values and just fork (but we need to get rid of
		// return argument first). This shows up in pcre when llvm
		// collapses the size expression with a select.

		ref<ConstantExpr> example;
		bool success = solver->getValue(state, size, example);
		assert(success && "FIXME: Unhandled solver failure");
		(void) success;

		// Try and start with a small example.
		Expr::Width W = example->getWidth();
		while (example->Ugt(ConstantExpr::alloc(128, W))->isTrue()) {
			ref<ConstantExpr> tmp = example->LShr(ConstantExpr::alloc(1, W));
			bool res;
			bool success = solver->mayBeTrue(state, EqExpr::create(tmp, size), res);
			assert(success && "FIXME: Unhandled solver failure");
			(void) success;
			if (!res)
				break;
			example = tmp;
		}

		StatePair fixedSize = fork(state, EqExpr::create(example, size), true);

		if (fixedSize.second) {
			// Check for exactly two values
			ref<ConstantExpr> tmp;
			bool success = solver->getValue(*fixedSize.second, size, tmp);
			assert(success && "FIXME: Unhandled solver failure");
			(void) success;
			bool res;
			success = solver->mustBeTrue(*fixedSize.second, EqExpr::create(tmp, size), res);
			assert(success && "FIXME: Unhandled solver failure");
			(void) success;
			if (res) {
				executeAlloc(*fixedSize.second, tmp, isLocal, target, zeroMemory, reallocFrom);
			} else {
				// See if a *really* big value is possible. If so assume
				// malloc will fail for it, so lets fork and return 0.
				StatePair hugeSize = fork(*fixedSize.second, UltExpr::create(ConstantExpr::alloc(1 << 31, W), size), true);
				if (hugeSize.first) {
					klee_message("NOTE: found huge malloc, returning 0");
					bindLocal(target, *hugeSize.first, ConstantExpr::alloc(0, Context::get().getPointerWidth()));
				}

				if (hugeSize.second) {

					std::string Str;
					llvm::raw_string_ostream info(Str);
					ExprPPrinter::printOne(info, "  size expr", size);
					info << "  concretization : " << example << "\n";
					info << "  unbound example: " << tmp << "\n";
					terminateStateOnError(*hugeSize.second, "concretized symbolic size", "model.err", info.str());
				}
			}
		}

		if (fixedSize.first) // can be zero when fork fails
			executeAlloc(*fixedSize.first, example, isLocal, target, zeroMemory, reallocFrom);
	}
}

void Executor::executeFree(ExecutionState &state, ref<Expr> address, KInstruction *target) {
	StatePair zeroPointer = fork(state, Expr::createIsZero(address), true);
	if (zeroPointer.first) {
		if (target)
			bindLocal(target, *zeroPointer.first, Expr::createPointer(0));
	}
	if (zeroPointer.second) { // address != 0
		ExactResolutionList rl;
		resolveExact(*zeroPointer.second, address, rl, "free");

		for (Executor::ExactResolutionList::iterator it = rl.begin(), ie = rl.end(); it != ie; ++it) {
			const MemoryObject *mo = it->first.first;
			if (mo->isLocal) {
				terminateStateOnError(*it->second, "free of alloca", "free.err", getAddressInfo(*it->second, address));
			} else if (mo->isGlobal) {
				terminateStateOnError(*it->second, "free of global", "free.err", getAddressInfo(*it->second, address));
			} else {
				it->second->currentStack->addressSpace->unbindObject(mo);
				if (target)
					bindLocal(target, *it->second, Expr::createPointer(0));
			}
		}
	}
}

void Executor::resolveExact(ExecutionState &state, ref<Expr> p, ExactResolutionList &results, const std::string &name) {
	// XXX we may want to be capping this?
	ResolutionList rl;
	state.currentStack->addressSpace->resolve(state, solver, p, rl);

	ExecutionState *unbound = &state;
	for (ResolutionList::iterator it = rl.begin(), ie = rl.end(); it != ie; ++it) {
		ref<Expr> inBounds = EqExpr::create(p, it->first->getBaseExpr());

		StatePair branches = fork(*unbound, inBounds, true);

		if (branches.first)
			results.push_back(std::make_pair(*it, branches.first));

		unbound = branches.second;
		if (!unbound) // Fork failure
			break;
	}

	if (unbound) {
		terminateStateOnError(*unbound, "memory error: invalid pointer: " + name, "ptr.err", getAddressInfo(*unbound, p));
	}
}

void Executor::executeMemoryOperation(ExecutionState &state, bool isWrite, ref<Expr> address, ref<Expr> value /* undef if read */,
		KInstruction *target /* undef if write */) {
	Expr::Width type = (isWrite ? value->getWidth() : getWidthForLLVMType(target->inst->getType()));
	unsigned bytes = Expr::getMinBytesForWidth(type);

	if (SimplifySymIndices) {
		if (!isa<ConstantExpr>(address))
			address = state.constraints.simplifyExpr(address);
		if (isWrite && !isa<ConstantExpr>(value))
			value = state.constraints.simplifyExpr(value);
	}

	// fast path: single in-bounds resolution
	ObjectPair op;
	bool success;
	solver->setTimeout(coreSolverTimeout);
	if (!state.currentStack->addressSpace->resolveOne(state, solver, address, op, success)) {
		address = toConstant(state, address, "resolveOne failure");
		success = state.currentStack->addressSpace->resolveOne(cast<ConstantExpr>(address), op);
	}
	solver->setTimeout(0);

	if (success) {
		const MemoryObject *mo = op.first;

		if (MaxSymArraySize && mo->size >= MaxSymArraySize) {
			address = toConstant(state, address, "max-sym-array-size");
		}

		ref<Expr> offset = mo->getOffsetExpr(address);

		bool inBounds;
		solver->setTimeout(coreSolverTimeout);
		bool success = solver->mustBeTrue(state, mo->getBoundsCheckOffset(offset, bytes), inBounds);
		solver->setTimeout(0);
		if (!success) {
			state.currentThread->pc = state.currentThread->prevPC;
			terminateStateEarly(state, "Query timed out (bounds check).");
			return;
		}

		if (inBounds) {
			const ObjectState *os = op.second;
			if (isWrite) {
				if (os->readOnly) {
					terminateStateOnError(state, "memory error: object read only", "readonly.err");
				} else {
					ObjectState *wos = state.currentStack->addressSpace->getWriteable(mo, os);
					wos->write(offset, value);
#if DEBUG_RUNTIME
					llvm::errs() << "store offset : " << offset << " store address : " << mo->address << " store value : " << value << "\n";
#endif
				}
			} else {
				ref<Expr> result = os->read(offset, type);

				if (interpreterOpts.MakeConcreteSymbolic)
					result = replaceReadWithSymbolic(state, result);
#if DEBUG_RUNTIME
				llvm::errs() << "load value : " << result << "\n";
#endif
				bindLocal(target, state, result);
			}

			return;
		}
	}

	// we are on an error path (no resolution, multiple resolution, one
	// resolution with out of bounds)

	ResolutionList rl;
	solver->setTimeout(coreSolverTimeout);
	bool incomplete = state.currentStack->addressSpace->resolve(state, solver, address, rl, 0, coreSolverTimeout);
	solver->setTimeout(0);

	// XXX there is some query wasteage here. who cares?
	ExecutionState *unbound = &state;

	for (ResolutionList::iterator i = rl.begin(), ie = rl.end(); i != ie; ++i) {
		const MemoryObject *mo = i->first;
		const ObjectState *os = i->second;
		ref<Expr> inBounds = mo->getBoundsCheckPointer(address, bytes);

		StatePair branches = fork(*unbound, inBounds, true);
		ExecutionState *bound = branches.first;

		// bound can be 0 on failure or overlapped
		if (bound) {
			if (isWrite) {
				if (os->readOnly) {
					terminateStateOnError(*bound, "memory error: object read only", "readonly.err");
				} else {
					ObjectState *wos = bound->currentStack->addressSpace->getWriteable(mo, os);
					wos->write(mo->getOffsetExpr(address), value);
				}
			} else {
				ref<Expr> result = os->read(mo->getOffsetExpr(address), type);
				bindLocal(target, *bound, result);
			}
		}

		unbound = branches.second;
		if (!unbound)
			break;
	}

	// XXX should we distinguish out of bounds and overlapped cases?
	if (unbound) {
		if (incomplete) {
			terminateStateEarly(*unbound, "Query timed out (resolve).");
		} else {
			terminateStateOnError(*unbound, "memory error: out of bound pointer", "ptr.err", getAddressInfo(*unbound, address));
		}
	}
}


ref<Expr> Executor::readExpr(ExecutionState& state, AddressSpace *addressSpace, ref<Expr> address, Expr::Width size) {
	ObjectPair op;
	getMemoryObject(op, state, addressSpace, address);
	const MemoryObject *mo = op.first;
	ref<Expr> offset = mo->getOffsetExpr(address);
	const ObjectState *os = op.second;
	ref<Expr> result = os->read(offset, size);
	return result;
}

void Executor::executeMakeSymbolic(ExecutionState &state, const MemoryObject *mo, const std::string &name) {
	// Create a new object state for the memory object (instead of a copy).
	if (!replayKTest) {
		// Find a unique name for this array.  First try the original name,
		// or if that fails try adding a unique identifier.
		unsigned id = 0;
		std::string uniqueName = name;
		while (!state.arrayNames.insert(uniqueName).second) {
			uniqueName = name + "_" + llvm::utostr(++id);
		}
		const Array *array = arrayCache.CreateArray(uniqueName, mo->size);
		bindObjectInState(state, mo, false, array);
		state.addSymbolic(mo, array);

		std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it = seedMap.find(&state);
		if (it != seedMap.end()) { // In seed mode we need to add this as a
								   // binding.
			for (std::vector<SeedInfo>::iterator siit = it->second.begin(), siie = it->second.end(); siit != siie; ++siit) {
				SeedInfo &si = *siit;
				KTestObject *obj = si.getNextInput(mo, NamedSeedMatching);

				if (!obj) {
					if (ZeroSeedExtension) {
						std::vector<unsigned char> &values = si.assignment.bindings[array];
						values = std::vector<unsigned char>(mo->size, '\0');
					} else if (!AllowSeedExtension) {
						terminateStateOnError(state, "ran out of inputs during seeding", "user.err");
						break;
					}
				} else {
					if (obj->numBytes != mo->size
							&& ((!(AllowSeedExtension || ZeroSeedExtension) && obj->numBytes < mo->size)
									|| (!AllowSeedTruncation && obj->numBytes > mo->size))) {
						std::stringstream msg;
						msg << "replace size mismatch: " << mo->name << "[" << mo->size << "]" << " vs " << obj->name << "["
								<< obj->numBytes << "]" << " in test\n";

						terminateStateOnError(state, msg.str(), "user.err");
						break;
					} else {
						std::vector<unsigned char> &values = si.assignment.bindings[array];
						values.insert(values.begin(), obj->bytes, obj->bytes + std::min(obj->numBytes, mo->size));
						if (ZeroSeedExtension) {
							for (unsigned i = obj->numBytes; i < mo->size; ++i)
								values.push_back('\0');
						}
					}
				}
			}
		}
	} else {
		ObjectState *os = bindObjectInState(state, mo, false);
		if (replayPosition >= replayKTest->numObjects) {
			terminateStateOnError(state, "replay count mismatch", "user.err");
		} else {
			KTestObject *obj = &replayKTest->objects[replayPosition++];
			if (obj->numBytes != mo->size) {
				terminateStateOnError(state, "replay size mismatch", "user.err");
			} else {
				for (unsigned i = 0; i < mo->size; i++)
					os->write8(i, obj->bytes[i]);
			}
		}
	}
}

/***/

void Executor::runFunctionAsMain(Function *f, int argc, char **argv, char **envp) {
	std::vector<ref<Expr> > arguments;

	// force deterministic initialization of memory objects
	srand(1);
	srandom(1);

	MemoryObject *argvMO = 0;

	// In order to make uclibc happy and be closer to what the system is
	// doing we lay out the environments at the end of the argv array
	// (both are terminated by a null). There is also a final terminating
	// null that uclibc seems to expect, possibly the ELF header?

	int envc;
	for (envc = 0; envp[envc]; ++envc)
		;

	unsigned NumPtrBytes = Context::get().getPointerWidth() / 8;
	KFunction *kf = kmodule->functionMap[f];
	assert(kf);
	Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
	if (ai != ae) {
		arguments.push_back(ConstantExpr::alloc(argc, Expr::Int32));

		if (++ai != ae) {
			argvMO = memory->allocate((argc + 1 + envc + 1 + 1) * NumPtrBytes, false, true, f->begin()->begin());

			arguments.push_back(argvMO->getBaseExpr());

			if (++ai != ae) {
				uint64_t envp_start = argvMO->address + (argc + 1) * NumPtrBytes;
				arguments.push_back(Expr::createPointer(envp_start));

				if (++ai != ae)
					klee_error("invalid main function (expect 0-3 arguments)");
			}
		}
	}

	ExecutionState *state;
	if (prefix) {
		state = new ExecutionState(kmodule->functionMap[f], prefix);
	} else {
		state = new ExecutionState(kmodule->functionMap[f]);
	}

	if (pathWriter)
		state->pathOS = pathWriter->open();
	if (symPathWriter)
		state->symPathOS = symPathWriter->open();

	if (statsTracker)
		statsTracker->framePushed(*state, 0);

	assert(arguments.size() == f->arg_size() && "wrong number of arguments");
	for (unsigned i = 0, e = f->arg_size(); i != e; ++i)
		bindArgument(kf, i, *state, arguments[i]);

	if (argvMO) {
		ObjectState *argvOS = bindObjectInState(*state, argvMO, false);

		for (int i = 0; i < argc + 1 + envc + 1 + 1; i++) {
			if (i == argc || i >= argc + 1 + envc) {
				// Write NULL pointer
				argvOS->write(i * NumPtrBytes, Expr::createPointer(0));
			} else {
				char *s = i < argc ? argv[i] : envp[i - (argc + 1)];
				int j, len = strlen(s);

				MemoryObject *arg = memory->allocate(len + 1, false, true, state->currentThread->pc->inst);
				ObjectState *os = bindObjectInState(*state, arg, false);
				for (j = 0; j < len + 1; j++)
					os->write8(j, s[j]);

				// Write pointer to newly allocated and initialised argv/envp c-string
				argvOS->write(i * NumPtrBytes, arg->getBaseExpr());
			}
		}
	}

	initializeGlobals(*state);

	processTree = new PTree(state);
	state->ptreeNode = processTree->root;

	listenerService->beforeRunMethodAsMain(this, *state, f, argvMO, arguments, argc, argv, envp);

	run(*state);

	delete processTree;
	processTree = 0;

	// hack to clear memory objects
	delete memory;
	memory = new MemoryManager(NULL);

	globalObjects.clear();
	globalAddresses.clear();

	if (statsTracker)
		statsTracker->done();

	listenerService->afterRunMethodAsMain(*state);
}

unsigned Executor::getPathStreamID(const ExecutionState &state) {
	assert(pathWriter);
	return state.pathOS.getID();
}

unsigned Executor::getSymbolicPathStreamID(const ExecutionState &state) {
	assert(symPathWriter);
	return state.symPathOS.getID();
}

void Executor::getConstraintLog(const ExecutionState &state, std::string &res, Interpreter::LogType logFormat) {

	std::ostringstream info;

	switch (logFormat) {
		case STP: {
			Query query(state.constraints, ConstantExpr::alloc(0, Expr::Bool));
			char *log = solver->getConstraintLog(query);
			res = std::string(log);
			free(log);
		}
			break;

		case KQUERY: {
			std::string Str;
			llvm::raw_string_ostream info(Str);
			ExprPPrinter::printConstraints(info, state.constraints);
			res = info.str();
		}
			break;

		case SMTLIB2: {
			std::string Str;
			llvm::raw_string_ostream info(Str);
			ExprSMTLIBPrinter printer;
			printer.setOutput(info);
			Query query(state.constraints, ConstantExpr::alloc(0, Expr::Bool));
			printer.setQuery(query);
			printer.generateOutput();
			res = info.str();
		}
			break;

		default:
			klee_warning("Executor::getConstraintLog() : Log format not supported!");
	}
}

bool Executor::getSymbolicSolution(const ExecutionState &state, std::vector<std::pair<std::string, std::vector<unsigned char> > > &res) {
	solver->setTimeout(coreSolverTimeout);

	ExecutionState tmp(state);

	// Go through each byte in every test case and attempt to restrict
	// it to the constraints contained in cexPreferences.  (Note:
	// usually this means trying to make it an ASCII character (0-127)
	// and therefore human readable. It is also possible to customize
	// the preferred constraints.  See test/Features/PreferCex.c for
	// an example) While this process can be very expensive, it can
	// also make understanding individual test cases much easier.
	for (unsigned i = 0; i != state.symbolics.size(); ++i) {
		const MemoryObject *mo = state.symbolics[i].first;
		std::vector<ref<Expr> >::const_iterator pi = mo->cexPreferences.begin(), pie = mo->cexPreferences.end();
		for (; pi != pie; ++pi) {
			bool mustBeTrue;
			// Attempt to bound byte to constraints held in cexPreferences
			bool success = solver->mustBeTrue(tmp, Expr::createIsZero(*pi), mustBeTrue);
			// If it isn't possible to constrain this particular byte in the desired
			// way (normally this would mean that the byte can't be constrained to
			// be between 0 and 127 without making the entire constraint list UNSAT)
			// then just continue on to the next byte.
			if (!success)
				break;
			// If the particular constraint operated on in this iteration through
			// the loop isn't implied then add it to the list of constraints.
			if (!mustBeTrue)
				tmp.addConstraint(*pi);
		}
		if (pi != pie)
			break;
	}

	std::vector<std::vector<unsigned char> > values;
	std::vector<const Array*> objects;
	for (unsigned i = 0; i != state.symbolics.size(); ++i)
		objects.push_back(state.symbolics[i].second);
	bool success = solver->getInitialValues(tmp, objects, values);
	solver->setTimeout(0);
	if (!success) {
		klee_warning("unable to compute initial values (invalid constraints?)!");
		ExprPPrinter::printQuery(llvm::errs(), state.constraints, ConstantExpr::alloc(0, Expr::Bool));
		return false;
	}

	for (unsigned i = 0; i != state.symbolics.size(); ++i)
		res.push_back(std::make_pair(state.symbolics[i].first->name, values[i]));
	return true;
}

void Executor::getCoveredLines(const ExecutionState &state, std::map<const std::string*, std::set<unsigned> > &res) {
	res = state.coveredLines;
}

void Executor::doImpliedValueConcretization(ExecutionState &state, ref<Expr> e, ref<ConstantExpr> value) {
	abort(); // FIXME: Broken until we sort out how to do the write back.

	if (DebugCheckForImpliedValues)
		ImpliedValue::checkForImpliedValues(solver->solver, e, value);

	ImpliedValueList results;
	ImpliedValue::getImpliedValues(e, value, results);
	for (ImpliedValueList::iterator it = results.begin(), ie = results.end(); it != ie; ++it) {
		ref<ReadExpr> re = it->first;

		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
			// FIXME: This is the sole remaining usage of the Array object
			// variable. Kill me.
			const MemoryObject *mo = 0; //re->updates.root->object;
			const ObjectState *os = state.addressSpace.findObject(mo);

			if (!os) {
				// object has been free'd, no need to concretize (although as
				// in other cases we would like to concretize the outstanding
				// reads, but we have no facility for that yet)
			} else {
				assert(!os->readOnly && "not possible? read only object with static read?");
				ObjectState *wos = state.addressSpace.getWriteable(mo, os);
				wos->write(CE, it->second);
			}
		}
	}
}

Expr::Width Executor::getWidthForLLVMType(LLVM_TYPE_Q llvm::Type *type) const {
	return kmodule->targetData->getTypeSizeInBits(type);
}

Interpreter *Interpreter::create(const InterpreterOptions &opts, InterpreterHandler *ih) {
	return new Executor(opts, ih);
}

/**
 * 获取address对应的ObjectPair
 */
bool Executor::getMemoryObject(ObjectPair& op, ExecutionState& state, AddressSpace *addressSpace, ref<Expr> address) {
	TimingSolver* solver = getTimeSolver();
	bool success;
	if (!addressSpace->resolveOne(state, solver, address, op, success)) {
		address = toConstant(state, address, "resolveOne failure");
		success = addressSpace->resolveOne(cast<ConstantExpr>(address), op);
	}
	return success;
}

bool Executor::isGlobalMO(const MemoryObject* mo) {
	bool result;
	if (mo->isGlobal) {
		result = true;
	} else {
		if (mo->isLocal) {
			result = false;
		} else {
			result = true;
		}
	}
	return result;
}

bool Executor::isFunctionSpecial(Function* f) {
	if (specialFunctionHandler->handlers.find(f) == specialFunctionHandler->handlers.end()) {
		return false;
	} else {
		return true;
	}
}

void Executor::runVerification(llvm::Function *f, int argc, char **argv, char **envp) {
	while (!isFinished) {
		execStatus = SUCCESS;
		listenerService->startControl(this);
		runFunctionAsMain(f, argc, argv, envp);
		listenerService->endControl(this);
		prepareNextExecution();
	}
}

void Executor::prepareNextExecution() {
	for (std::set<ExecutionState*>::const_iterator it = states.begin(), ie =
			states.end(); it != ie; ++it) {
		delete *it;
	}
}

void Executor::getNewPrefix() {
	//获取新的前缀
	Prefix* prefix = listenerService->getRuntimeDataManager()->getNextPrefix();
	//Prefix* prefix = NULL;
	if (prefix) {
		delete this->prefix;
		this->prefix = prefix;
		isFinished = false;
	} else {
		isFinished = true;
#if PRINT_RUNTIMEINFO
		printPrefix();
#endif
	}
}

/**
 * 打印函数，用于打印执行的指令
 */
void Executor::printInstrcution(ExecutionState &state, KInstruction* ki) {
	std::string fileName = "trace" + Transfer::uint64toString(listenerService->getRuntimeDataManager()->getCurrentTrace()->Id) + ".txt";
	std::string errorMsg;
	raw_fd_ostream out(fileName.c_str(), errorMsg, sys::fs::F_Append);
	//ostream& out = cerr;
	if (prefix && prefix->isFinished()) {
		out << "prefix finished\n";
	}
	Instruction* inst = ki->inst;
	if (MDNode *mdNode = inst->getMetadata("dbg")) { // Here I is an LLVM instruction
		DILocation loc(mdNode); // DILocation is in DebugInfo.h
		unsigned line = loc.getLineNumber();
		std::string file = loc.getFilename().str();
		std::string dir = loc.getDirectory().str();
		out << "thread" << state.currentThread->threadId << " " << dir << "/" << file << " " << line << ": ";
		inst->print(out);
		//cerr << "thread" << state.currentThread->threadId << " " << dir << "/" << file << " " << line << ": " << inst->getOpcodeName() << endl;
		switch (inst->getOpcode()) {
			case Instruction::Call: {
				CallSite cs(inst);
				Value *fp = cs.getCalledValue();
				Function *f = getTargetFunction(fp, state);
				if (!f) {
					ref<Expr> expr = eval(ki, 0, state).value;
					ConstantExpr* constExpr = dyn_cast<ConstantExpr>(expr);
					uint64_t functionPtr = constExpr->getZExtValue();
					f = (Function*) functionPtr;
				}
				out << " " << f->getName().str();
				if (f->getName().str() == "pthread_mutex_lock") {
					ref<Expr> param = eval(ki, 1, state).value;
					ConstantExpr* cexpr = dyn_cast<ConstantExpr>(param);
					out << " " << cexpr->getZExtValue();
				} else if (f->getName().str() == "pthread_mutex_unlock") {
					ref<Expr> param = eval(ki, 1, state).value;
					ConstantExpr* cexpr = dyn_cast<ConstantExpr>(param);
					out << " " << cexpr->getZExtValue();
				} else if (f->getName().str() == "pthread_cond_wait") {
					//get lock
					ref<Expr> param = eval(ki, 2, state).value;
					ConstantExpr* cexpr = dyn_cast<ConstantExpr>(param);
					out << " " << cexpr->getZExtValue();
					//get cond
					param = eval(ki, 1, state).value;
					cexpr = dyn_cast<ConstantExpr>(param);
					out << " " << cexpr->getZExtValue();
				} else if (f->getName().str() == "pthread_cond_signal") {
					ref<Expr> param = eval(ki, 1, state).value;
					ConstantExpr* cexpr = dyn_cast<ConstantExpr>(param);
					out << " " << cexpr->getZExtValue();
				} else if (f->getName().str() == "pthread_cond_broadcast") {
					ref<Expr> param = eval(ki, 1, state).value;
					ConstantExpr* cexpr = dyn_cast<ConstantExpr>(param);
					out << " " << cexpr->getZExtValue();
				}
				break;
			}

		}
		out << '\n';
	} else {
		out << "thread" << state.currentThread->threadId << " klee/internal 0: " << inst->getOpcodeName() << '\n';
	}

	out.close();
	if (prefix && prefix->current() + 1 == prefix->end()) {
		inst->print(errs());
		Event* event = *prefix->current();
		ref<Expr> param = eval(ki, 0, state).value;
		ConstantExpr* condition = dyn_cast<ConstantExpr>(param);
		if (condition->getAPValue().getBoolValue() != event->brCondition) {
			llvm::errs() << "\n前缀已被取反\n";
		} else {
			llvm::errs() << "\n前缀未被取反\n";
		}
	}
}

void Executor::printPrefix() {
	if (prefix) {
		std::string fileName = prefix->getName() + ".txt";
		std::string errorMsg;
		raw_fd_ostream out(fileName.c_str(), errorMsg, sys::fs::F_Append);
		prefix->print(out);
		out.close();
		//prefix->print(cerr);
	}
}

/**
 * execute pthread_create
 * if the first pointer is not point to a unsigned int, this function will crash
 * ylc
 **/
unsigned Executor::executePThreadCreate(ExecutionState &state, KInstruction *ki, std::vector<ref<Expr> > &arguments) {
	CallInst* calli = dyn_cast<CallInst>(ki->inst);
	if (calli) {
		assert(calli->getNumArgOperands() == 4 && "pthread_create has 4 params");
		Value* threadEntranceFP = calli->getArgOperand(2);
		Function *threadEntrance = getTargetFunction(threadEntranceFP, state);
		if (!threadEntrance) {
			ref<Expr> param = eval(ki, 3, state).value;
			ConstantExpr* functionPtr = dyn_cast<ConstantExpr>(param);
			threadEntrance = (Function*) (functionPtr->getZExtValue());
		}
		KFunction *kthreadEntrance = kmodule->functionMap[threadEntrance];
		Thread* newThread = NULL;
		if (prefix && !prefix->isFinished()) {
			newThread = state.createThread(kthreadEntrance, prefix->getNextThreadId());
		} else {
			newThread = state.createThread(kthreadEntrance);
		}

		// vector clock : creat
		Thread *thread = state.getCurrentThread();
		for (unsigned i = 0; i < thread->vectorClock.size(); i++) {
			newThread->vectorClock[i] = thread->vectorClock[i];
		}
		newThread->vectorClock[newThread->threadId]++;

		state.currentStack = newThread->stack;
		bindArgument(kthreadEntrance, 0, state, arguments[3]);
		state.currentStack = thread->stack;

		if (statsTracker)
			statsTracker->framePushed(state, 0);
//		PTree* ptree = new PTree(newThread);
//		ptreeVector[nextThreadId] = ptree;

		// set threadId to function's first param
		PointerType* pointerType = (PointerType*) (calli->getArgOperand(0)->getType());
		IntegerType* elementType = (IntegerType*) (pointerType->getElementType());
		Expr::Width type = elementType->getBitWidth();
		ref<Expr> pidAddress = arguments[0];
		executeMemoryOperation(state, true, pidAddress, ConstantExpr::create(newThread->threadId, type), 0);
//		llvm::errs() << "pidAddress : " << pidAddress << "\n";
	} else {
		assert(0 && "inst must be callInst!");
	}
	return 0;
}

/**
 * execute pthread_join
 * if the first pointer is not point to a unsigned int, this function will crash
 * ylc
 */
unsigned Executor::executePThreadJoin(ExecutionState &state, KInstruction *ki, std::vector<ref<Expr> > &arguments) {
	CallInst* calli = dyn_cast<CallInst>(ki->inst);
	if (calli) {
		ref<ConstantExpr> threadIdExpr = dyn_cast<ConstantExpr>(arguments[0]);
//		llvm::errs() << " joinedThreadId : " << threadIdExpr << "\n";
		unsigned threadId = threadIdExpr->getZExtValue();
		Thread* joinThread = state.findThreadById(threadId);
		if (joinThread) {
			if (!joinThread->isTerminated()) {
				std::map<unsigned, std::vector<unsigned> >::iterator ji = state.joinRecord.find(threadId);
				if (ji == state.joinRecord.end()) {
					std::vector<unsigned> blockedList;
					blockedList.push_back(state.currentThread->threadId);
					state.joinRecord.insert(make_pair(threadId, blockedList));
				} else {
					ji->second.push_back(state.currentThread->threadId);
				}
				state.swapOutThread(state.currentThread, false, false, true, false);
				;
			}
		} else {
			assert(0 && "thread not exist!");
		}
	} else {
		assert(0 && "inst must be callInst!");
	}
	return 0;
}

/**
 * execute pthread_cond_wait
 */
unsigned Executor::executePThreadCondWait(ExecutionState &state, KInstruction *ki, std::vector<ref<Expr> > &arguments) {
	ConstantExpr* condAddress = dyn_cast<ConstantExpr>(arguments[0]);
	ConstantExpr* mutexAddress = dyn_cast<ConstantExpr>(arguments[1]);
	if (!mutexAddress) {
		assert(0 && "mutex address is not const");
	}
	if (!condAddress) {
		assert(0 && "cond address is not const");
	}
	std::string condName = Transfer::uint64toString(condAddress->getZExtValue());
	std::string mutexName = Transfer::uint64toString(mutexAddress->getZExtValue());
	std::string errorMsg;
	bool isSuccess = state.condManager.wait(condName, mutexName, state.currentThread->threadId, errorMsg);
	if (isSuccess) {
		state.swapOutThread(state.currentThread, true, false, false, false);
	} else {
		llvm::errs() << errorMsg << "\n";
		assert(0 && "wait error");
	}
	return 0;
}

/**
 * execute pthread_cond_signal
 */
unsigned Executor::executePThreadCondSignal(ExecutionState &state, KInstruction *ki, std::vector<ref<Expr> > &arguments) {
	ConstantExpr* condAddress = dyn_cast<ConstantExpr>(arguments[0]);
	if (!condAddress) {
		assert(0 && "cond address is not const");
	}
	std::string condName = Transfer::uint64toString(condAddress->getZExtValue());
	std::string errorMsg;
	unsigned releasedThreadId;
	bool isSuccess = state.condManager.signal(condName, releasedThreadId, errorMsg);
	if (isSuccess) {
		if (releasedThreadId != 0) {
			state.swapInThread(releasedThreadId, false, true);

			//vector clock : signal
			Thread *thread = state.getCurrentThread();
			Thread *tthread = state.findThreadById(releasedThreadId);
			for (unsigned i = 0; i < tthread->vectorClock.size(); i++) {
				if (tthread->vectorClock[i] < thread->vectorClock[i]) {
					tthread->vectorClock[i] = thread->vectorClock[i];
				}
			}
			thread->vectorClock[thread->threadId]++;

		}
	} else {
		llvm::errs() << errorMsg << "\n";
		assert(0 && "signal failed");
	}
	return 0;
}

/**
 * execute pthread_cond_broadcast
 */
unsigned Executor::executePThreadCondBroadcast(ExecutionState &state, KInstruction *ki, std::vector<ref<Expr> > &arguments) {
	ConstantExpr* condAddress = dyn_cast<ConstantExpr>(arguments[0]);
	if (!condAddress) {
		assert(0 && "cond address is not const");
	}
	std::string condName = Transfer::uint64toString(condAddress->getZExtValue());
	std::vector<unsigned> threadList;
	std::string errorMsg;
	bool isSuccess = state.condManager.broadcast(condName, threadList, errorMsg);
	if (isSuccess) {
		std::vector<unsigned>::iterator ti, te;
		std::vector<bool>::iterator bi;
		for (ti = threadList.begin(), te = threadList.end(); ti != te; ti++, bi++) {
			state.swapInThread(*ti, false, true);

			//vector clock : signal
			Thread *thread = state.getCurrentThread();
			Thread *tthread = state.findThreadById(*ti);
			for (unsigned i = 0; i < tthread->vectorClock.size(); i++) {
				if (tthread->vectorClock[i] < thread->vectorClock[i]) {
					tthread->vectorClock[i] = thread->vectorClock[i];
				}
			}
			thread->vectorClock[thread->threadId]++;

		}
	} else {
		llvm::errs() << errorMsg << "\n";
		assert(0 && "broadcast failed");
	}
	return 0;
}

/**
 * execute pthread_mutex_lock
 */
unsigned Executor::executePThreadMutexLock(ExecutionState &state, KInstruction *ki, std::vector<ref<Expr> > &arguments) {
	ref<Expr> address = arguments[0];
	ConstantExpr* mutexAddress = dyn_cast<ConstantExpr>(address);
	//cerr << " lock param : " << mutexAddress->getZExtValue();
	if (mutexAddress) {
		std::string key = Transfer::uint64toString(mutexAddress->getZExtValue());
		std::string errorMsg;
		bool isBlocked;
		bool isSuccess = state.mutexManager.lock(key, state.currentThread->threadId, isBlocked, errorMsg);
		if (isSuccess) {
			if (isBlocked) {
				state.switchThreadToMutexBlocked(state.currentThread);
			}
		} else {
			llvm::errs() << errorMsg << "\n";
			assert(0 && "lock error!");
		}
	} else {
		assert(0 && "mutex address is not const");
	}
	return 0;
}

/**
 * execute pthread_mutex_unlock
 */
unsigned Executor::executePThreadMutexUnlock(ExecutionState &state, KInstruction *ki, std::vector<ref<Expr> > &arguments) {
	ref<Expr> address = arguments[0];
	ConstantExpr* mutexAddress = dyn_cast<ConstantExpr>(address);
	if (mutexAddress) {
		std::string key = Transfer::uint64toString(mutexAddress->getZExtValue());
		std::string errorMsg;
		bool isSuccess = state.mutexManager.unlock(key, errorMsg);
		if (!isSuccess) {
			llvm::errs() << errorMsg << "\n";
			assert(0 && "unlock error");
		}
	} else {
		assert(0 && "mutex address is not const");
	}
	return 0;
}

/**
 * execute pthread_barrier_init
 */
unsigned Executor::executePThreadBarrierInit(ExecutionState &state, KInstruction *ki, std::vector<ref<Expr> > &arguments) {
	ConstantExpr* barrierAddress = dyn_cast<ConstantExpr>(arguments[0]);
	ConstantExpr* count = dyn_cast<ConstantExpr>(arguments[2]);
	if (!barrierAddress) {
		assert(0 && "barrier address is not const");
	}
	if (!count) {
		assert(0 && "count is not const");
	}
	std::string barrierName = Transfer::uint64toString(barrierAddress->getZExtValue());
	std::string errorMsg;
	bool isSuccess = state.barrierManager.init(barrierName, count->getZExtValue(), errorMsg);
	if (!isSuccess) {
		llvm::errs() << errorMsg << "\n";
		assert(0 && "barrier init error");
	}
	return 0;
}

/**
 * execute pthread_barrier_wait
 */
unsigned Executor::executePThreadBarrierWait(ExecutionState &state, KInstruction *ki, std::vector<ref<Expr> > &arguments) {
	ConstantExpr* barrierAddress = dyn_cast<ConstantExpr>(arguments[0]);
	if (!barrierAddress) {
		assert(0 && "barrier address is not const");
	}
	std::string barrierName = Transfer::uint64toString(barrierAddress->getZExtValue());
	std::vector<unsigned> blockedList;
	bool isReleased = false;
	std::string errorMsg;
	bool isSuccess = state.barrierManager.wait(barrierName, state.currentThread->threadId, isReleased, blockedList, errorMsg);
	if (isSuccess) {
		if (isReleased) {
			//may be a bottleneck as time complexity is O(n*n)
			//ylc
			for (std::vector<unsigned>::iterator ti = blockedList.begin(), te = blockedList.end(); ti != te; ti++) {
				unsigned threadId = *ti;
				if (threadId != state.currentThread->threadId) {
					state.swapInThread(threadId, true, false);
				}
			}
		} else {
			state.swapOutThread(state.currentThread, false, true, false, false);
		}
	} else {
		llvm::errs() << errorMsg << "\n";
		assert("barrier wait error");
	}
	return 0;
}

/**
 * execute pthread_barrier_destory
 */
unsigned Executor::executePThreadBarrierDestory(ExecutionState &state, KInstruction *ki, std::vector<ref<Expr> > &arguments) {
	return 0;
}

/**
 * handle global mutex, condition and barrier
 *
 */
void Executor::handleInitializers(ExecutionState& initialState) {
	for (Module::const_global_iterator i = kmodule->module->global_begin(), e = kmodule->module->global_end(); i != e; ++i) {
		if (i->hasInitializer() && i->getName().str().at(0) != '.') {
//			llvm::errs() << "name : " << i->getName().str() << "\n";
			Type* type = i->getInitializer()->getType();
			ref<ConstantExpr> address = globalAddresses.find(i)->second;
			uint64_t startAddress = address->getZExtValue();
			createSpecialElement(initialState, type, startAddress, true);
		}
	}
}

//创建mutex,cond，barrier时可能会重复创建引发断言错误，待修正！
//ylc
/**
 * find all mutex, cond and barrier behind startAddress
 * type:
 * startAddress:
 */
void Executor::createSpecialElement(ExecutionState& state, Type* type, uint64_t& startAddress, bool isInitializer) {

//	llvm::errs() << "type : " << type->getTypeID() << "\n";
//	llvm::errs() << "startAddress : " << startAddress << "\n";

	switch (type->getTypeID()) {

		case Type::IntegerTyID:
		case Type::FloatTyID:
		case Type::DoubleTyID: {
			DataLayout* layout = kmodule->targetData;
			unsigned alignment = layout->getABITypeAlignment(type);
			if (startAddress % alignment != 0) {
				startAddress = (startAddress / alignment + 1) * alignment;
			}
			startAddress += kmodule->targetData->getTypeSizeInBits(type) / 8;
			break;
		}

		case Type::PointerTyID: {
			DataLayout* layout = kmodule->targetData;
			unsigned alignment = layout->getABITypeAlignment(type);
			if (startAddress % alignment != 0) {
				startAddress = (startAddress / alignment + 1) * alignment;
			}
			startAddress += Context::get().getPointerWidth() / 8;
			break;
		}

		case Type::ArrayTyID: {
			unsigned num = type->getArrayNumElements();
			for (unsigned i = 0; i < num; i++) {
				Type* elementType = type->getArrayElementType();
				createSpecialElement(state, elementType, startAddress, isInitializer);
			}
			break;
		}

		case Type::VectorTyID: {
			unsigned num = type->getVectorNumElements();
			for (unsigned i = 0; i < num; i++) {
				Type* elementType = type->getVectorElementType();
				createSpecialElement(state, elementType, startAddress, isInitializer);
			}
			break;
		}

		case Type::StructTyID: {
			std::string errorMsg;
//			llvm::errs() << "StructName : " << type->getStructName().str() << "\n";
			//下列代码只是为了处理三种特殊结构体的内存对齐，对于复杂对象，其第一个元素在被访问时会计算内存对齐，因此不需要额外对复杂对象计算
			//内存对齐，这里计算结构体的内存对齐只是因为mutex，cond，barrier三种类型不会被解析，因此需要提前计算。
			DataLayout* layout = kmodule->targetData;
			//检查是否是opaque结构体，如果是跳过不做处理
			if (!dyn_cast<StructType>(type)->isOpaque() && !dyn_cast<StructType>(type)->isLiteral()) {
				unsigned alignment = layout->getABITypeAlignment(type);
				if (startAddress % alignment != 0) {
					startAddress = (startAddress / alignment + 1) * alignment;
				}
				if (type->getStructName() == "union.pthread_mutex_t") {
					std::string mutexName = Transfer::uint64toString(startAddress);
					state.mutexManager.addMutex(mutexName, errorMsg);
//					llvm::errs() << "mutexName : " << mutexName << "\n";
					startAddress += kmodule->targetData->getTypeSizeInBits(type) / 8;
				} else if (type->getStructName() == "union.pthread_cond_t") {
					std::string condName = Transfer::uint64toString(startAddress);
					if (prefix) {
						state.condManager.addCondition(condName, errorMsg, prefix);
					} else {
						state.condManager.addCondition(condName, errorMsg);
					}
					startAddress += kmodule->targetData->getTypeSizeInBits(type) / 8;
				} else if (type->getStructName() == "union.pthread_barrier_t") {
					state.barrierManager.addBarrier(Transfer::uint64toString(startAddress), errorMsg);
					startAddress += kmodule->targetData->getTypeSizeInBits(type) / 8;
				} else {
					unsigned num = type->getStructNumElements();
					for (unsigned i = 0; i < num; i++) {
						Type* elementType = type->getStructElementType(i);
						createSpecialElement(state, elementType, startAddress, isInitializer);
					}
				}
			}
			break;
		}

		case Type::FunctionTyID: {
			// only met with function pointer, can we skipping it?
			//ylc
			break;
		}

		default: {
//			assert(0 && "unsupport initializer type");
		}

	}
}

