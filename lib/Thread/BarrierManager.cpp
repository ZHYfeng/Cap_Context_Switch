/*
 * BarrierManager.cpp
 *
 *  Created on: 2014年10月10日
 *      Author: berserker
 */

#include "BarrierManager.h"

#include <iostream>

using namespace::std;

namespace klee {

BarrierManager::BarrierManager() {

}

BarrierManager::~BarrierManager() {
	clear();
}

bool BarrierManager::addBarrier(string barrierName, string& errorMsg) {
	map<string, Barrier*>::iterator bi = barrierPool.find(barrierName);
	if (bi != barrierPool.end()) {
		errorMsg = "redefinition of barrier " + barrierName;
		return false;
	} else {
		Barrier* barrier = new Barrier(barrierName, Barrier::DEFAULTCOUNT);
		barrierPool.insert(make_pair(barrierName, barrier));
		return true;
	}
}

Barrier* BarrierManager::getBarrier(string barrierName) {
	map<string, Barrier*>::iterator bi = barrierPool.find(barrierName);
	if (bi != barrierPool.end()) {
		return bi->second;
	} else {
		return NULL;
	}
}

bool BarrierManager::init(string barrierName, unsigned count, string& errorMsg) {
	Barrier* barrier = getBarrier(barrierName);
	if (barrier == NULL) {
		errorMsg = "barrier " + barrierName + " undefined";
		return false;
	} else {
		barrier->setCount(count);
		return true;
	}
}

bool BarrierManager::wait(std::string barrierName, unsigned threadId, bool& isReleased, vector<unsigned>& blockedList, std::string& errorMsg) {
	Barrier* barrier = getBarrier(barrierName);
	if (barrier == NULL) {
		errorMsg = "barrier " + barrierName + " undefined";
		return false;
	} else {
		barrier->wait(threadId);
		if (barrier->isFull()) {
			isReleased = true;
			blockedList = barrier->getBlockedList();
			barrier->reset();
		} else {
			isReleased = false;
		}
		return true;
	}
}

void BarrierManager::clear() {
	for (map<string, Barrier*>::iterator bi = barrierPool.begin(), be = barrierPool.end(); bi != be; bi++) {
		delete bi->second;
	}
	barrierPool.clear();
}

void BarrierManager::print(ostream &out) {
	out << "barrier pool\n";
	for (map<string, Barrier*>::iterator bi = barrierPool.begin(), be = barrierPool.end(); bi != be; bi++) {
		out << bi->first << endl;
	}
}

}
/* namespace klee */
