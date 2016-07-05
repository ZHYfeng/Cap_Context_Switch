/*
 * ListenerService.cpp
 *
 *  Created on: 2015年11月13日
 *      Author: zhy
 */

#ifndef LIB_CORE_LISTENERSERVICE_C_
#define LIB_CORE_LISTENERSERVICE_C_

#include "ListenerService.h"

#include <stddef.h>
#include <sys/time.h>
#include <iterator>

#include "../Core/Executor.h"
#include "DTAM.h"
#include "Encode.h"
#include "Prefix.h"
#include "SymbolicListener.h"
#include "TaintListener.h"

namespace klee {

	ListenerService::ListenerService(Executor* executor) {
		encode = NULL;
		dtam = NULL;
		runState = 0;
		cost = 0;

	}

	void ListenerService::pushListener(BitcodeListener* bitcodeListener) {
		bitcodeListeners.push_back(bitcodeListener);
	}

	void ListenerService::removeListener(BitcodeListener* bitcodeListener) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			if ((*bit) == bitcodeListener) {
				bitcodeListeners.erase(bit);
				break;
			}
		}
	}

	void ListenerService::popListener() {
		bitcodeListeners.pop_back();
	}

	RuntimeDataManager* ListenerService::getRuntimeDataManager() {
		return &rdManager;
	}

	void ListenerService::beforeRunMethodAsMain(ExecutionState &initialState) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->beforeRunMethodAsMain(initialState);
		}
	}

	void ListenerService::executeInstruction(ExecutionState &state, KInstruction *ki) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->executeInstruction(state, ki);
		}
	}

	void ListenerService::instructionExecuted(ExecutionState &state, KInstruction *ki) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->instructionExecuted(state, ki);
		}
	}

	void ListenerService::afterRunMethodAsMain() {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->afterRunMethodAsMain();
		}
	}

	void ListenerService::executionFailed(ExecutionState &state, KInstruction *ki) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->executionFailed(state, ki);
		}
	}

	void ListenerService::createThread(ExecutionState &state, Thread* thread) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->createThread(state, thread);
		}
	}

	void ListenerService::startControl(Executor* executor) {
		runState = rdManager.runState;
		switch (runState) {
			case 0: {
//		BitcodeListener* PSOlistener = new PSOListener(executor, &rdManager);
//		pushListener(PSOlistener);
				executor->executionNum++;
				gettimeofday(&start, NULL);
				break;
			}
			case 1: {
				BitcodeListener* Symboliclistener = new SymbolicListener(executor, &rdManager);
				pushListener(Symboliclistener);
				rdManager.runState = 2;
				if (executor->prefix) {
					executor->prefix->reuse();
				}
				gettimeofday(&start, NULL);
				break;
			}
			case 2: {
				BitcodeListener* Taintlistener = new TaintListener(executor, &rdManager);
				pushListener(Taintlistener);
				rdManager.runState = 0;
				if (executor->prefix) {
					executor->prefix->reuse();
				}
				gettimeofday(&start, NULL);
				break;
			}
			case 3: {
				break;
			}
			default: {
				break;
			}
		}
	}

	void ListenerService::endControl(Executor* executor) {
		switch (runState) {
			case 0: {
				popListener();
				gettimeofday(&finish, NULL);
				cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
				rdManager.runningCost += cost;
				break;
			}
			case 1: {
				popListener();
				encode = new Encode(&rdManager);
				encode->buildifAndassert();
				if (encode->verify()) {
					encode->check_if();
				}
				gettimeofday(&finish, NULL);
				cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
				rdManager.solvingCost += cost;
				break;
			}
			case 2: {
				popListener();
				gettimeofday(&finish, NULL);
				cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
				rdManager.TaintCost += cost;
				rdManager.allTaintCost.push_back(cost);

				gettimeofday(&start, NULL);
				dtam = new DTAM(&rdManager);
				dtam->dtam();
				gettimeofday(&finish, NULL);
				cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
				rdManager.DTAMCost += cost;
				rdManager.allDTAMCost.push_back(cost);

				gettimeofday(&start, NULL);
				encode->PTS();
				gettimeofday(&finish, NULL);
				cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
				rdManager.PTSCost += cost;
				rdManager.allPTSCost.push_back(cost);

				executor->getNewPrefix();
				break;
			}
			default: {
				break;
			}
		}

	}

}

#endif
