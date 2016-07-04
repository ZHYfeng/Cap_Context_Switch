/*
 * Condition.h
 *
 *  Created on: Jul 24, 2014
 *      Author: klee
 */

#ifndef CONDITION_H_
#define CONDITION_H_

#include <string>
#include <vector>

#include "CondScheduler.h"
#include "WaitParam.h"


namespace klee {

class Condition {
private:
	CondScheduler *waitingList;
	//Mutex* associatedMutex;

public:
	unsigned id;
	std::string name;

public:

	Condition();

	Condition(unsigned id, std::string name, CondScheduler::CondSchedulerType schedulerType);

	Condition(unsigned id, std::string name, CondScheduler::CondSchedulerType schedulerType, Prefix* prefix);

	void wait(WaitParam* waitParam);

	WaitParam* signal();

	void broadcast(std::vector<WaitParam*>& allWait);

	virtual ~Condition();
};

}
#endif /* CONDITION_H_ */
