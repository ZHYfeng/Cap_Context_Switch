/*
 * BitcodeListener.h
 *
 *  Created on: May 16, 2014
 *      Author: ylc
 */

#ifndef BITCODELISTENER_H_
#define BITCODELISTENER_H_

#include "klee/ExecutionState.h"
#include "../Core/AddressSpace.h"
#include "../Encode/RuntimeDataManager.h"
#include "../Thread/StackType.h"

#define PTR 0
#define DEBUG_RUNTIME 0
#define BIT_WIDTH 64

namespace klee {

class BitcodeListener {
	public:

		enum listenerKind {
			defaultKind,
			PSOListenerKind,
			SymbolicListenerKind,
			TaintListenerKind,
			InputListenerKind,
			DebugerListenerKind
		};

		BitcodeListener(RuntimeDataManager* rdManager);
		virtual ~BitcodeListener();

		listenerKind kind;

		RuntimeDataManager* rdManager;

//		AddressSpace addressSpace;
//		std::map<unsigned, StackType*> stack;

//		std::vector<ref<Expr> > arguments;

		virtual void beforeRunMethodAsMain(ExecutionState &state) = 0;
		virtual void beforeExecuteInstruction(ExecutionState &state, KInstruction *ki) = 0;
		virtual void afterExecuteInstruction(ExecutionState &state, KInstruction *ki) = 0;
		virtual void afterRunMethodAsMain(ExecutionState &state) = 0;
		virtual void executionFailed(ExecutionState &state, KInstruction *ki) = 0;



};

}

#endif /* BITCODELISTENER_H_ */
