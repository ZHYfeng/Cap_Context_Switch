/*
 * RuntimeDataManager.h
 *
 *  Created on: Jun 10, 2014
 *      Author: ylc
 */

#ifndef RUNTIMEDATAMANAGER_H_
#define RUNTIMEDATAMANAGER_H_

#include <iostream>
#include <list>
#include <set>
#include <string>
#include <vector>

#include "Prefix.h"
#include "Trace.h"



namespace klee {

class RuntimeDataManager {

	private:
		std::vector<Trace*> traceList; // store all traces;
		Trace* currentTrace; // trace associated with current execution
		std::set<Trace*> testedTraceList; // traces which have been examined
		std::list<Prefix*> scheduleSet; // prefixes which have not been examined

	public:
		unsigned allFormulaNum;
		unsigned allGlobal;
		unsigned brGlobal;
		unsigned solvingTimes;
		unsigned satBranch;
		unsigned unSatBranch;
		unsigned uunSatBranch;
		double runningCost;
		double solvingCost;
		double satCost;
		double unSatCost;

		double TaintCost;
		double PTSCost;
		double DTAMCost;
		double DTAMParallelCost;
		double DTAMhybridCost;

		std::vector<double> allTaintCost;
		std::vector<double> allPTSCost;
		std::vector<double> allDTAMCost;
		std::vector<double> allDTAMParallelCost;
		std::vector<double> allDTAMhybridCost;

		std::vector<unsigned> DTAMSerial;
		std::vector<unsigned> DTAMParallel;
		std::vector<unsigned> DTAMhybrid;

		std::vector<unsigned> taint;
		std::vector<unsigned> taintPTS;
		std::vector<unsigned> noTaintPTS;

		std::vector<unsigned> DTAMSerialMap;
		std::vector<unsigned> DTAMParallelMap;
		std::vector<unsigned> DTAMhybridMap;
		std::vector<unsigned> TaintMap;

		std::set<std::string> allTaintMap;
		std::set<std::string> allDTAMSerialMap;
		std::set<std::string> allDTAMParallelMap;
		std::set<std::string> allDTAMhybridMap;

		RuntimeDataManager();
		virtual ~RuntimeDataManager();

		Trace* createNewTrace(unsigned traceId);
		Trace* getCurrentTrace();
		void addScheduleSet(Prefix* prefix);
		void printCurrentTrace(bool file);
		Prefix* getNextPrefix();
		void clearAllPrefix();
		bool isCurrentTraceUntested();
		void printAllPrefix(std::ostream &out);
		void printAllTrace(std::ostream &out);

};

}
#endif /* RUNTIMEDATAMANAGER_H_ */
