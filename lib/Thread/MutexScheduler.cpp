/*
 * LockScheduler.cpp
 *
 *  Created on: 2015年1月7日
 *      Author: berserker
 */

#include "MutexScheduler.h"
#include <assert.h>

using namespace::std;

namespace klee {

MutexScheduler* getMutexSchedulerByType(MutexScheduler::MutexSchedulerType type) {
	MutexScheduler* scheduler;
	switch (type) {
	case MutexScheduler::FIFS: {
		scheduler = new FIFSMutexScheduler();
		break;
	}

	case MutexScheduler::Preemptive: {
		scheduler = new PreemptiveMutexScheduler();
		break;
	}

	case MutexScheduler::Random: {
		//scheduler = new RandomLockScheduler();
		break;
	}
	default: {
		scheduler = new FIFSMutexScheduler();
	}

	}
	return scheduler;
}

MutexScheduler::MutexScheduler() {
	// TODO Auto-generated constructor stub

}

MutexScheduler::~MutexScheduler() {
	// TODO Auto-generated destructor stub
}

FIFSMutexScheduler::FIFSMutexScheduler() {

}

FIFSMutexScheduler::~FIFSMutexScheduler() {

}

unsigned FIFSMutexScheduler::selectNextItem() {
	unsigned result = queue.front();
	queue.pop_front();
	return result;
}

void FIFSMutexScheduler::popAllItem(vector<unsigned>& allItem) {
	allItem.reserve(queue.size());
	for (list<unsigned>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		allItem.push_back(*ii);
	}
	queue.clear();
}

int FIFSMutexScheduler::itemNum() {
	return queue.size();
}

bool FIFSMutexScheduler::isQueueEmpty() {
	return queue.empty();
}

void FIFSMutexScheduler::addItem(unsigned threadId) {
	queue.push_back(threadId);
}

unsigned FIFSMutexScheduler::removeItem(unsigned threadId) {
	unsigned result = 0;
	for (list<unsigned>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		if (*ii == threadId) {
			result = *ii;
			queue.erase(ii);
			break;
		}
	}
	return result;
}

void FIFSMutexScheduler::printAllItem(ostream &os) {
	for (list<unsigned>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		os << (*ii) << " ";
	}
	os << endl;
}

PreemptiveMutexScheduler::PreemptiveMutexScheduler() {

}

PreemptiveMutexScheduler::~PreemptiveMutexScheduler() {

}

unsigned PreemptiveMutexScheduler::selectNextItem() {
	unsigned result = queue.back();
	queue.pop_back();
	return result;
}

void PreemptiveMutexScheduler::popAllItem(vector<unsigned>& allItem) {
	allItem.reserve(queue.size());
	for (list<unsigned>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		allItem.push_back(*ii);
	}
	queue.clear();
}

int PreemptiveMutexScheduler::itemNum() {
	return queue.size();
}

bool PreemptiveMutexScheduler::isQueueEmpty() {
	return queue.empty();
}

void PreemptiveMutexScheduler::addItem(unsigned threadId) {
	queue.push_back(threadId);
}

unsigned PreemptiveMutexScheduler::removeItem(unsigned threadId) {
	unsigned result = 0;
	for (list<unsigned>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		if (*ii == threadId) {
			result = *ii;
			queue.erase(ii);
			break;
		}
	}
	return result;
}

void PreemptiveMutexScheduler::printAllItem(ostream &os) {
	for (list<unsigned>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		os << (*ii) << " ";
	}
	os << endl;
}

//GuidedMutexScheduler::GuidedMutexScheduler(MutexSchedulerType secondarySchedulerType, vector<unsigned>& threadList)
//	:threadList(threadList),
//	 pos(0) {
//	baseScheduler = getMutexSchedulerByType(secondarySchedulerType);
//}
//
//GuidedMutexScheduler::~GuidedMutexScheduler() {
//	delete baseScheduler;
//}
//
//unsigned GuidedMutexScheduler::selectNextItem() {
//	if (pos == threadList.size()) {
//		return baseScheduler->selectNextItem();
//	} else {
//		unsigned threadId = threadList[pos++];
//		unsigned result = baseScheduler->removeItem(threadId);
//		assert(result);
//		return result;
//	}
//}
//
//void GuidedMutexScheduler::popAllItem(std::vector<unsigned>& allItem) {
//	baseScheduler->popAllItem(allItem);
//}
//
//int GuidedMutexScheduler::itemNum() {
//	return baseScheduler->itemNum();
//}
//
//bool GuidedMutexScheduler::isQueueEmpty() {
//	return baseScheduler->isQueueEmpty();
//}
//
//void GuidedMutexScheduler::addItem(unsigned threadId) {
//	baseScheduler->addItem(threadId);
//}
//
//unsigned GuidedMutexScheduler::removeItem(unsigned threadId) {
//	return baseScheduler->removeItem(threadId);
//}
//
//void GuidedMutexScheduler::printAllItem(ostream &os) {
//	baseScheduler->printAllItem(os);
//}

} /* namespace klee */
