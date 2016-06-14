/*
 * Barrier.cpp
 *
 *  Created on: 2014年10月10日
 *      Author: berserker
 */

#include "Barrier.h"

using namespace::std;

namespace klee {

Barrier::Barrier(string name, unsigned count)
	: name(name),
	  count(count), 
	  current(0) {
}

Barrier::~Barrier() {
}

void Barrier::wait(unsigned threadId) {
	this->current++;
	blockedList.push_back(threadId);
}

void Barrier::reset() {
	this->current = 0;
	blockedList.clear();
}

bool Barrier::isFull() {
	if (this->current == this->count) {
		return true;
	} else {
		return false;
	}
}

void Barrier::setCount(unsigned count) {
	this->count = count;
}

vector<unsigned>& Barrier::getBlockedList() {
	return blockedList;
}

} /* namespace klee */
