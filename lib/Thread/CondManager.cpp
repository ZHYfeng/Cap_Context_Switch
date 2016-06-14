/*
 * CondManager.cpp
 *
 *  Created on: Jul 24, 2014
 *      Author: ylc
 */

#include "CondManager.h"

#include <iterator>
#include <utility>

#include "CondScheduler.h"
#include "Mutex.h"
#include "Transfer.h"
#include "WaitParam.h"

using namespace::std;

namespace klee {

bool CondManager::wait(string condName, string mutexName, unsigned threadId, string& errorMsg) {
	Mutex* mutex = mutexManager->getMutex(mutexName);
	if (mutex == NULL) {
		errorMsg = "mutex " + mutexName + " undefined";
		return false;
	}
	Condition* cond = getCondition(condName);
	if (cond == NULL) {
		errorMsg = "condition " + condName + " undefined";
		return false;
	}
	if (!mutex->isThreadOwnMutex(threadId)) {
		errorMsg = Transfer::uint64toString(threadId) + " does not own mutex " + mutexName;
		return false;
	} else {
		WaitParam* wp = new WaitParam(mutexName, threadId);
		cond->wait(wp);
		return mutexManager->unlock(mutexName, errorMsg);
	}
}

bool CondManager::signal(string condName, unsigned& releasedThreadId, string& errorMsg) {
	Condition* cond = getCondition(condName);
	if (cond == NULL) {
		errorMsg = "condition " + condName + " undefined";
		return false;
	} else {
		WaitParam* wp = cond->signal();
		if (wp != NULL) {
			releasedThreadId = wp->threadId;
			//change state
			mutexManager->addBlockedThread(wp->threadId, wp->mutexName);
			delete wp;
		} else {
			releasedThreadId = 0;
		}
		return true;
	}
}

bool CondManager::broadcast(string condName, vector<unsigned>& threads, string& errorMsg) {
	Condition* cond = getCondition(condName);
	if (cond == NULL) {
		errorMsg = "condition " + condName + " undefined";
		return false;
	} else {
		vector<WaitParam*> itemList;
		cond->broadcast(itemList);
		threads.resize(itemList.size());
		vector<unsigned>::iterator ti = threads.begin();
		for (vector<WaitParam*>::iterator wi = itemList.begin(), we = itemList.end(); wi != we; wi++, ti++) {
			WaitParam* wp = *wi;
			*ti = wp->threadId;
			mutexManager->addBlockedThread(wp->threadId, wp->mutexName);
			delete wp;
		}
		return true;
	}
}

bool CondManager::addCondition(string condName, string& errorMsg) {
	Condition* cond = getCondition(condName);
	if (cond) {
		errorMsg = "redefinition of condition " + condName;
		return false;
	} else {
		cond = new Condition(nextConditionId++, condName, CondScheduler::FIFS);
		//Condition* cond = new Condition(condName, Condition::RANDOM);
		pair<string, Condition*> item(condName, cond);
		condPool.insert(item);
		return true;
	}
}

bool CondManager::addCondition(string condName, string& errorMsg, Prefix* prefix) {
	Condition* cond = getCondition(condName);
	if (cond) {
		errorMsg = "redefinition of condition " + condName;
		return false;
	} else {
		cond = new Condition(nextConditionId++, condName, CondScheduler::FIFS, prefix);
		//Condition* cond = new Condition(condName, Condition::RANDOM);
		pair<string, Condition*> item(condName, cond);
		condPool.insert(item);
		return true;
	}
}

Condition* CondManager::getCondition(string condName) {
	map<string, Condition*>::iterator ci = condPool.find(condName);
	if (ci == condPool.end()) {
		return NULL;
	} else {
		return ci->second;
	}
}

CondManager::CondManager()
	: nextConditionId(1) {
	this->mutexManager = NULL;
}

CondManager::CondManager(MutexManager* _mutexManaget) : nextConditionId(1) {
	this->mutexManager = _mutexManaget;
}

CondManager::~CondManager() {
	clear();
}

void CondManager::clear() {
	for (map<string, Condition*>::iterator ci = condPool.begin(), ce = condPool.end(); ci != ce; ci++) {
		delete ci->second;
	}
	condPool.clear();
	nextConditionId = 1;
}

void CondManager::print(ostream &out) {
	out << "condition pool\n";
	for (map<string, Condition*>::iterator ci = condPool.begin(), ce = condPool.end(); ci != ce; ci++) {
		out << ci->first << endl;
	}
}

unsigned CondManager::getNextConditionId() {
	return nextConditionId;
}

}
