/*
 * ListenerService.h
 *
 *  Created on: 2015年11月13日
 *      Author: zhy
 */

#ifndef LIB_CORE_LISTENERSERVICE_H_
#define LIB_CORE_LISTENERSERVICE_H_

#include <vector>

#include "../../include/klee/ExecutionState.h"
#include "BitcodeListener.h"
#include "RuntimeDataManager.h"

namespace klee {
	class DTAM;
	class Encode;
} /* namespace klee */

namespace klee {

	class ListenerService {

		private:
			std::vector<BitcodeListener*> bitcodeListeners;
			RuntimeDataManager rdManager;
			Encode *encode;
			DTAM *dtam;
			struct timeval start, finish;
			double cost;

		public:
			ListenerService(Executor* executor);
			~ListenerService();

			void pushListener(BitcodeListener* bitcodeListener);
			void removeListener(BitcodeListener* bitcodeListener);
			void removeListener(BitcodeListener::listenerKind kind);
			void popListener();

			RuntimeDataManager* getRuntimeDataManager();

			void Preparation();
			void beforeRunMethodAsMain(Executor* executor, ExecutionState &state, llvm::Function *f, MemoryObject *argvMO,
					std::vector<ref<Expr> > arguments, int argc, char **argv, char **envp);
			void beforeExecuteInstruction(Executor* executor, ExecutionState &state, KInstruction *ki);
			void afterExecuteInstruction(Executor* executor, ExecutionState &state, KInstruction *ki);
			void afterRunMethodAsMain(ExecutionState &state);
			void executionFailed(ExecutionState &state, KInstruction *ki);

			void startControl(Executor* executor);
			void endControl(Executor* executor);

	};

}

#endif /* LIB_CORE_LISTENERSERVICE_H_ */
