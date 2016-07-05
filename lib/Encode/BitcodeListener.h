/*
 * BitcodeListener.h
 *
 *  Created on: May 16, 2014
 *      Author: ylc
 */

#ifndef BITCODELISTENER_H_
#define BITCODELISTENER_H_

#include "klee/ExecutionState.h"

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
		virtual void beforeExecuteInstruction(ExecutionState &state, KInstruction *ki) = 0;
		virtual void afterExecuteInstruction(ExecutionState &state, KInstruction *ki) = 0;
		virtual void afterRunMethodAsMain() = 0;
		virtual void executionFailed(ExecutionState &state, KInstruction *ki) = 0;

};

}

#endif /* BITCODELISTENER_H_ */
