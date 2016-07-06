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
#include "PSOListener.h"
#include "SymbolicListener.h"
#include "TaintListener.h"

namespace klee {

	ListenerService::ListenerService(Executor* executor) {
		encode = NULL;
		dtam = NULL;
		cost = 0;

	}

	ListenerService::~ListenerService() {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			bitcodeListeners.erase(bit);
		}
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

	void ListenerService::removeListener(BitcodeListener::listenerKind kind) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			if ((*bit)->kind == kind) {
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

	void ListenerService::beforeExecuteInstruction(ExecutionState &state, KInstruction *ki) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->beforeExecuteInstruction(state, ki);
		}
	}

	void ListenerService::afterExecuteInstruction(ExecutionState &state, KInstruction *ki) {
		for (std::vector<BitcodeListener*>::iterator bit = bitcodeListeners.begin(), bie = bitcodeListeners.end(); bit != bie; ++bit) {
			(*bit)->afterExecuteInstruction(state, ki);
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

	void ListenerService::executeInstruction(ExecutionState &state, KInstruction *ki) {

	}

	void ListenerService::startControl(Executor* executor) {

		executor->executionNum++;

		BitcodeListener* PSOlistener = new PSOListener(executor, &rdManager);
		pushListener(PSOlistener);
		BitcodeListener* Symboliclistener = new SymbolicListener(executor, &rdManager);
		pushListener(Symboliclistener);
		BitcodeListener* Taintlistener = new TaintListener(executor, &rdManager);
		pushListener(Taintlistener);

		gettimeofday(&start, NULL);

	}

	void ListenerService::endControl(Executor* executor) {

		gettimeofday(&finish, NULL);
		cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
		rdManager.runningCost += cost;

		gettimeofday(&start, NULL);
		encode = new Encode(&rdManager);
		encode->buildifAndassert();
		if (encode->verify()) {
			encode->check_if();
		}
		gettimeofday(&finish, NULL);
		cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
		rdManager.solvingCost += cost;

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

		delete encode;
		delete dtam;

	}

}

#endif
