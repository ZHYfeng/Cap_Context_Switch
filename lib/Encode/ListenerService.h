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
			unsigned runState;
			struct timeval start, finish;
			double cost;

		public:
			ListenerService(Executor* executor);
			~ListenerService() {
			}
			void pushListener(BitcodeListener* bitcodeListener);
			void removeListener(BitcodeListener* bitcodeListener);
			void popListener();

			RuntimeDataManager* getRuntimeDataManager();

			void Preparation();
			void beforeRunMethodAsMain(ExecutionState &initialState);
			void beforeExecuteInstruction(ExecutionState &state, KInstruction *ki);
			void afterExecuteInstruction(ExecutionState &state, KInstruction *ki);
			void afterRunMethodAsMain();
			void executionFailed(ExecutionState &state, KInstruction *ki);

			void executeInstruction(ExecutionState &state, KInstruction *ki);

			void startControl(Executor* executor);
			void endControl(Executor* executor);

	};

}

#endif /* LIB_CORE_LISTENERSERVICE_H_ */
