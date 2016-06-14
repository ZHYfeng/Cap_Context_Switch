/*
 * BarrierManager.h
 *
 *  Created on: 2014年10月10日
 *      Author: berserker
 */

#ifndef BARRIERMANAGER_H_
#define BARRIERMANAGER_H_

#include <map>
#include "Barrier.h"

namespace klee {

class BarrierManager {
private:
	std::map<std::string, Barrier*> barrierPool;

public:
	BarrierManager();
	virtual ~BarrierManager();
	bool init(std::string barrierName, unsigned count, std::string& errorMsg);
	bool wait(std::string barrierName, unsigned threadId, bool& isReleased, std::vector<unsigned>& blockedList, std::string& errorMsg);
	bool addBarrier(std::string barrierName, std::string& errorMsg);
	Barrier* getBarrier(std::string barrierName);
	void clear();
	void print(std::ostream &out);
};

} /* namespace klee */

#endif /* BARRIERMANAGER_H_ */
