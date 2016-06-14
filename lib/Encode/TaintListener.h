/*
 * TaintListener.h
 *
 *  Created on: 2016年2月17日
 *      Author: 11297
 */

#ifndef LIB_CORE_TAINTLISTENER_H_
#define LIB_CORE_TAINTLISTENER_H_

#include "Executor.h"
#include "RuntimeDataManager.h"
#include "BitcodeListener.h"
#include "FilterSymbolicExpr.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/ExecutionState.h"

namespace llvm {
class Type;
class Constant;
}

namespace klee {

class TaintListener: public BitcodeListener {
public:
	TaintListener(Executor* executor, RuntimeDataManager* rdManager);
	~TaintListener();

	void beforeRunMethodAsMain(ExecutionState &initialState);
	void executeInstruction(ExecutionState &state, KInstruction *ki);
	void instructionExecuted(ExecutionState &state, KInstruction *ki);
	void afterRunMethodAsMain();
	void createThread(ExecutionState &state, Thread* thread);
	void executionFailed(ExecutionState &state, KInstruction *ki);

private:
	Executor* executor;
	RuntimeDataManager* runtimeData;
	FilterSymbolicExpr filter;
	std::vector<Event*>::iterator currentEvent, endEvent;
	//此Map更新有两处，Load、某些函数。
	std::map<ref<Expr>, ref<Expr> > addressSymbolicMap;
	std::map<std::string, ref<Expr> > symbolicMap;
	AddressSpace addressSpace;
	std::map<unsigned, StackType *> stack;

private:

	//add by hy
	ref<Expr> manualMakeTaintSymbolic(ExecutionState& state, std::string name, unsigned size);
	void manualMakeTaint(ref<Expr> value, bool isTaint);
	ref<Expr> readExpr(ExecutionState &state, ref<Expr> address, Expr::Width size);

};

}



#endif /* LIB_CORE_TAINTLISTENER_H_ */
