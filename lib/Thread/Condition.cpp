/*
 * Condition.cpp
 *
 *  Created on: Jul 24, 2014
 *      Author: klee
 */

#include "Condition.h"

using namespace::std;

namespace klee {

Condition::Condition() {
	// TODO Auto-generated constructor stub

}

Condition::Condition(unsigned id, string name, CondScheduler::CondSchedulerType scheduleType)
	: id(id),
	  name(name){
	waitingList = getCondSchedulerByType(scheduleType);
	//associatedMutex == NULL;
}

Condition::Condition(unsigned id, string name, CondScheduler::CondSchedulerType schedulerType, Prefix* prefix)
	: id(id),
	  name(name) {
	waitingList = new GuidedCondScheduler(schedulerType, prefix);
}

void Condition::wait(WaitParam* waitParam) {
	waitingList->addItem(waitParam);
}

WaitParam* Condition::signal() {
	if (waitingList->isQueueEmpty()) {
		return NULL;
	} else {
		WaitParam* wp = waitingList->selectNextItem();
		return wp;
	}
}

void Condition::broadcast(vector<WaitParam*>& allWait) {
	waitingList->popAllItem(allWait);
}

Condition::~Condition() {
	// TODO Auto-generated destructor stub
	//release unsignal thread
	if (!waitingList->isQueueEmpty()) {
		vector<WaitParam*> allItem;
		waitingList->popAllItem(allItem);
		for (vector<WaitParam*>::iterator wi = allItem.begin(), we = allItem.end(); wi != we; wi++) {
			delete *wi;
		}
	}
	delete this->waitingList;
}

}
