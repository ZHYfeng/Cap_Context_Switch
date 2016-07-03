//===-- ExecutionState.h ----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXECUTIONSTATE_H
#define KLEE_EXECUTIONSTATE_H

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../../lib/Core/AddressSpace.h"
#include "../../lib/Core/Memory.h"
#include "../../lib/Thread/BarrierManager.h"
#include "../../lib/Thread/CondManager.h"
#include "../../lib/Thread/MutexManager.h"
#include "../../lib/Thread/StackFrame.h"
#include "../../lib/Thread/ThreadList.h"
#include "Constraints.h"
#include "Expr.h"
#include "Internal/ADT/TreeStream.h"
#include "Internal/Module/KInstIterator.h"
#include "util/Ref.h"

namespace klee {
class ThreadScheduler;
} /* namespace klee */

namespace klee {
class Array;
class CallPathNode;
struct Cell;
struct KFunction;
struct KInstruction;
class MemoryObject;
class PTreeNode;
struct InstructionInfo;

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const MemoryMap &mm);

/// @brief ExecutionState representing a path under exploration
class ExecutionState {
public:
	typedef std::vector<StackFrame> stack_ty;

private:
	// unsupported, use copy constructor
	ExecutionState &operator=(const ExecutionState &);

	std::map<std::string, std::string> fnAliases;

public:
	// Execution - Control Flow specific

	/// @brief Address space used by this state (e.g. Global and Heap)
	AddressSpace addressSpace;

	/// @brief Constraints collected so far
	ConstraintManager constraints;

	/// Statistics and information

	/// @brief Costs for all queries issued for this state, in seconds
	mutable double queryCost;

	/// @brief Weight assigned for importance of this state.  Can be
	/// used for searchers to decide what paths to explore
	double weight;

	/// @brief Exploration depth, i.e., number of times KLEE branched for this state
	unsigned depth;

	/// @brief History of complete path: represents branches taken to
	/// reach/create this state (both concrete and symbolic)
	TreeOStream pathOS;

	/// @brief History of symbolic path: represents symbolic branches
	/// taken to reach/create this state
	TreeOStream symPathOS;

	/// @brief Counts how many instructions were executed since the last new
	/// instruction was covered.
	unsigned instsSinceCovNew;

	/// @brief Whether a new instruction was covered in this state
	bool coveredNew;

	/// @brief Disables forking for this state. Set by user code
	bool forkDisabled;

	/// @brief Set containing which lines in which files are covered by this state
	std::map<const std::string *, std::set<unsigned> > coveredLines;

	/// @brief Pointer to the process tree of the current state
	PTreeNode *ptreeNode;

	/// @brief Ordered list of symbolics: used to generate test cases.
	//
	// FIXME: Move to a shared list structure (not critical).
	std::vector<std::pair<const MemoryObject *, const Array *> > symbolics;

	/// @brief Set of used array names for this state.  Used to avoid collisions.
	std::set<std::string> arrayNames;



	std::string getFnAlias(std::string fn);
	void addFnAlias(std::string old_fn, std::string new_fn);
	void removeFnAlias(std::string fn);

	/// @zhy use for multiple stack, this points to current stack, maybe from thread or listener.
	StackType *currentStack;

	unsigned nextThreadId;
	ThreadScheduler* threadScheduler;
	ThreadList threadList;
	Thread* currentThread;

	MutexManager mutexManager;
	CondManager condManager;
	BarrierManager barrierManager;
	std::map<unsigned, std::vector<unsigned> > joinRecord;

public:
	ExecutionState(KFunction *kf);

	ExecutionState(KFunction *kf, Prefix* prefix);

	// XXX total hack, just used to make a state so solver can
	// use on structure
	ExecutionState(const std::vector<ref<Expr> > &assumptions);

	ExecutionState(const ExecutionState &state);

	~ExecutionState();

	ExecutionState *branch();

	void addSymbolic(const MemoryObject *mo, const Array *array);
	void addConstraint(ref<Expr> e) {
		constraints.addConstraint(e);
	}
	bool merge(const ExecutionState &b);

	Thread* findThreadById(unsigned threadId);

	Thread* getNextThread();

	Thread* getCurrentThread();

	bool examineAllThreadFinalState();

	unsigned getNextThreadId();

	Thread* createThread(KFunction *kf);

	Thread* createThread(KFunction *kf, unsigned threadId);

	void swapOutThread(Thread* thread, bool isCondBlocked,
			bool isBarrierBlocked, bool isJoinBlocked, bool isTerminated);

	void swapInThread(Thread* thread, bool isRunnable, bool isMutexBlocked);

	void swapOutThread(unsigned threadId, bool isCondBlocked,
			bool isBarrierBlocked, bool isJoinBlocked, bool isTerminated);

	void swapInThread(unsigned threadId, bool isRunnable, bool isMutexBlocked);

	void switchThreadToMutexBlocked(Thread* thread);

	void switchThreadToMutexBlocked(unsigned threadId);

	void switchThreadToRunnable(Thread* thread);

	void switchThreadToRunnable(unsigned threadId);

	void reSchedule();
};
}

#endif
