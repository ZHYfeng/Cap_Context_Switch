/*
 * BitcodeListener.h
 *
 *  Created on: May 16, 2014
 *      Author: ylc
 */

#ifndef BITCODELISTENER_H_
#define BITCODELISTENER_H_

#include <map>

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"

namespace klee {

class BitcodeListener {
	public:

		enum listenerKind {
			PSOListenerKind,
			SymbolicListenerKind,
			TaintListenerKind,
			InputListenerKind,
			DebugerListenerKind
		};

		listenerKind Kind;

		virtual ~BitcodeListener();
		virtual void beforeRunMethodAsMain(ExecutionState &initialState) = 0;
		virtual void executeInstruction(ExecutionState &state, KInstruction *ki) = 0;
		virtual void instructionExecuted(ExecutionState &state, KInstruction *ki) = 0;
		virtual void afterRunMethodAsMain() = 0;
		virtual void createThread(ExecutionState &state, Thread* thread) = 0;
		virtual void executionFailed(ExecutionState &state, KInstruction *ki) = 0;

};

}

#endif /* BITCODELISTENER_H_ */
