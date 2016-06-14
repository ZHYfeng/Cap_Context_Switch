/*
 * MutexManager.cpp
 *
 *  Created on: Jul 24, 2014
 *      Author: klee
 */

#include "MutexManager.h"
#include "Transfer.h"

#include <iostream>

using namespace::std;

namespace klee {

MutexManager::MutexManager()
	: nextMutexId(1) {
	// TODO Auto-generated constructor stub

}

MutexManager::~MutexManager() {
	// TODO Auto-generated destructor stub
	clear();
}

bool MutexManager::lock(string mutexName, unsigned threadId, bool& isBlocked, string& errorMsg) {
	Mutex* mutex = getMutex(mutexName);
	if (!mutex) {
		errorMsg = "mutex " + mutexName + " undefined";
		return false;
	} else {
		return lock(mutex, threadId, isBlocked, errorMsg);
	}
}

bool MutexManager::lock(Mutex* mutex, unsigned threadId, bool& isBlocked, string& errorMsg) {
	if (mutex->isMutexLocked()) {
		//mutex->addToBlockedList(thread);
		blockedThreadPool.insert(make_pair(threadId, mutex));
		isBlocked = true;
		return true;
	} else {
		mutex->lock(threadId);
		isBlocked = false;
		map<unsigned, Mutex*>::iterator ti = blockedThreadPool.find(threadId);
		if (ti != blockedThreadPool.end()) {
			blockedThreadPool.erase(ti);
			//mutex->removeFromBlockedList(thread);
		}
		return true;
	}
}

bool MutexManager::unlock(string mutexName, string& errorMsg) {
	Mutex* mutex = getMutex(mutexName);
	if (!mutex) {
		errorMsg = "mutex " + mutexName + " undefined";
		return false;
	} else {
		return unlock(mutex, errorMsg);
	}
}

bool MutexManager::unlock(Mutex* mutex, string& errorMsg) {
	if (!mutex->isMutexLocked()) {
		//errorMsg = "mutex " + mutex->name + " has not been locked";
		//return false;
		// this is not an error. Calling unlock without any lock is ok.
		cerr << "warning: mutex " + mutex->name + " there isn't any lock before this unlock\n";
		return true;
	} else {
		mutex->unlock();
		return true;
	}
}


bool MutexManager::addMutex(string mutexName, string& errorMsg) {
	Mutex* mutex = getMutex(mutexName);
	if (mutex) {
		errorMsg = "redefinition of mutex " + mutexName;
		return false;
	} else {
		mutex = new Mutex(nextMutexId++, mutexName);
		mutexPool.insert(make_pair(mutexName, mutex));
		return true;
	}
}

Mutex* MutexManager::getMutex(string mutexName) {
	map<string, Mutex*>::iterator mi = mutexPool.find(mutexName);
	if (mi == mutexPool.end()) {
		return NULL;
	} else {
		return mi->second;
	}
}

void MutexManager::clear() {
	for (map<string, Mutex*>::iterator mi = mutexPool.begin(), me = mutexPool.end(); mi != me; mi++) {
		delete mi->second;
	}
	mutexPool.clear();
	blockedThreadPool.clear();
	nextMutexId = 1;
}

void MutexManager::print(ostream &out) {
	out << "mutex pool\n";
	for (map<string, Mutex*>::iterator mi = mutexPool.begin(), me = mutexPool.end(); mi != me; mi++) {
		out << mi->first << endl;
	}
}

unsigned MutexManager::getNextMutexId() {
	return nextMutexId;
}

void MutexManager::addBlockedThread(unsigned threadId, std::string mutexName) {
	Mutex* mutex = getMutex(mutexName);
	assert(mutex);
	blockedThreadPool.insert(make_pair(threadId, mutex));
}

bool MutexManager::tryToLockForBlockedThread(unsigned threadId, bool& isBlocked, string& errorMsg) {
	map<unsigned, Mutex*>::iterator mi = blockedThreadPool.find(threadId);
	if (mi == blockedThreadPool.end()) {
		errorMsg = "thread " + Transfer::uint64toString(threadId) + " does not blocked for mutex";
		return false;
	} else {
		Mutex* mutex = mi->second;
		return lock(mutex, threadId, isBlocked, errorMsg);
	}
}

}
