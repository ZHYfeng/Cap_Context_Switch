/*
 * VectorClock.cpp
 *
 *  Created on: Feb 24, 2016
 *      Author: zhy
 */

#include "DTAM.h"

#include <iostream>
#include <iterator>
#include <utility>
#include <vector>

#include "../../include/klee/Expr.h"
#include "../../include/klee/util/Ref.h"
#include "Event.h"

namespace klee {

DTAM::DTAM(RuntimeDataManager* data) :
		runtimeData(data) {
	trace = runtimeData->getCurrentTrace();
	cost = 0;
}

DTAM::~DTAM() {

}

void DTAM::DTAMParallel() {

	for (std::map<std::string, std::vector<Event *> >::iterator it = trace->allReadSet.begin(), ie =
			trace->allReadSet.end(); it != ie; it++) {
		std::vector<Event *> var = (*it).second;
		for (std::vector<Event *>::iterator itt = var.begin(), iee = var.end(); itt != iee; itt++) {
			std::string globalVarFullName = (*itt)->globalName;
			DTAMPoint *point = new DTAMPoint(globalVarFullName, (*itt)->vectorClock);
			allRead[globalVarFullName] = point;
		}
	}

	for (std::map<std::string, std::vector<Event *> >::iterator it = trace->allWriteSet.begin(), ie =
			trace->allWriteSet.end(); it != ie; it++) {
		std::vector<Event *> var = (*it).second;
		for (std::vector<Event *>::iterator itt = var.begin(), iee = var.end(); itt != iee; itt++) {
			std::string globalVarFullName = (*itt)->globalName;
			DTAMPoint *point = new DTAMPoint(globalVarFullName, (*itt)->vectorClock);
			for (std::vector<ref<klee::Expr> >::iterator ittt = (*itt)->relatedSymbolicExpr.begin(), ieee =
					(*itt)->relatedSymbolicExpr.end(); ittt != ieee; ittt++) {
				std::string value = FilterSymbolicExpr::getGlobalName(*ittt);
				point->affectedPoint.push_back(allRead[value]);
				allRead[value]->affectingPoint.push_back(point);
			}
			std::string varName = (*itt)->name;
			for (std::vector<Event *>::iterator ittt = trace->allReadSet[varName].begin(), ieee =
					trace->allReadSet[varName].end(); ittt != ieee; ittt++) {
				std::string value = (*ittt)->globalName;
				point->affectingPoint.push_back(allRead[value]);
				allRead[value]->affectedPoint.push_back(point);
			}
			allWrite[globalVarFullName] = point;
		}
	}

}

void DTAM::DTAMhybrid() {

	for (std::map<std::string, DTAMPoint*>::iterator it = allWrite.begin(), ie = allWrite.end(); it != ie; it++) {
		DTAMPoint *point = (*it).second;
		std::string name = point->name;
		for (std::vector<DTAMPoint*>::iterator itt = point->affectingPoint.begin(); itt < point->affectingPoint.end();
				) {
			if ((*point) <= (*itt)) {
				itt++;
			} else {
				for (std::vector<DTAMPoint*>::iterator ittt = (*itt)->affectingPoint.begin();
						ittt < (*itt)->affectingPoint.end(); ittt++) {
					if (*ittt == point) {
						(*itt)->affectedPoint.erase(ittt);
					}
				}
				point->affectingPoint.erase(itt);
			}
		}
	}
}

void DTAM::initTaint(std::vector<DTAMPoint*> &remainPoint) {

	remainPoint.clear();

	for (std::map<std::string, DTAMPoint*>::iterator it = allWrite.begin(), ie = allWrite.end(); it != ie; it++) {
		DTAMPoint *point = (*it).second;
		std::string name = point->name;
		if (trace->DTAMSerial.find(name) != trace->DTAMSerial.end()) {
			point->isTaint = true;
			remainPoint.push_back(point);
		} else {
			point->isTaint = false;
		}
	}

	for (std::map<std::string, DTAMPoint*>::iterator it = allRead.begin(), ie = allRead.end(); it != ie; it++) {
		DTAMPoint *point = (*it).second;
		std::string name = point->name;
		if (trace->DTAMSerial.find(name) != trace->DTAMSerial.end()) {
			point->isTaint = true;
			remainPoint.push_back(point);
		} else {
			point->isTaint = false;
		}
	}

}

void DTAM::propagateTaint(std::vector<DTAMPoint*> &remainPoint) {

	std::vector<DTAMPoint*>::iterator it;
	while (!remainPoint.empty()) {
		it = remainPoint.end();
		it--;
		DTAMPoint *point = (*it);
		remainPoint.pop_back();
		for (std::vector<DTAMPoint*>::iterator itt = point->affectingPoint.begin(), iee = point->affectingPoint.end();
				itt < iee; itt++) {
			if (!(*itt)->isTaint) {
				(*itt)->isTaint = true;
				remainPoint.push_back((*itt));
			}
		}
	}
}

void DTAM::getTaint(std::set<std::string> &taint) {

	for (std::map<std::string, DTAMPoint*>::iterator it = allWrite.begin(), ie = allWrite.end(); it != ie; it++) {
		DTAMPoint *point = (*it).second;
		if (point->isTaint == true) {
			std::string name = point->name;
			taint.insert(name);
		}
	}

	for (std::map<std::string, DTAMPoint*>::iterator it = allRead.begin(), ie = allRead.end(); it != ie; it++) {
		DTAMPoint *point = (*it).second;
		if (point->isTaint == true) {
			std::string name = point->name;
			taint.insert(name);
		}
	}
}

void DTAM::dtam() {

	std::vector<DTAMPoint*> remainPoint;

	for (std::set<std::string>::iterator it = trace->DTAMSerial.begin(), ie = trace->DTAMSerial.end(); it != ie; it++) {
		std::string name = (*it);
		runtimeData->allDTAMSerialMap.insert(trace->getAssemblyLine(name));
		trace->DTAMSerialMap.insert(trace->getAssemblyLine(name));
	}
	runtimeData->DTAMSerialMap.push_back(trace->DTAMSerialMap.size());
	runtimeData->DTAMSerial.push_back(trace->DTAMSerial.size());

	gettimeofday(&start, NULL);
	DTAMParallel();
	initTaint(remainPoint);
	propagateTaint(remainPoint);
	getTaint(trace->DTAMParallel);
	std::set<std::string> &potentialTaint = trace->potentialTaint;
	for (std::set<std::string>::iterator it = trace->DTAMParallel.begin(), ie = trace->DTAMParallel.end(); it != ie;
			it++) {
		std::string name = (*it);
		runtimeData->allDTAMParallelMap.insert(trace->getAssemblyLine(name));
		trace->DTAMParallelMap.insert(trace->getAssemblyLine(name));
		potentialTaint.insert(filter.getName(name));
	}
	runtimeData->DTAMParallelMap.push_back(trace->DTAMParallelMap.size());
	runtimeData->DTAMParallel.push_back(trace->DTAMParallel.size());
	gettimeofday(&finish, NULL);
	cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
	runtimeData->DTAMParallelCost += cost;
	runtimeData->allDTAMParallelCost.push_back(cost);

	gettimeofday(&start, NULL);
	DTAMhybrid();
	initTaint(remainPoint);
	propagateTaint(remainPoint);
	getTaint(trace->DTAMhybrid);
	for (std::set<std::string>::iterator it = trace->DTAMhybrid.begin(), ie = trace->DTAMhybrid.end(); it != ie; it++) {
		std::string name = (*it);
		runtimeData->allDTAMhybridMap.insert(trace->getAssemblyLine(name));
		trace->DTAMhybridMap.insert(trace->getAssemblyLine(name));
	}
	runtimeData->DTAMhybridMap.push_back(trace->DTAMhybridMap.size());
	runtimeData->DTAMhybrid.push_back(trace->DTAMhybrid.size());
	gettimeofday(&finish, NULL);
	cost = (double) (finish.tv_sec * 1000000UL + finish.tv_usec - start.tv_sec * 1000000UL - start.tv_usec) / 1000000UL;
	runtimeData->DTAMhybridCost += cost;
	runtimeData->allDTAMhybridCost.push_back(cost);
}

}
