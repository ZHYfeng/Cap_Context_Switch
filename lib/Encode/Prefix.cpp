/*
 * Prefix.cpp
 *
 *  Created on: 2015年2月3日
 *      Author: berserker
 */

#include "Prefix.h"

#include <llvm/IR/Instruction.h>
#include <iterator>

#include "../../include/klee/Internal/Module/InstructionInfoTable.h"

using namespace ::std;
using namespace ::llvm;

namespace klee {

Prefix::Prefix(vector<Event*>& eventList, std::map<Event*, uint64_t>& threadIdMap, std::string name) :
		eventList(eventList), threadIdMap(threadIdMap), name(name) {
	position = this->eventList.begin();
}

void Prefix::reuse() {
	position = this->eventList.begin();
}

Prefix::~Prefix() {

}

vector<Event*>* Prefix::getEventList() {
	return &eventList;
}

void Prefix::increasePosition() {
	if (!isFinished()) {
		position++;
	}
}

bool Prefix::isFinished() {
	return position == eventList.end();
}

Prefix::EventIterator Prefix::begin() {
	return eventList.begin();
}

Prefix::EventIterator Prefix::end() {
	return eventList.end();
}

Prefix::EventIterator Prefix::current() {
	return Prefix::EventIterator(position);
}

uint64_t Prefix::getNextThreadId() {
	assert(!isFinished());
	Event* event = *position;
	map<Event*, uint64_t>::iterator ti = threadIdMap.find(event);
	return ti->second;
}

unsigned Prefix::getCurrentEventThreadId() {
	assert(!isFinished());
	Event* event = *position;
	return event->threadId;
}

void Prefix::print(ostream &out) {
	for (vector<Event*>::iterator ei = eventList.begin(), ee = eventList.end(); ei != ee; ei++) {
		Event* event = *ei;
		out << "thread" << event->threadId << " " << event->inst->info->file << " " << event->inst->info->line << ": "
				<< event->inst->inst->getOpcodeName();
		map<Event*, uint64_t>::iterator ti = threadIdMap.find(event);
		if (ti != threadIdMap.end()) {
			out << "\n child threadId = " << ti->second;
		}
		out << endl;
	}
	out << "prefix print finished\n";
}

void Prefix::print(raw_ostream &out) {
	for (vector<Event*>::iterator ei = eventList.begin(), ee = eventList.end(); ei != ee; ei++) {
		Event* event = *ei;
		out << "thread" << event->threadId << " " << event->inst->info->file << " " << event->inst->info->line << ": ";
		event->inst->inst->print(out);
		map<Event*, uint64_t>::iterator ti = threadIdMap.find(event);
		if (ti != threadIdMap.end()) {
			out << "\n child threadId = " << ti->second;
		}
		out << '\n';
	}
	out << "prefix print finished\n";
}

KInstruction* Prefix::getCurrentInst() {
	assert(!isFinished());
	Event* event = *position;
	return event->inst;
}
std::string Prefix::getName() {
	return name;
}

} /* namespace klee */
