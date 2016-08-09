/*
 * Trace.cpp
 *
 *  Created on: 2015年1月30日
 *      Author: berserker
 */

#include "Trace.h"

#include "Transfer.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/TypeBuilder.h"

#include <iostream>

using namespace std;
using namespace llvm;

namespace klee {

	Trace::Trace() :
			Id(0), nextEventId(0), eventList(20), isUntested(true) {

	}

	Trace::~Trace() {
		for (vector<Event*>::iterator ti = path.begin(), te = path.end(); ti != te; ti++) {
			delete *ti;
		}
		for (map<string, vector<LockPair *> >::iterator li = all_lock_unlock.begin(), le = all_lock_unlock.end(); li != le; li++) {
			for (vector<LockPair *>::iterator ei = li->second.begin(), ee = li->second.end(); ei != ee; ei++) {
				delete *ei;
			}
		}
		for (map<string, vector<Wait_Lock *> >::iterator wi = all_wait.begin(), we = all_wait.end(); wi != we; wi++) {
			for (vector<Wait_Lock *>::iterator ei = wi->second.begin(), ee = wi->second.end(); ei != ee; ei++) {
				delete *ei;
			}
		}
//overlook some event, alloca event only exist in trace!
//	for (vector<vector<Event*>*>::iterator evi = eventList.begin(), eve =
//			eventList.end(); evi != eve; evi++) {
//		if (*evi) {
//			for (vector<Event*>::iterator ei = (*evi)->begin(), ee =
//					(*evi)->end(); ei != ee; ei++) {
//				delete *ei;
//			}
//			delete *evi;
//		}
//	}

//	for (map<string, Constant*>::iterator pi = printf_variable_value.begin(), pe = printf_variable_value.end(); pi != pe; pi++) {
//		if (pi->second != NULL) {
//			delete pi->second;
//		}
//	}
	}

	void Trace::print(bool file) {
		//output to file
		if (file) {
			stringstream ss;
			ss << "./output_info/trace_" << Id << ".data";
			string ErrorInfo;
			raw_fd_ostream out(ss.str().c_str(), ErrorInfo, sys::fs::F_Append);
			printAllEvent(out);
			printThreadCreateAndJoin(out);
			printReadSetAndWriteSet(out);
			printWaitAndSignal(out);
			printLockAndUnlock(out);
			printBarrierOperation(out);
			printPrintfParam(out);
			printGlobalVariableInitializer(out);
			printGlobalVariableLast(out);
		} else { // output to terminal
			printAllEvent(errs());
			printThreadCreateAndJoin(errs());
			printWaitAndSignal(errs());
			printReadSetAndWriteSet(errs());
			printLockAndUnlock(errs());
			printBarrierOperation(errs());
			printPrintfParam(errs());
			printGlobalVariableInitializer(errs());
			printGlobalVariableLast(errs());
		}

	}

	void Trace::printAllEvent(raw_ostream& out) {
		int threadId = 0;
		for (vector<vector<Event*>*>::iterator evi = eventList.begin(), eve = eventList.end(); evi != eve; evi++) {
			vector<Event*>* item = *evi;
			if (item) {
				out << "/*********Thread: " << threadId << "**********/\n";
				for (vector<Event*>::iterator ei = item->begin(), ee = item->end(); ei != ee; ei++) {
					Event* event = *ei;
					out << event->toString() << "\n";
				}
			}
			threadId++;
		}
	}

	void Trace::printThreadCreateAndJoin(raw_ostream& out) {
		out << "<----Thread Create---->\n";
		for (map<Event*, uint64_t>::iterator ci = createThreadPoint.begin(), ce = createThreadPoint.end(); ci != ce; ci++) {
			out << ci->first->eventName << " create thread " << ci->second << "\n";
		}

		out << "<----Thread Join---->\n";
		for (map<Event*, uint64_t>::iterator ji = joinThreadPoint.begin(), je = joinThreadPoint.end(); ji != je; ji++) {
			out << ji->first->eventName << " create join " << ji->second << "\n";
		}
	}

	void Trace::printWaitAndSignal(raw_ostream& out) {
		out << "<----Thread Wait---->\n";
		for (map<string, vector<Wait_Lock *> >::iterator wi = all_wait.begin(), we = all_wait.end(); wi != we; wi++) {
			out << wi->first << " wait at \n";
			for (vector<Wait_Lock*>::iterator vi = wi->second.begin(), ve = wi->second.end(); vi != ve; vi++) {
				out << (*vi)->wait->toString() << "\n";
			}
		}
		out << "<----Thread Signal---->\n";
		for (map<string, vector<Event *> >::iterator si = all_signal.begin(), se = all_signal.end(); si != se; si++) {
			out << si->first << " signal at \n";
			for (vector<Event*>::iterator vi = si->second.begin(), ve = si->second.end(); vi != ve; vi++) {
				out << (*vi)->toString() << "\n";
			}
		}
	}

	void Trace::printReadSetAndWriteSet(raw_ostream& out) {
		out << "<----ReadSet---->\n";
		for (map<string, vector<Event *> >::iterator ri = readSet.begin(), re = readSet.end(); ri != re; ri++) {
			out << ri->first << " is readed at " << "\n";
			for (vector<Event*>::iterator vi = ri->second.begin(), ve = ri->second.end(); vi != ve; vi++) {
				out << (*vi)->toString() << "\n";
			}
		}
		out << "<----WriteSet---->\n";
		for (map<string, vector<Event *> >::iterator wi = writeSet.begin(), we = writeSet.end(); wi != we; wi++) {
			out << wi->first << " is writed at \n";
			for (vector<Event*>::iterator vi = wi->second.begin(), ve = wi->second.end(); vi != ve; vi++) {
				out << (*vi)->toString() << "\n";
			}
		}
	}

	void Trace::printLockAndUnlock(raw_ostream& out) {
		out << "<----all_lock_unlock---->\n";
		for (map<string, vector<LockPair *> >::iterator li = all_lock_unlock.begin(), le = all_lock_unlock.end(); li != le; li++) {
			out << "Mutex:" << li->first << ":\n";
			for (vector<LockPair*>::iterator lpi = li->second.begin(), lpe = li->second.end(); lpi != lpe; lpi++) {
				out << "lock at " << (*lpi)->lockEvent->toString() << "\n";
				if ((*lpi)->unlockEvent) {
					out << "unlock at " << (*lpi)->unlockEvent->toString() << "\n";
				}
			}
		}
	}

	void Trace::printBarrierOperation(raw_ostream& out) {
		out << "<----all_barrier_operation---->\n";
		for (map<string, vector<Event*> >::iterator bi = all_barrier.begin(), be = all_barrier.end(); bi != be; bi++) {
			out << "Barrier:" << bi->first << ":\n";
			for (vector<Event*>::iterator vi = bi->second.begin(), ve = bi->second.end(); vi != ve; vi++) {
				out << (*vi)->toString() << "\n";
			}
		}
	}

	void Trace::printPrintfParam(raw_ostream& out) {
		out << "<----all_printf_param---->\n";
		for (map<string, Constant*>::iterator pi = printf_variable_value.begin(), pe = printf_variable_value.end(); pi != pe; pi++) {
			out << "Variable:" << pi->first << "\n";
			pi->second->print(out);
			out << "\n";
		}
	}

	void Trace::printGlobalVariableLast(raw_ostream& out) {
		out << "<----all_global_variable_last---->\n";
		for (map<string, Constant*>::iterator gi = global_variable_final.begin(), ge = global_variable_final.end(); gi != ge; gi++) {
			out << "Variable:" << gi->first << "\n";
			gi->second->print(out);
			out << "\n";
		}
	}

	void Trace::printGlobalVariableInitializer(raw_ostream& out) {
		out << "<----all_global_variable_initializer---->\n";
		for (map<string, Constant*>::iterator gi = global_variable_initializer.begin(), ge = global_variable_initializer.end(); gi != ge;
				gi++) {
			out << "Variable:" << gi->first << "\n";
			gi->second->print(out);
			out << "\n";
		}
	}

	void Trace::insertEvent(Event* event, unsigned threadId) {
		//cerr << threadId << " " << eventList.size() << endl;
		while (eventList.size() <= threadId) {
			eventList.resize(2 * eventList.size(), NULL);
		}
		if (!eventList[threadId]) {
			eventList[threadId] = new vector<Event*>();
//			std::cerr << "create eventList id : " << threadId << "\n";
		}
		eventList[threadId]->push_back(event);
		event->threadEventId = eventList[threadId]->size();
	}

	Event* Trace::createEvent(unsigned threadId, KInstruction* inst, uint64_t address, bool isLoad, int time, Event::EventType eventType) {
		ss.str("");
		ss << 'G';
		ss << address;
		string globalVarName = ss.str();
		if (isLoad) {
			ss << 'L';
		} else {
			ss << 'S';
		}
		ss << time;
		string globalVarFullName = ss.str();
		return new Event(threadId, nextEventId, "E" + Transfer::uint64toString(nextEventId++), inst, globalVarName, globalVarFullName,
				eventType);
	}

	Event* Trace::createEvent(unsigned threadId, KInstruction* inst, Event::EventType eventType) {
		return new Event(threadId, nextEventId, "E" + Transfer::uint64toString(nextEventId++), inst, "", "", eventType);
	}

	void Trace::insertThreadCreateOrJoin(pair<Event*, uint64_t> item, bool isThreadCreate) {
		if (isThreadCreate) {
			createThreadPoint.insert(item);
		} else {
			joinThreadPoint.insert(item);
		}
	}

	void Trace::insertReadSet(string name, Event* item) {
		map<string, vector<Event *> >::iterator mi = readSet.find(name);
		if (mi != readSet.end()) {
			mi->second.push_back(item);
		} else {
			vector<Event*> events;
			events.push_back(item);
			readSet.insert(make_pair(name, events));
		}
	}

	void Trace::insertWriteSet(string name, Event* item) {
		map<string, vector<Event *> >::iterator mi = writeSet.find(name);
		if (mi != writeSet.end()) {
			mi->second.push_back(item);
		} else {
			vector<Event*> events;
			events.push_back(item);
			writeSet.insert(make_pair(name, events));
		}
	}

	void Trace::insertWait(string condName, Event* wait, Event* associatedLock) {
		Wait_Lock* wl = new Wait_Lock();
		wl->wait = wait;
		wl->lock_by_wait = associatedLock;
		map<string, vector<Wait_Lock *> >::iterator mi = all_wait.find(condName);
		if (mi != all_wait.end()) {
			mi->second.push_back(wl);
		} else {
			vector<Wait_Lock*> events;
			events.push_back(wl);
			all_wait.insert(make_pair(condName, events));
		}
	}

	void Trace::insertSignal(string condName, Event* event) {
		map<string, vector<Event *> >::iterator mi = all_signal.find(condName);
		if (mi != all_signal.end()) {
			mi->second.push_back(event);
		} else {
			vector<Event*> events;
			events.push_back(event);
			all_signal.insert(make_pair(condName, events));
		}
	}

	void Trace::insertLockOrUnlock(unsigned threadId, string mutex, Event* event, bool isLock) {
		map<string, vector<LockPair *> >::iterator li = all_lock_unlock.find(mutex);
		if (li != all_lock_unlock.end()) {
			if (isLock) {
				LockPair* lp = new LockPair();
				lp->mutex = mutex;
				lp->lockEvent = event;
				lp->unlockEvent = NULL;
				lp->threadId = threadId;
				li->second.push_back(lp);
			} else {
				//find the LockPair
				LockPair* lp = NULL;
				for (vector<LockPair*>::iterator lpi = (li->second.end() - 1), lpe = li->second.begin(); lpi >= lpe; lpi--) {
					if ((*lpi)->threadId == threadId) {
						lp = *lpi;
						break;
					}
				}

				//LockPair* lp = li->second.back();
				if (lp->unlockEvent) {
					//assert(0 && "has been unlocked");
				} else {
					lp->unlockEvent = event;
				}
			}
		} else {
			if (isLock) {
				vector<LockPair*> lpVector;
				LockPair* lp = new LockPair();
				lp->mutex = mutex;
				lp->lockEvent = event;
				lp->unlockEvent = NULL;
				lp->threadId = threadId;
				lpVector.push_back(lp);
				all_lock_unlock.insert(make_pair(mutex, lpVector));
			} else {
				//assert(0 && "the mutex have not be first locked");
			}

		}
	}

	void Trace::insertBarrierOperation(string name, Event* event) {
		map<string, vector<Event*> >::iterator bi = all_barrier.find(name);
		if (bi != all_barrier.end()) {
			bi->second.push_back(event);
		} else {
			vector<Event*> boVector;
			boVector.push_back(event);
			all_barrier.insert(make_pair(name, boVector));
		}
	}

	void Trace::insertGlobalVariableInitializer(string name, Constant* initializer) {
		global_variable_initializer.insert(make_pair(name, initializer));
	}

	void Trace::insertPrintfParam(std::string name, Constant* param) {
		printf_variable_value.insert(make_pair(name, param));
	}

	void Trace::insertGlobalVariableLast(std::string name, Constant* finalValue) {
		global_variable_final.insert(make_pair(name, finalValue));
	}

	void Trace::insertPath(Event* event) {
		path.push_back(event);
	}

//void Trace::insertArgc(int argc) {
//	this->argc = argc;
//}

//void Trace::calculateAccessVector(vector<Event*>& prefix) {
//	map< string, vector<unsigned> > accessVector;
//	map< string, set<unsigned> > blockedMap;
//	map< string, set<unsigned> > waitingMap;
//	map< string, unsigned> lockedMap;
//	for (vector<Event*>::iterator ei = prefix.begin(), ee = prefix.end(); ei != ee; ei++) {
//		Event* event = *ei;
//		switch (event->inst->inst->getOpcode()) {
//		case Instruction::Call: {
//			string functionName = event->calledFunction->getName().str();
//			if (functionName == "pthread_cond_wait") {
//				map< string, set<unsigned> >::iterator wi =  waitingMap.find(event->condName);
//				if (wi == waitingMap.end()) {
//					set<unsigned> waitingSet;
//					waitingSet.insert(event->threadId);
//					waitingMap.insert(make_pair(event->condName, waitingSet));
//				} else {
//					wi->second.insert(event->threadId);
//				}
//				map< string, set<unsigned> >::iterator bi = blockedMap.find(event->mutexName);
//				if (bi != blockedMap.end()) {
//					for (vector<Event*>::iterator eni = ei; eni != ee; eni++) {
//						Event* following = *eni;
//						if (bi->second.erase(following->threadId) == 1) {
//							map< string, vector<unsigned> >::iterator ai = accessVector.find(event->mutexName);
//							lockedMap.insert(make_pair(event->mutexName, following->threadId));
//							if (ai == accessVector.end()) {
//								vector<unsigned> unlockList;
//								unlockList.push_back(following->threadId);
//								accessVector.insert(make_pair(event->mutexName, unlockList));
//							} else {
//								ai->second.push_back(following->threadId);
//							}
//						}
//						break;
//					}
//				}
//			} else if (functionName == "pthread_cond_signal") {
//				map< string, set<unsigned> >::iterator wi =  waitingMap.find(event->condName);
//				if (wi != waitingMap.end() && wi->second.size() != 0) {
//					for (vector<Event*>::iterator eni = ei; eni != ee; eni++) {
//						Event* following = *eni;
//						if (wi->second.erase(following->threadId) == 1) {
//							map< string, vector<unsigned> >::iterator ai = accessVector.find(event->condName);
//							if (ai == accessVector.end()) {
//								vector<unsigned> releaseList;
//								releaseList.push_back(following->threadId);
//								accessVector.insert(make_pair(event->condName, releaseList));
//							} else {
//								ai->second.push_back(following->threadId);
//							}
//						}
//					}
//				}
//			} else if (functionName == "pthread_cond_broadcast") {
//				map< string, set<unsigned> >::iterator wi =  waitingMap.find(event->condName);
//				if (wi != waitingMap.end()) {
//					wi->second.clear();
//				}
//			} else if (functionName == "pthread_mutex_lock") {
//				if (lockedMap.find(event->mutexName) == lockedMap.end()) {
//					lockedMap.insert(make_pair(event->mutexName, event->threadId));
//				} else {
//					map< string, set<unsigned> >::iterator bi = blockedMap.find(event->mutexName);
//					if (bi == blockedMap.end()) {
//						set<unsigned> blockSet;
//						blockSet.insert(event->threadId);
//						blockedMap.insert(make_pair(event->mutexName, blockSet));
//					} else {
//						bi->second.insert(event->threadId);
//					}
//				}
//			} else if (functionName == "pthread_mutex_unlock") {
//				if (lockedMap.find(event->mutexName) != lockedMap.end()) {
//					lockedMap.erase(event->mutexName);
//					map< string, set<unsigned> >::iterator bi = blockedMap.find(event->mutexName);
//					if (bi != blockedMap.end()) {
//						for (vector<Event*>::iterator eni = ei; eni != ee; eni++) {
//							Event* following = *eni;
//							if (bi->second.erase(following->threadId) == 1) {
//								map< string, vector<unsigned> >::iterator ai = accessVector.find(event->mutexName);
//								lockedMap.insert(make_pair(event->mutexName, following->threadId));
//								if (ai == accessVector.end()) {
//									vector<unsigned> unlockList;
//									unlockList.push_back(following->threadId);
//									accessVector.insert(make_pair(event->mutexName, unlockList));
//								} else {
//									ai->second.push_back(following->threadId);
//								}
//							}
//							break;
//						}
//					}
//				} else {
//					assert(0 && "has not been locked");
//				}
//			}
//			break;
//		}
//
//		}
//	}
//
//}

	void Trace::createAbstract() {
//	std::cerr << "createAbstract\n" ;
		for (unsigned tid = 0; tid < this->eventList.size(); tid++) {
			std::vector<Event*>* thread = this->eventList[tid];
			if (thread == NULL) //A bug introduced by lcyu
				continue;
			stringstream ss;
			string fn = thread->at(0)->inst->inst->getParent()->getParent()->getName().data();
			ss << fn << ":";
//		std::cerr << fn << "\n";
			for (unsigned i = 0; i < thread->size(); i++) {
				Event* event = thread->at(i);
				if (event->isConditionInst) {
//				std::cerr << "condition : " << event->condition << " ";
//				event->inst->inst->dump();
					ss << event->brCondition;
				}
			}
			abstract.push_back(ss.str());
//		std::cerr << "abstract : " << ss.str().c_str() << "\n";
		}
	}

	bool Trace::isEqual(Trace* trace) {
		if (this->abstract.empty()) {
			this->createAbstract();
		}

		if (trace->abstract.empty()) {
			trace->createAbstract();
		}

		unsigned selfSize = this->abstract.size();
		unsigned otherSize = trace->abstract.size();

		if (selfSize != otherSize)
			return false;

		vector<bool> otherFlag(otherSize, false);
		bool same = true;
		for (unsigned i = 0; i < selfSize; i++) {
			bool oneSame = false;
			for (unsigned j = 0; j < otherSize; j++) {
				if (otherFlag[j] == true)
					continue;
//			std::cerr << "this->abstract[i]  : " << this->abstract[i].c_str()  << "\n";
//			std::cerr << "trace->abstract[j] : " << trace->abstract[j].c_str() << "\n";
				if (this->abstract[i] == trace->abstract[j]) {
					otherFlag[j] = true;
					oneSame = true;
					break;
				}
			}
			if (oneSame == false) {
				same = false;
				break;
			}
		}
		return same;
	}

	std::string Trace::getAssemblyLine(std::string name) {
		std::stringstream varName;
		std::stringstream AssemblyLineName;
		varName.str("");
		unsigned int i = 0;
		while ((name.at(i) != 'S') && (name.at(i) != 'L')) {
			varName << name.at(i);
			i++;
		}
//	std::cerr << "getAssemblyLine name : " << name << "\n";
		std::map<std::string, std::vector<Event *> >::iterator all;
		if (name.at(i) == 'S') {
			all = allWriteSet.find(varName.str());
			if (all == allWriteSet.end()) {
				assert(0 && "allWriteSet getAssemblyLine can not find");
			}
		} else {
			all = allReadSet.find(varName.str());
			if (all == allReadSet.end()) {
				assert(0 && "allReadSet getAssemblyLine can not find");
			}
		}
		for (std::vector<Event *>::iterator it = all->second.begin(), ie = all->second.end(); it != ie; it++) {
			if ((*it)->globalName == name) {
//			return  Transfer::uint64toString((*it)->threadId) + "_" + Transfer::uint64toString((*it)->threadEventId);
				return Transfer::uint64toString((*it)->inst->info->assemblyLine);
//				AssemblyLineName << Transfer::uint64toString((*it)->inst->info->assemblyLine);
			}
		}

//		i = 0;
//		while ((name.at(i) != '_')) {
//			i++;
//		}
//
//		while (i < name.size()) {
//			AssemblyLineName << name.at(i);
//			i++;
//		}
//			std::cerr << "getAssemblyLine name : " << AssemblyLineName.str() << "\n";
//
//		return AssemblyLineName.str();



		assert(0 && "getAssemblyLine can not find");
		return 0;
	}

	std::string Trace::getLine(std::string name) {
		std::stringstream varName;
		varName.str("");
		unsigned int i = 0;
		while ((name.at(i) != 'S') && (name.at(i) != 'L')) {
			varName << name.at(i);
			i++;
		}
//	std::cerr << "getLine name : " << name << "\n";
		std::map<std::string, std::vector<Event *> >::iterator all;
		if (name.at(i) == 'S') {
			all = allWriteSet.find(varName.str());
			if (all == allWriteSet.end()) {
				assert(0 && "allWriteSet getLine can not find");
			}
		} else {
			all = allReadSet.find(varName.str());
			if (all == allReadSet.end()) {
				assert(0 && "allReadSet getLine can not find");
			}
		}
		for (std::vector<Event *>::iterator it = all->second.begin(), ie = all->second.end(); it != ie; it++) {
			if ((*it)->globalName == name) {
//			return  Transfer::uint64toString((*it)->threadId) + "_" + Transfer::uint64toString((*it)->threadEventId);
				return Transfer::uint64toString((*it)->inst->info->line);
			}
		}
		assert(0 && "getLine can not find");
		return 0;
	}

	Event* Trace::getEvent(std::string name) {
		std::stringstream varName;
		varName.str("");
		unsigned int i = 0;
		while ((name.at(i) != 'S') && (name.at(i) != 'L')) {
			varName << name.at(i);
			i++;
		}

//	std::cerr << "getEvent name : " << name << "\n";
		std::map<std::string, std::vector<Event *> >::iterator all;
		if (name.at(i) == 'S') {
			all = allWriteSet.find(varName.str());
			if (all == allWriteSet.end()) {
				assert(0 && "allWriteSet getEvent can not find");
			}
		} else {
			all = allReadSet.find(varName.str());
			if (all == allReadSet.end()) {
				assert(0 && "allReadSet getEvent can not find");
			}
		}

		Event* curr;

		for (std::vector<Event *>::iterator it = all->second.begin(), ie = all->second.end(); it != ie; it++) {
			if ((*it)->globalName == name) {
				curr = (*it);
				return curr;
			}
		}
		assert(0 && "getEvent can not find");
		return curr;
	}

} /* namespace klee */
